import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

void main() {
  runApp(const AdsbApp());
}

class AdsbApp extends StatelessWidget {
  const AdsbApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ADS-B Frames',
      theme: ThemeData(colorSchemeSeed: Colors.blue, useMaterial3: true),
      home: const FrameTablePage(),
    );
  }
}

/// One decoded frame, as published by the backend's --ws-port
/// (see proto/src/main.cpp: frame_to_json).
class FrameRecord {
  final int idx;
  final int bits;
  final double confidence;
  final int df;
  final String? icao;
  final bool crcOk;
  final String payload;

  FrameRecord({
    required this.idx,
    required this.bits,
    required this.confidence,
    required this.df,
    required this.icao,
    required this.crcOk,
    required this.payload,
  });

  factory FrameRecord.fromJson(Map<String, dynamic> j) {
    return FrameRecord(
      idx: j['idx'] as int,
      bits: j['bits'] as int,
      confidence: (j['confidence'] as num).toDouble(),
      df: j['df'] as int,
      icao: j['icao'] as String?,
      crcOk: j['crc_ok'] as bool,
      payload: j['payload'] as String,
    );
  }
}

/// Current tracked state of one aircraft, as published by the backend's
/// --ws-port on every change (see proto/src/main.cpp: aircraft_to_json).
/// Unlike FrameRecord this isn't appended to a log -- each message carries
/// the aircraft's full current state, so the frontend just replaces its map
/// entry for that ICAO wholesale.
class AircraftRecord {
  final String icao;
  final String? callsign;
  final int? altitudeFt;
  final double? lat;
  final double? lon;
  final double? groundSpeedKt;
  final double? trackDeg;
  final int? verticalRateFpm;
  final double lastSeenS;

  AircraftRecord({
    required this.icao,
    required this.callsign,
    required this.altitudeFt,
    required this.lat,
    required this.lon,
    required this.groundSpeedKt,
    required this.trackDeg,
    required this.verticalRateFpm,
    required this.lastSeenS,
  });

  factory AircraftRecord.fromJson(Map<String, dynamic> j) {
    return AircraftRecord(
      icao: j['icao'] as String,
      callsign: j['callsign'] as String?,
      altitudeFt: j['altitude_ft'] as int?,
      lat: (j['lat'] as num?)?.toDouble(),
      lon: (j['lon'] as num?)?.toDouble(),
      groundSpeedKt: (j['ground_speed_kt'] as num?)?.toDouble(),
      trackDeg: (j['track_deg'] as num?)?.toDouble(),
      verticalRateFpm: j['vertical_rate_fpm'] as int?,
      lastSeenS: (j['last_seen_s'] as num).toDouble(),
    );
  }
}

/// One FFT snapshot of the raw pre-resample IQ, as published by the
/// backend's --ws-port (see proto/src/main.cpp: spectrum_to_json). `bins` is
/// already fftshifted -- bin 0 is freqHz - rateHz/2, the last bin is
/// freqHz + rateHz/2 -- so it plots directly against a linear frequency axis.
class SpectrumFrame {
  final double freqHz;
  final double rateHz;
  final List<double> bins;

  SpectrumFrame({required this.freqHz, required this.rateHz, required this.bins});

  factory SpectrumFrame.fromJson(Map<String, dynamic> j) {
    return SpectrumFrame(
      freqHz: (j['freq_hz'] as num).toDouble(),
      rateHz: (j['rate_hz'] as num).toDouble(),
      bins: (j['bins'] as List).map((e) => (e as num).toDouble()).toList(),
    );
  }
}

enum FrameSortField { idx, df, icao, confidence, crc }

// Downlink format codes ADS-B/Mode S actually defines. A frame decoded with
// a df outside this set almost certainly came from a false-positive preamble
// detection on noise, not a real transponder reply. df 24-31 all decode to
// Comm-D (ELM) -- the top 3 bits (110) are what's significant, not the full
// 5-bit value -- so the whole range counts as known.
final Set<int> _knownDfValues = {0, 4, 5, 11, 16, 17, 18, 19, 20, 21, ...List.generate(8, (i) => 24 + i)};

enum AircraftSortField { icao, callsign, altitude, lat, lon, speed, track, verticalRate, lastSeen }

class FrameTablePage extends StatefulWidget {
  const FrameTablePage({super.key});

  @override
  State<FrameTablePage> createState() => _FrameTablePageState();
}

class _FrameTablePageState extends State<FrameTablePage> with SingleTickerProviderStateMixin {
  // 127.0.0.1, not "localhost": the backend's WsListener binds an IPv4
  // socket only, and "localhost" can resolve to ::1 first on Linux, which
  // fails to connect instead of falling back to the v4 address.
  final _urlController = TextEditingController(text: 'ws://127.0.0.1:8765');
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connected = false;
  String? _error;
  late final TabController _tabController;

  // Frames arrive far faster than the table can usefully redraw or the user
  // can read, so incoming messages are buffered here and only merged into
  // state (via a single setState) on a timer -- see _flushUpdates.
  static const int _maxFrames = 5000;
  static const Duration _uiUpdateInterval = Duration(seconds: 1);

  final Queue<FrameRecord> _frames = Queue<FrameRecord>();
  final List<FrameRecord> _pendingFrames = [];
  int _frameSortColumnIndex = 0;
  bool _frameSortAscending = false;

  // Cumulative counts across the whole connection, independent of the
  // _maxFrames display cap -- a frame that ages out of the table still
  // counts here. Rate is computed per flush interval only (no smoothing
  // across intervals), so it reflects exactly what arrived in that window.
  int _statsTotalFrames = 0;
  int _statsCrcOk = 0;
  int _statsUnknownDf = 0;
  final Map<int, int> _statsByDf = {};
  double _statsFramesPerSec = 0;
  DateTime? _statsWindowStart;

  final Map<String, AircraftRecord> _aircraft = {};
  final Map<String, AircraftRecord> _pendingAircraft = {};

  // Accumulated in-memory position history per aircraft, oldest first, for
  // drawing a route trail on the map -- purely a running log of what's
  // arrived since this client connected (see AircraftRecord's doc comment:
  // each update is the aircraft's full current state, not a delta, so the
  // trail has to be built up here rather than read off any single message).
  // Capped per-aircraft so a long-running session doesn't grow this map
  // unboundedly; the oldest points are dropped first, same trim policy as
  // _frames.
  static const int _maxTrailPoints = 1000;
  final Map<String, List<LatLng>> _aircraftTrails = {};
  int _aircraftSortColumnIndex = 0;
  bool _aircraftSortAscending = true;
  Timer? _uiUpdateTimer;

  // Aircraft older than this (relative to the newest last_seen_s we've
  // received) are shown greyed-out rather than removed outright. last_seen_s
  // is stream position, not wall-clock time -- using it instead of
  // DateTime.now() keeps this correct in file-playback mode, where replay
  // can run much faster or slower than real time.
  static const double _staleAfterS = 30;
  double _latestStreamTimeS = 0;

  // Wall-clock time each aircraft's state was last updated, for the "last
  // seen" table column -- shown as seconds-ago from the real current time,
  // not stream position, since that's what the person watching the table
  // actually wants to know.
  final Map<String, DateTime> _aircraftLastSeenWallClock = {};

  // Selecting a row in the aircraft table highlights the matching marker on
  // the map, and vice versa.
  String? _selectedIcao;

  final _mapController = MapController();
  // Center the map on the first aircraft position we see, then leave the
  // user's pan/zoom alone -- re-centering on every update would fight
  // anyone trying to look around.
  bool _mapAutoCentered = false;

  // Width of the aircraft table pane; dragged via the divider in
  // _buildAircraftPane. 640 is wide enough to show all columns, including
  // "last seen", without needing to scroll horizontally.
  double _tableWidth = 640;
  static const double _minPaneWidth = 200;

  // Spectrum snapshots arrive far slower than frames (backend throttles to
  // one every 150ms regardless of sample rate -- see kSpectrumInterval in
  // main.cpp), so unlike frames/aircraft these go straight into their own
  // ValueNotifier on arrival instead of through the once-a-second
  // _flushUpdates batch: only the waterfall widget listening to it repaints,
  // not the whole page, and at ~7Hz that's cheap either way.
  static const int _maxSpectrumRows = 150;
  final ValueNotifier<List<SpectrumFrame>> _spectrumHistory = ValueNotifier<List<SpectrumFrame>>([]);

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 3, vsync: this);
  }

  @override
  void dispose() {
    _uiUpdateTimer?.cancel();
    _sub?.cancel();
    _channel?.sink.close();
    _urlController.dispose();
    _tabController.dispose();
    _spectrumHistory.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    final url = _urlController.text.trim();
    if (url.isEmpty) return;
    _uiUpdateTimer?.cancel();
    setState(() {
      _error = null;
      _frames.clear();
      _pendingFrames.clear();
      _aircraft.clear();
      _pendingAircraft.clear();
      _aircraftTrails.clear();
      _mapAutoCentered = false;
      _latestStreamTimeS = 0;
      _aircraftLastSeenWallClock.clear();
      _selectedIcao = null;
      _statsTotalFrames = 0;
      _statsCrcOk = 0;
      _statsUnknownDf = 0;
      _statsByDf.clear();
      _statsFramesPerSec = 0;
      _statsWindowStart = DateTime.now();
      _spectrumHistory.value = [];
    });
    final channel = WebSocketChannel.connect(Uri.parse(url));
    _channel = channel;
    try {
      await channel.ready;
    } catch (e) {
      setState(() {
        _error = '$e';
        _connected = false;
      });
      return;
    }
    _sub = channel.stream.listen(
      (event) {
        // Frames/aircraft can arrive at rates far higher than the UI can
        // (or should) redraw at, so just buffer them here -- _flushUpdates,
        // on a timer, is what actually calls setState.
        try {
          final json = jsonDecode(event as String) as Map<String, dynamic>;
          switch (json['type']) {
            case 'frame':
              _pendingFrames.add(FrameRecord.fromJson(json));
            case 'aircraft':
              final ac = AircraftRecord.fromJson(json);
              _pendingAircraft[ac.icao] = ac;
            case 'history':
              // Backend-persisted track for one aircraft, sent once right
              // after connect (see proto/src/ws_publisher.cpp:handle_client)
              // -- applied straight to _aircraftTrails rather than routed
              // through _pendingAircraft, since it's a one-shot backfill of
              // past points, not a live per-frame update.
              final icao = json['icao'] as String;
              final points = json['track'] as List;
              final track = points
                  .map((p) => p as Map<String, dynamic>)
                  .where((p) => p['lat'] != null && p['lon'] != null)
                  .map((p) => LatLng((p['lat'] as num).toDouble(), (p['lon'] as num).toDouble()))
                  .toList();
              if (track.length > _maxTrailPoints) {
                track.removeRange(0, track.length - _maxTrailPoints);
              }
              setState(() {
                _aircraftTrails[icao] = track;
              });
            case 'spectrum':
              final rows = [..._spectrumHistory.value, SpectrumFrame.fromJson(json)];
              if (rows.length > _maxSpectrumRows) {
                rows.removeRange(0, rows.length - _maxSpectrumRows);
              }
              _spectrumHistory.value = rows;
          }
        } catch (_) {
          // Ignore malformed lines rather than tearing down the connection.
        }
      },
      onError: (e) => setState(() {
        _error = '$e';
        _connected = false;
      }),
      onDone: () => setState(() => _connected = false),
    );
    _uiUpdateTimer = Timer.periodic(_uiUpdateInterval, (_) => _flushUpdates());
    setState(() => _connected = true);
  }

  /// Merges buffered frames/aircraft into the displayed state in one
  /// setState, trims the frame log to _maxFrames (dropping the oldest), and
  /// runs once-only map auto-centering off the batch's first new position.
  void _flushUpdates() {
    // No early-return-if-nothing-pending here: even with no new data this
    // still needs to fire every tick so the "last seen" column's ago-times
    // (and staleness) keep advancing against the real clock while idle.
    setState(() {
      final now = DateTime.now();
      final elapsedS = now.difference(_statsWindowStart ?? now).inMicroseconds / 1e6;
      _statsFramesPerSec = elapsedS > 0 ? _pendingFrames.length / elapsedS : 0;
      _statsWindowStart = now;
      for (final f in _pendingFrames) {
        _statsTotalFrames++;
        if (f.crcOk) _statsCrcOk++;
        if (!_knownDfValues.contains(f.df)) _statsUnknownDf++;
        _statsByDf[f.df] = (_statsByDf[f.df] ?? 0) + 1;
      }

      _frames.addAll(_pendingFrames);
      while (_frames.length > _maxFrames) {
        _frames.removeFirst();
      }
      _pendingFrames.clear();

      _aircraft.addAll(_pendingAircraft);
      LatLng? firstNewPosition;
      if (!_mapAutoCentered) {
        for (final ac in _pendingAircraft.values) {
          if (ac.lat != null && ac.lon != null) {
            firstNewPosition = LatLng(ac.lat!, ac.lon!);
            break;
          }
        }
      }
      for (final ac in _pendingAircraft.values) {
        if (ac.lastSeenS > _latestStreamTimeS) _latestStreamTimeS = ac.lastSeenS;
        _aircraftLastSeenWallClock[ac.icao] = now;
        if (ac.lat != null && ac.lon != null) {
          final trail = _aircraftTrails.putIfAbsent(ac.icao, () => []);
          trail.add(LatLng(ac.lat!, ac.lon!));
          if (trail.length > _maxTrailPoints) {
            trail.removeRange(0, trail.length - _maxTrailPoints);
          }
        }
      }
      _pendingAircraft.clear();

      if (firstNewPosition != null) {
        _mapAutoCentered = true;
        final pos = firstNewPosition;
        WidgetsBinding.instance.addPostFrameCallback((_) {
          try {
            _mapController.move(pos, 9);
          } catch (_) {
            // Map isn't mounted (user hasn't opened the Aircraft tab
            // yet) -- it'll just show the default view when opened.
          }
        });
      }
    });
  }

  bool _isStale(AircraftRecord a) => _latestStreamTimeS - a.lastSeenS > _staleAfterS;

  String _agoLabel(String icao) {
    final t = _aircraftLastSeenWallClock[icao];
    if (t == null) return '';
    return '${DateTime.now().difference(t).inSeconds}s ago';
  }

  void _toggleSelected(String icao) {
    setState(() => _selectedIcao = _selectedIcao == icao ? null : icao);
  }

  void _disconnect() {
    _uiUpdateTimer?.cancel();
    _uiUpdateTimer = null;
    _sub?.cancel();
    _channel?.sink.close();
    _sub = null;
    _channel = null;
    setState(() => _connected = false);
  }

  void _onFrameSort(int columnIndex, bool ascending) {
    setState(() {
      _frameSortColumnIndex = columnIndex;
      _frameSortAscending = ascending;
    });
  }

  void _onAircraftSort(int columnIndex, bool ascending) {
    setState(() {
      _aircraftSortColumnIndex = columnIndex;
      _aircraftSortAscending = ascending;
    });
  }

  List<FrameRecord> get _sortedFrames {
    final sorted = List<FrameRecord>.of(_frames);
    int cmp(FrameRecord a, FrameRecord b) {
      switch (FrameSortField.values[_frameSortColumnIndex]) {
        case FrameSortField.idx:
          return a.idx.compareTo(b.idx);
        case FrameSortField.df:
          return a.df.compareTo(b.df);
        case FrameSortField.icao:
          return (a.icao ?? '').compareTo(b.icao ?? '');
        case FrameSortField.confidence:
          return a.confidence.compareTo(b.confidence);
        case FrameSortField.crc:
          return (a.crcOk ? 1 : 0).compareTo(b.crcOk ? 1 : 0);
      }
    }

    sorted.sort((a, b) => _frameSortAscending ? cmp(a, b) : cmp(b, a));
    return sorted;
  }

  List<AircraftRecord> get _sortedAircraft {
    final sorted = _aircraft.values.toList();
    int cmpNum(num? a, num? b) {
      if (a == null && b == null) return 0;
      if (a == null) return -1;
      if (b == null) return 1;
      return a.compareTo(b);
    }

    int cmp(AircraftRecord a, AircraftRecord b) {
      switch (AircraftSortField.values[_aircraftSortColumnIndex]) {
        case AircraftSortField.icao:
          return a.icao.compareTo(b.icao);
        case AircraftSortField.callsign:
          return (a.callsign ?? '').compareTo(b.callsign ?? '');
        case AircraftSortField.altitude:
          return cmpNum(a.altitudeFt, b.altitudeFt);
        case AircraftSortField.lat:
          return cmpNum(a.lat, b.lat);
        case AircraftSortField.lon:
          return cmpNum(a.lon, b.lon);
        case AircraftSortField.speed:
          return cmpNum(a.groundSpeedKt, b.groundSpeedKt);
        case AircraftSortField.track:
          return cmpNum(a.trackDeg, b.trackDeg);
        case AircraftSortField.verticalRate:
          return cmpNum(a.verticalRateFpm, b.verticalRateFpm);
        case AircraftSortField.lastSeen:
          return a.lastSeenS.compareTo(b.lastSeenS);
      }
    }

    sorted.sort((a, b) => _aircraftSortAscending ? cmp(a, b) : cmp(b, a));
    return sorted;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('ADS-B Frames'),
        bottom: TabBar(
          controller: _tabController,
          tabs: [
            Tab(text: 'Frames (${_frames.length})'),
            Tab(text: 'Aircraft (${_aircraft.length})'),
            const Tab(text: 'Spectrum'),
          ],
        ),
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _urlController,
                    decoration: const InputDecoration(
                      labelText: 'WebSocket URL',
                      border: OutlineInputBorder(),
                    ),
                    enabled: !_connected,
                  ),
                ),
                const SizedBox(width: 12),
                FilledButton(
                  onPressed: _connected ? _disconnect : _connect,
                  child: Text(_connected ? 'Disconnect' : 'Connect'),
                ),
              ],
            ),
          ),
          if (_error != null)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12),
              child: Text(_error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
            ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            child: Row(
              children: [
                Icon(
                  Icons.circle,
                  size: 10,
                  color: _connected ? Colors.green : Colors.grey,
                ),
                const SizedBox(width: 6),
                Text(_connected ? 'Connected' : 'Disconnected'),
              ],
            ),
          ),
          if (_statsTotalFrames > 0) _buildStatsBar(),
          const Divider(height: 1),
          Expanded(
            child: TabBarView(
              controller: _tabController,
              children: [
                _buildFrameTable(),
                _buildAircraftPane(),
                _buildSpectrumPane(),
              ],
            ),
          ),
        ],
      ),
    );
  }

  /// Summary of incoming-frame health: throughput and how much of what's
  /// being demodulated looks like a real transponder reply vs. noise. Always
  /// visible (not per-tab) since it's about the link, not either table.
  Widget _buildStatsBar() {
    final total = _statsTotalFrames;
    final crcOkPct = _statsCrcOk / total * 100;
    final unknownDfPct = _statsUnknownDf / total * 100;
    return Theme(
      data: Theme.of(context).copyWith(dividerColor: Colors.transparent),
      child: ExpansionTile(
        tilePadding: const EdgeInsets.symmetric(horizontal: 12),
        childrenPadding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
        title: Wrap(
          spacing: 20,
          runSpacing: 4,
          children: [
            _statChip('$total', 'frames'),
            _statChip(_statsFramesPerSec.toStringAsFixed(1), 'frames/s'),
            _statChip('${crcOkPct.toStringAsFixed(1)}%', 'crc ok'),
            _statChip(
              '${unknownDfPct.toStringAsFixed(1)}%',
              'unrecognized df',
              warn: _statsUnknownDf > 0,
            ),
          ],
        ),
        children: [_buildDfBreakdown()],
      ),
    );
  }

  Widget _statChip(String value, String label, {bool warn = false}) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          value,
          style: TextStyle(
            fontWeight: FontWeight.bold,
            color: warn ? Colors.orange[800] : null,
          ),
        ),
        Text(label, style: TextStyle(fontSize: 11, color: Colors.grey[600])),
      ],
    );
  }

  /// Per-downlink-format counts, sorted by frequency. DFs outside
  /// _knownDfValues are flagged -- they're the frames most likely to be
  /// false-positive preamble detections on noise rather than real replies.
  Widget _buildDfBreakdown() {
    final total = _statsTotalFrames;
    final entries = _statsByDf.entries.toList()..sort((a, b) => b.value.compareTo(a.value));
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        for (final e in entries)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 1),
            child: Text(
              'df ${e.key.toString().padLeft(2)}: ${e.value.toString().padLeft(6)} '
              '(${(e.value / total * 100).toStringAsFixed(1)}%)'
              '${_knownDfValues.contains(e.key) ? '' : '  — unrecognized, likely noise'}',
              style: TextStyle(
                fontFamily: 'monospace',
                color: _knownDfValues.contains(e.key) ? null : Colors.orange[800],
              ),
            ),
          ),
      ],
    );
  }

  Widget _buildFrameTable() {
    final frames = _sortedFrames;
    return SingleChildScrollView(
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: DataTable(
          sortColumnIndex: _frameSortColumnIndex,
          sortAscending: _frameSortAscending,
          columns: [
            DataColumn(label: const Text('idx'), numeric: true, onSort: _onFrameSort),
            DataColumn(label: const Text('df'), numeric: true, onSort: _onFrameSort),
            DataColumn(label: const Text('icao'), onSort: _onFrameSort),
            DataColumn(label: const Text('confidence'), numeric: true, onSort: _onFrameSort),
            DataColumn(label: const Text('crc'), onSort: _onFrameSort),
            const DataColumn(label: Text('bits'), numeric: true),
            const DataColumn(label: Text('payload')),
          ],
          rows: [
            for (final f in frames)
              DataRow(
                cells: [
                  DataCell(Text('${f.idx}')),
                  DataCell(Text('${f.df}')),
                  DataCell(Text(f.icao ?? '??????')),
                  DataCell(Text(f.confidence.toStringAsFixed(1))),
                  DataCell(Text(
                    f.crcOk ? 'ok' : 'fail',
                    style: TextStyle(color: f.crcOk ? Colors.green : Colors.red),
                  )),
                  DataCell(Text('${f.bits}')),
                  DataCell(Text(f.payload, style: const TextStyle(fontFamily: 'monospace'))),
                ],
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildAircraftTable() {
    final aircraft = _sortedAircraft;
    return SingleChildScrollView(
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: DataTable(
          sortColumnIndex: _aircraftSortColumnIndex,
          sortAscending: _aircraftSortAscending,
          showCheckboxColumn: false,
          columns: [
            DataColumn(label: const Text('icao'), onSort: _onAircraftSort),
            DataColumn(label: const Text('callsign'), onSort: _onAircraftSort),
            DataColumn(label: const Text('alt (ft)'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('lat'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('lon'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('gs (kt)'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('trk'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('vr (fpm)'), numeric: true, onSort: _onAircraftSort),
            DataColumn(label: const Text('last seen'), numeric: true, onSort: _onAircraftSort),
          ],
          rows: [for (final a in aircraft) _buildAircraftRow(a)],
        ),
      ),
    );
  }

  DataRow _buildAircraftRow(AircraftRecord a) {
    final style = _isStale(a) ? TextStyle(color: Colors.grey[500]) : null;
    Widget cell(String text) => Text(text, style: style);
    return DataRow(
      selected: a.icao == _selectedIcao,
      onSelectChanged: (_) => _toggleSelected(a.icao),
      cells: [
        DataCell(cell(a.icao)),
        DataCell(cell(a.callsign ?? '')),
        DataCell(cell(a.altitudeFt?.toString() ?? '')),
        DataCell(cell(a.lat?.toStringAsFixed(4) ?? '')),
        DataCell(cell(a.lon?.toStringAsFixed(4) ?? '')),
        DataCell(cell(a.groundSpeedKt?.toStringAsFixed(0) ?? '')),
        DataCell(cell(a.trackDeg?.toStringAsFixed(0) ?? '')),
        DataCell(cell(a.verticalRateFpm?.toString() ?? '')),
        DataCell(cell(_agoLabel(a.icao))),
      ],
    );
  }

  Widget _buildAircraftPane() {
    return LayoutBuilder(
      builder: (context, constraints) {
        final maxTableWidth = math.max(_minPaneWidth, constraints.maxWidth - _minPaneWidth - 8);
        final tableWidth = _tableWidth.clamp(_minPaneWidth, maxTableWidth);
        return Row(
          children: [
            SizedBox(width: tableWidth, child: _buildAircraftTable()),
            MouseRegion(
              cursor: SystemMouseCursors.resizeColumn,
              child: GestureDetector(
                behavior: HitTestBehavior.translucent,
                onHorizontalDragUpdate: (details) {
                  setState(() {
                    _tableWidth = (tableWidth + details.delta.dx).clamp(_minPaneWidth, maxTableWidth);
                  });
                },
                child: const SizedBox(
                  width: 8,
                  child: Center(child: VerticalDivider(width: 1)),
                ),
              ),
            ),
            Expanded(child: _buildAircraftMap()),
          ],
        );
      },
    );
  }

  Widget _buildAircraftMap() {
    final positioned = _aircraft.values.where((a) => a.lat != null && a.lon != null).toList();
    return Stack(
      children: [
        FlutterMap(
          mapController: _mapController,
          options: const MapOptions(
            initialCenter: LatLng(20, 0),
            initialZoom: 2,
          ),
          children: [
            TileLayer(
              urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
              userAgentPackageName: 'com.example.adsb_ui',
            ),
            PolylineLayer(
              polylines: [
                for (final a in positioned)
                  if ((_aircraftTrails[a.icao]?.length ?? 0) > 1)
                    Polyline(
                      points: _aircraftTrails[a.icao]!,
                      strokeWidth: a.icao == _selectedIcao ? 3 : 2,
                      color: (_isStale(a) ? Colors.grey : Theme.of(context).colorScheme.primary)
                          .withValues(alpha: a.icao == _selectedIcao ? 0.9 : 0.5),
                    ),
              ],
            ),
            MarkerLayer(
              markers: [
                for (final a in positioned)
                  Marker(
                    point: LatLng(a.lat!, a.lon!),
                    width: 44,
                    height: 44,
                    child: _buildAircraftMarker(a),
                  ),
              ],
            ),
          ],
        ),
        Positioned(
          right: 12,
          bottom: 12,
          child: FloatingActionButton.small(
            tooltip: 'Center on aircraft',
            onPressed: positioned.isEmpty ? null : _recenterMap,
            child: const Icon(Icons.my_location),
          ),
        ),
      ],
    );
  }

  /// A track-oriented arrow for one aircraft, dimmed if stale and ringed if
  /// selected. Tapping it selects/deselects, mirroring the table row.
  Widget _buildAircraftMarker(AircraftRecord a) {
    final stale = _isStale(a);
    final selected = a.icao == _selectedIcao;
    final color = stale ? Colors.grey : Theme.of(context).colorScheme.primary;
    return GestureDetector(
      onTap: () => _toggleSelected(a.icao),
      child: Tooltip(
        message: [
          a.callsign ?? a.icao,
          if (a.altitudeFt != null) '${a.altitudeFt} ft',
          if (a.groundSpeedKt != null) '${a.groundSpeedKt!.toStringAsFixed(0)} kt',
          if (stale) 'stale (${(_latestStreamTimeS - a.lastSeenS).toStringAsFixed(0)}s ago)',
        ].join('\n'),
        child: Container(
          alignment: Alignment.center,
          decoration: selected
              ? BoxDecoration(
                  shape: BoxShape.circle,
                  color: color.withValues(alpha: 0.15),
                  border: Border.all(color: color, width: 2),
                )
              : null,
          child: Opacity(
            opacity: stale ? 0.5 : 1.0,
            child: Transform.rotate(
              angle: (a.trackDeg ?? 0) * math.pi / 180,
              child: Icon(Icons.navigation, color: color),
            ),
          ),
        ),
      ),
    );
  }

  void _recenterMap() {
    final positioned = _aircraft.values.where((a) => a.lat != null && a.lon != null).toList();
    if (positioned.isEmpty) return;
    final lat = positioned.map((a) => a.lat!).reduce((a, b) => a + b) / positioned.length;
    final lon = positioned.map((a) => a.lon!).reduce((a, b) => a + b) / positioned.length;
    _mapController.move(LatLng(lat, lon), _mapController.camera.zoom);
  }

  /// Scrolling waterfall of the raw pre-resample IQ spectrum -- lets you see
  /// at a glance whether the tuner is centered on 1090 MHz (the signal
  /// should sit on the dashed center line) and how noisy the band is outside
  /// the ADS-B channel itself. Rebuilds only on new spectrum data (via
  /// ValueListenableBuilder), independent of the once-a-second frame/
  /// aircraft flush.
  Widget _buildSpectrumPane() {
    return ValueListenableBuilder<List<SpectrumFrame>>(
      valueListenable: _spectrumHistory,
      builder: (context, rows, _) {
        if (rows.isEmpty) {
          return const Center(child: Text('No spectrum data yet'));
        }
        final latest = rows.last;
        final halfSpanMhz = latest.rateHz / 2 / 1e6;
        final centerMhz = latest.freqHz / 1e6;
        return Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Expanded(
                child: ClipRect(
                  child: CustomPaint(
                    size: Size.infinite,
                    painter: _WaterfallPainter(rows),
                  ),
                ),
              ),
              const SizedBox(height: 4),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text('${(centerMhz - halfSpanMhz).toStringAsFixed(3)} MHz'),
                  Text('${centerMhz.toStringAsFixed(3)} MHz (center)'),
                  Text('${(centerMhz + halfSpanMhz).toStringAsFixed(3)} MHz'),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Paints `rows` (oldest first) as a scrolling image: each row is one
/// horizontal line, newest at the top, colored by magnitude (dB, auto-scaled
/// per paint across all currently-buffered rows so a strong signal doesn't
/// permanently wash out the color range once it passes). A dashed line marks
/// the center frequency so an off-tuned signal is visually obvious.
class _WaterfallPainter extends CustomPainter {
  final List<SpectrumFrame> rows;

  _WaterfallPainter(this.rows);

  static const List<Color> _colormap = [
    Color(0xFF000030),
    Color(0xFF0000C0),
    Color(0xFF00C0C0),
    Color(0xFF00C000),
    Color(0xFFE0E000),
    Color(0xFFE00000),
  ];

  Color _colorFor(double t) {
    final scaled = t.clamp(0.0, 1.0) * (_colormap.length - 1);
    final i = scaled.floor().clamp(0, _colormap.length - 2);
    return Color.lerp(_colormap[i], _colormap[i + 1], scaled - i)!;
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (rows.isEmpty || size.height <= 0 || size.width <= 0) return;

    double minDb = double.infinity;
    double maxDb = -double.infinity;
    for (final r in rows) {
      for (final b in r.bins) {
        if (b < minDb) minDb = b;
        if (b > maxDb) maxDb = b;
      }
    }
    if (maxDb <= minDb) maxDb = minDb + 1;

    final rowHeight = size.height / _maxSpectrumRowsHint;
    for (int i = 0; i < rows.length; i++) {
      final bins = rows[i].bins;
      // rows[] is oldest-first; the newest row (last index) is drawn at the
      // top and each older one pushed further down, like a real waterfall
      // scrolling downward as time passes.
      final y = (rows.length - 1 - i) * rowHeight;
      if (y > size.height) continue;
      final n = bins.length;
      final stops = List<double>.generate(n, (j) => j / (n - 1));
      final colors = [for (final b in bins) _colorFor((b - minDb) / (maxDb - minDb))];
      final paint = Paint()
        ..shader = ui.Gradient.linear(Offset(0, y), Offset(size.width, y), colors, stops);
      canvas.drawRect(Rect.fromLTWH(0, y, size.width, rowHeight + 1), paint);
    }

    final centerPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.6)
      ..strokeWidth = 1;
    final centerX = size.width / 2;
    const dash = 6.0;
    for (double y = 0; y < size.height; y += dash * 2) {
      canvas.drawLine(Offset(centerX, y), Offset(centerX, math.min(y + dash, size.height)), centerPaint);
    }
  }

  // Rows scroll from the bottom up as the buffer fills, rather than
  // stretching to fill the pane immediately -- keeps each row's on-screen
  // height constant instead of changing as more history arrives.
  static const int _maxSpectrumRowsHint = _FrameTablePageState._maxSpectrumRows;

  @override
  bool shouldRepaint(covariant _WaterfallPainter oldDelegate) => !identical(oldDelegate.rows, rows);
}
