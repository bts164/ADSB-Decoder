import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../lazy_table.dart';
import '../models.dart';
import '../painters.dart';
import '../settings.dart';
import '../settings_panel.dart';

part 'aircraft_table.dart';
part 'detail_panel.dart';
part 'frames.dart';
part 'heatmap.dart';
part 'history.dart';
part 'map.dart';
part 'spectrum.dart';
part 'stats.dart';

/// A Listenable notified by hand: the page notifies one of these when a piece of its state changes, and only
/// the widgets showing that piece (ListenableBuilders on it) rebuild, not the whole page.
class _Changed extends ChangeNotifier {
  void notify() => notifyListeners();
}

class FrameTablePage extends StatefulWidget {
  const FrameTablePage({super.key, this.settings});

  /// Units and time display; unsaved defaults if null.
  final DisplaySettings? settings;

  @override
  State<FrameTablePage> createState() => _FrameTablePageState();
}

class _FrameTablePageState extends State<FrameTablePage> {
  // 127.0.0.1, not "localhost": the backend's WsListener binds an IPv4
  // socket only, and "localhost" can resolve to ::1 first on Linux, which
  // fails to connect instead of falling back to the v4 address.
  final _urlController = TextEditingController(text: 'ws://127.0.0.1:8765');
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connected = false;
  String? _error;

  // Frames arrive far faster than the table can usefully redraw or the user
  // can read, so incoming messages are buffered here and only merged into
  // state on a timer -- see _flushUpdates. setState is only for the
  // connection controls; data changes notify the _Changed below, so each
  // flush rebuilds just the widgets showing what changed.
  static const int _maxFrames = 5000;
  static const Duration _uiUpdateInterval = Duration(seconds: 1);

  final Queue<FrameRecord> _frames = Queue<FrameRecord>();
  final List<FrameRecord> _pendingFrames = [];
  int _frameSortColumn = 0;
  bool _frameSortAscending = false;
  // _frames sorted for the table; null once stale (new frames or sort change).
  List<FrameRecord>? _sortedFramesCache;
  // While the frame table is paused, what it shows: _frames as of pausing.
  // Frames keep arriving into _frames and the stats meanwhile.
  List<FrameRecord>? _pausedFrames;
  int _framesSincePause = 0;
  // Frame log and stats.
  final _framesChanged = _Changed();

  // Cumulative counts across the whole connection, independent of the
  // _maxFrames display cap -- a frame that ages out of the table still
  // counts here. Rate is computed per flush interval only (no smoothing
  // across intervals), so it reflects exactly what arrived in that window.
  int _statsTotalFrames = 0;
  // Self-checking formats (DF11/17/18): a correct frame always passes, so this is the clean pass rate.
  int _statsSelfCheck = 0;
  int _statsSelfCheckOk = 0;
  // Frames that passed only after single-bit error correction (DF17/18); counted in _statsSelfCheckOk too.
  int _statsCrcFixed = 0;
  // Address/parity formats: pass only if the address was already confirmed, so a failure is either
  // corruption or an aircraft not yet heard on DF11/17/18 -- the two can't be told apart per frame.
  int _statsAddrParity = 0;
  int _statsAddrParityOk = 0;
  int _statsCrcUnchecked = 0;
  int _statsUnknownDf = 0;
  final Map<int, int> _statsByDf = {};
  double _statsFramesPerSec = 0;
  // Latest input_status; null before the first, and again once it's stale
  // (see _inputStatusMaxAge).
  final _inputStatus = ValueNotifier<InputStatus?>(null);
  static const _inputStatusMaxAge = Duration(seconds: 6);
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
  final Map<String, List<TrailPoint>> _aircraftTrails = {};
  int _aircraftSortColumn = 0;
  bool _aircraftSortAscending = true;
  List<AircraftRecord>? _sortedAircraftCache;
  // Whether the aircraft table and map show stale aircraft (greyed out) or hide them.
  bool _showStale = true;
  // Whether the table lists only aircraft with a position (the map only ever shows those).
  bool _positionOnly = false;
  // Aircraft search, lowercased; matched against the text columns.
  final _searchController = TextEditingController();
  String _search = '';
  // Aircraft, trails, selection and the table/map split.
  final _aircraftChanged = _Changed();
  // Every flush, for the "last seen" ago-labels, which advance even with no new data.
  final _clockTick = _Changed();

  // History scrubbing, with the backend's --history-db. On connect the
  // backend sends one snapshot of the recent sky, which backfills the live
  // state and turns the scrubber on; after that it sends one per seek.
  bool _backfilled = false;
  // Span the scrubber covers (Unix s): the db's first update to the newest,
  // live ones included. Null without a history db.
  double? _historyStartS;
  double? _historyEndS;
  // Scrubber position, or null when live.
  double? _scrubTimeS;
  // The fine slider covers _scrubWindowS starting at _scrubWindowStartS, or the
  // newest _scrubWindowS when that's null (live, or never moved). The overview
  // strip above it spans the whole history and moves the window.
  static const _scrubWindowChoicesS = [600.0, 1800.0, 3600.0, 3 * 3600.0, 12 * 3600.0];
  double _scrubWindowS = 1800;
  double? _scrubWindowStartS;
  // Distinct aircraft per _activityBucketS bucket, consecutive from
  // _activityT0 (Unix s), for the overview strip's histogram. Replaced, not
  // mutated, on each activity reply, so the painter can tell it changed.
  // Fetched whole after the backfill, then the tail every _activityPollInterval.
  static const _activityPollInterval = Duration(seconds: 30);
  List<int> _activity = const [];
  double? _activityT0;
  double _activityBucketS = 60;
  int _activityPeakCount = 0;
  bool _activityInFlight = false;
  Timer? _activityTimer;
  // The map's heatmap of where aircraft flew (see _requestHeatmap): how far back from the shown time it
  // looks, null when it's off and infinity for all history.
  double? _heatmapRangeS;
  static const _heatmapRangeChoicesS = [3600.0, 6 * 3600.0, 86400.0, 7 * 86400.0, double.infinity];
  // Grid cell size, in logical pixels of the map.
  static const _heatmapCellPx = 4.0;
  OverlayImage? _heatmapOverlay;
  // Flights per cell at the dark end of the heatmap's colours, for its legend.
  double _heatmapTop = 0;
  // One request in flight at a time; a view change meanwhile asks again when it lands.
  bool _heatmapInFlight = false;
  bool _heatmapQueued = false;
  // Waits for panning and zooming to settle before asking.
  Timer? _heatmapDebounce;
  // _mapController.camera throws until the map has been laid out.
  bool _mapReady = false;
  // Latest snapshot for _scrubTimeS, shown instead of the live aircraft while scrubbing.
  AircraftSnapshot? _snapshot;
  // One seek in flight at a time; the newest position asked for meanwhile waits here.
  bool _seekInFlight = false;
  double? _queuedSeekS;
  Timer? _uiUpdateTimer;

  // Aircraft older than this (relative to the newest last_seen_unix_s we've
  // received, or the scrubbed-to time) are shown greyed-out rather than
  // removed outright. Relative to the newest update rather than
  // DateTime.now(), since in file-playback mode the backend's times run
  // faster or slower than real time.
  static const double _staleAfterS = 30;
  double _latestTimeS = 0;

  // Wall-clock time each aircraft's state was last updated, for the "last
  // seen" table column -- shown as seconds-ago from the real current time,
  // not stream position, since that's what the person watching the table
  // actually wants to know.
  final Map<String, DateTime> _aircraftLastSeenWallClock = {};

  // Selecting a row in the aircraft table highlights the matching marker on
  // the map, and vice versa.
  String? _selectedIcao;
  // The selected aircraft's history for the detail panel: fetched from the
  // backend's history db on selection (and on each seek while scrubbing),
  // then extended with live updates. Oldest first.
  List<HistoryPoint> _selectedHistory = [];
  // The aircraft _selectedHistory is for.
  String? _selectedHistoryIcao;

  final _mapController = MapController();
  // Center the map on the first aircraft position we see, then leave the
  // user's pan/zoom alone -- re-centering on every update would fight
  // anyone trying to look around.
  bool _mapAutoCentered = false;

  // Fraction of the aircraft pane's height given to the map, above the
  // full-width table; dragged via the divider in _buildAircraftSplit.
  double _mapFraction = 0.5;
  static const double _minPaneHeight = 120;
  // Dock to the right of the map and table, for the frame log and the
  // spectrum waterfall, each shown on demand from the top bar (stacked when
  // both are). Its width is dragged from its left edge.
  bool _showFrames = false;
  bool _showSpectrum = false;
  double _dockWidth = 560;
  static const double _minDockWidth = 400;
  static const double _minMapWidth = 320;

  // Spectrum snapshots arrive far slower than frames (backend throttles to
  // one every 150ms regardless of sample rate -- see kSpectrumInterval in
  // main.cpp), so unlike frames/aircraft these go straight into their own
  // ValueNotifier on arrival instead of through the once-a-second
  // _flushUpdates batch: only the waterfall widget listening to it repaints,
  // not the whole page, and at ~7Hz that's cheap either way.
  static const int _maxSpectrumRows = 150;
  final ValueNotifier<List<SpectrumFrame>> _spectrumHistory = ValueNotifier<List<SpectrumFrame>>([]);

  // Display units and time format. The page's own defaults if it wasn't given any.
  late final DisplaySettings _units = widget.settings ?? DisplaySettings();
  // Rebuilt when the units change, for the headers.
  late List<TableColumn<AircraftRecord>> _aircraftColumns = _buildAircraftColumns();

  @override
  void initState() {
    super.initState();
    _units.addListener(_onUnitsChanged);
  }

  // Settings change rarely, so this rebuilds the whole page, ListenableBuilders included.
  void _onUnitsChanged() => setState(() => _aircraftColumns = _buildAircraftColumns());

  @override
  void dispose() {
    _units.removeListener(_onUnitsChanged);
    if (widget.settings == null) _units.dispose();
    _uiUpdateTimer?.cancel();
    _activityTimer?.cancel();
    _heatmapDebounce?.cancel();
    _sub?.cancel();
    _channel?.sink.close();
    _urlController.dispose();
    _searchController.dispose();
    _spectrumHistory.dispose();
    _framesChanged.dispose();
    _inputStatus.dispose();
    _aircraftChanged.dispose();
    _clockTick.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    final url = _urlController.text.trim();
    if (url.isEmpty) return;
    _uiUpdateTimer?.cancel();
    _frames.clear();
    _pendingFrames.clear();
    _sortedFramesCache = null;
    _pausedFrames = null;
    _aircraft.clear();
    _pendingAircraft.clear();
    _sortedAircraftCache = null;
    _aircraftTrails.clear();
    _backfilled = false;
    _historyStartS = null;
    _historyEndS = null;
    _scrubTimeS = null;
    _scrubWindowStartS = null;
    _activityTimer?.cancel();
    _activity = const [];
    _activityT0 = null;
    _activityPeakCount = 0;
    _activityInFlight = false;
    _heatmapOverlay = null;
    _heatmapInFlight = false;
    _heatmapQueued = false;
    _snapshot = null;
    _seekInFlight = false;
    _queuedSeekS = null;
    _mapAutoCentered = false;
    _latestTimeS = 0;
    _aircraftLastSeenWallClock.clear();
    _selectedIcao = null;
    _selectedHistory = [];
    _selectedHistoryIcao = null;
    _statsTotalFrames = 0;
    _statsSelfCheck = 0;
    _statsSelfCheckOk = 0;
    _statsCrcFixed = 0;
    _statsAddrParity = 0;
    _statsAddrParityOk = 0;
    _statsCrcUnchecked = 0;
    _statsUnknownDf = 0;
    _statsByDf.clear();
    _statsFramesPerSec = 0;
    _inputStatus.value = null;
    _statsWindowStart = DateTime.now();
    _spectrumHistory.value = [];
    _framesChanged.notify();
    _aircraftChanged.notify();
    setState(() => _error = null);
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
        // on a timer, is what actually triggers a rebuild.
        try {
          final json = jsonDecode(event as String) as Map<String, dynamic>;
          switch (json['type']) {
            case 'frame':
              _pendingFrames.add(FrameRecord.fromJson(json));
            case 'aircraft':
              final ac = AircraftRecord.fromJson(json);
              _pendingAircraft[ac.icao] = ac;
            case 'snapshot':
              _onSnapshot(AircraftSnapshot.fromJson(json));
            case 'aircraft_history':
              _onAircraftHistory(json);
            case 'heatmap':
              _onHeatmap(json);
            case 'input_status':
              _inputStatus.value = InputStatus.fromJson(json);
            case 'activity':
              _onActivity(json);
            case 'spectrum':
              final rows = [..._spectrumHistory.value, SpectrumFrame.fromJson(json)];
              if (rows.length > _maxSpectrumRows) {
                rows.removeRange(0, rows.length - _maxSpectrumRows);
              }
              _spectrumHistory.value = rows;
          }
        } catch (e, st) {
          // Skip the message rather than tearing down the connection, but say
          // why: a handler that throws here fails silently otherwise.
          debugPrint('dropped ws message: $e\n$st');
        }
      },
      onError: (e) => _disconnect(error: '$e'),
      onDone: _disconnect,
      cancelOnError: true,
    );
    _uiUpdateTimer = Timer.periodic(_uiUpdateInterval, (_) => _flushUpdates());
    _activityTimer = Timer.periodic(_activityPollInterval, (_) {
      _requestActivity();
      // A live heatmap grows with the history; a scrubbed one doesn't change.
      if (_snapshot == null) _requestHeatmap();
    });
    setState(() => _connected = true);
  }

  /// Merges buffered frames/aircraft into the displayed state, trims the
  /// frame log to _maxFrames (dropping the oldest), runs once-only map
  /// auto-centering off the batch's first new position, and notifies whatever
  /// changed.
  void _flushUpdates() {
    final now = DateTime.now();
    // The backend only reports while packets arrive, so an old status says
    // nothing about now.
    if (_inputStatus.value != null && now.difference(_inputStatus.value!.received) > _inputStatusMaxAge) {
      _inputStatus.value = null;
    }
    final elapsedS = now.difference(_statsWindowStart ?? now).inMicroseconds / 1e6;
    final framesPerSec = elapsedS > 0 ? _pendingFrames.length / elapsedS : 0.0;
    _statsWindowStart = now;

    if (_pendingFrames.isNotEmpty || framesPerSec != _statsFramesPerSec) {
      _statsFramesPerSec = framesPerSec;
      for (final f in _pendingFrames) {
        _statsTotalFrames++;
        if (selfCheckDfs.contains(f.df)) {
          _statsSelfCheck++;
          if (f.crcOk) _statsSelfCheckOk++;
          if (f.crcFixedBit != null) _statsCrcFixed++;
        } else if (addrParityDfs.contains(f.df) || f.df >= 24) {
          _statsAddrParity++;
          if (f.crcOk) _statsAddrParityOk++;
        } else if (!f.crcChecked) {
          _statsCrcUnchecked++;
        }
        if (!knownDfValues.contains(f.df)) _statsUnknownDf++;
        _statsByDf[f.df] = (_statsByDf[f.df] ?? 0) + 1;
      }

      _frames.addAll(_pendingFrames);
      while (_frames.length > _maxFrames) {
        _frames.removeFirst();
      }
      if (_pausedFrames == null) {
        _sortedFramesCache = null;
      } else {
        _framesSincePause += _pendingFrames.length;
      }
      _pendingFrames.clear();
      _framesChanged.notify();
    }

    if (_pendingAircraft.isNotEmpty) {
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
        if (ac.lastSeenUnixS > _latestTimeS) _latestTimeS = ac.lastSeenUnixS;
        _aircraftLastSeenWallClock[ac.icao] = now;
        if (ac.icao == _selectedIcao && _scrubTimeS == null) _appendHistory(historyPointOf(ac));
        if (ac.lat != null && ac.lon != null) {
          final trail = _aircraftTrails.putIfAbsent(ac.icao, () => []);
          trail.add((pos: LatLng(ac.lat!, ac.lon!), altFt: ac.altitudeFt));
          if (trail.length > _maxTrailPoints) {
            trail.removeRange(0, trail.length - _maxTrailPoints);
          }
        }
      }
      _pendingAircraft.clear();
      _sortedAircraftCache = null;
      if (_backfilled) {
        _historyStartS ??= _latestTimeS;
        _historyEndS = math.max(_historyEndS ?? _latestTimeS, _latestTimeS);
      }

      if (firstNewPosition != null) {
        _mapAutoCentered = true;
        final pos = firstNewPosition;
        WidgetsBinding.instance.addPostFrameCallback((_) {
          try {
            _mapController.move(pos, 9);
          } catch (_) {
            // Map isn't mounted yet -- it'll just show the default view.
          }
        });
      }
      _aircraftChanged.notify();
    }

    _clockTick.notify();
  }

  // What the map and aircraft table show: the scrubbed-to snapshot, or live.
  Map<String, AircraftRecord> get _shownAircraft => _snapshot?.aircraft ?? _aircraft;
  Map<String, List<TrailPoint>> get _shownTrails => _snapshot?.trails ?? _aircraftTrails;
  double get _shownTimeS => _snapshot?.timeS ?? _latestTimeS;

  bool _isStale(AircraftRecord a) => _shownTimeS - a.lastSeenUnixS > _staleAfterS;

  // _shownAircraft through the bottom bar's filters, for the table and map.
  Iterable<AircraftRecord> get _visibleAircraft => _shownAircraft.values.where(
        (a) => (_showStale || !_isStale(a)) && (!_positionOnly || a.lat != null) && _matchesSearch(a),
      );

  bool _matchesSearch(AircraftRecord a) =>
      _search.isEmpty ||
      [a.icao, a.callsign, a.squawk, a.category, categoryNames[a.category]]
          .any((field) => field != null && field.toLowerCase().contains(_search));

  void _onAircraftFilterChanged() {
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  /// Live, time since the update arrived; scrubbing, time before the scrubbed-to time.
  double? _ageS(AircraftRecord a) {
    final snapshot = _snapshot;
    if (snapshot != null) return snapshot.timeS - a.lastSeenUnixS;
    final t = _aircraftLastSeenWallClock[a.icao];
    if (t == null) return null;
    return DateTime.now().difference(t).inMilliseconds / 1000;
  }

  /// Applies a snapshot from the backend. The first, sent on connect before
  /// any live message, backfills the live aircraft and trails; the rest
  /// answer seeks and are shown while scrubbing.
  void _onSnapshot(AircraftSnapshot s) {
    if (s.startS != null) {
      _historyStartS = math.min(_historyStartS ?? s.startS!, s.startS!);
      _historyEndS = math.max(_historyEndS ?? s.endS!, s.endS!);
    }
    if (!_backfilled) {
      _backfilled = true;
      _requestActivity();
      _scheduleHeatmap();
      final now = DateTime.now();
      _aircraft.addAll(s.aircraft);
      _aircraftTrails.addAll(s.trails);
      for (final a in s.aircraft.values) {
        _aircraftLastSeenWallClock[a.icao] =
            now.subtract(Duration(milliseconds: ((s.timeS - a.lastSeenUnixS) * 1000).round()));
        _latestTimeS = math.max(_latestTimeS, a.lastSeenUnixS);
      }
    } else {
      _seekInFlight = false;
      // Ignore a reply that lands after going back to live.
      if (_scrubTimeS != null) {
        _snapshot = s;
        _requestSelectedHistory();
        _scheduleHeatmap();
      }
      final queued = _queuedSeekS;
      _queuedSeekS = null;
      if (queued != null) _seek(queued);
    }
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  /// Closes the connection and stops the flush timer, after flushing what's
  /// still buffered. Also where a dropped connection ends up, so the timer
  /// never outlives it.
  void _disconnect({String? error}) {
    if (_uiUpdateTimer != null) _flushUpdates();
    _uiUpdateTimer?.cancel();
    _uiUpdateTimer = null;
    _activityTimer?.cancel();
    _activityTimer = null;
    _sub?.cancel();
    _channel?.sink.close();
    _sub = null;
    _channel = null;
    _seekInFlight = false;
    _queuedSeekS = null;
    _heatmapInFlight = false;
    _heatmapQueued = false;
    setState(() {
      _connected = false;
      if (error != null) _error = error;
    });
  }

  void _onFrameSort(int column, bool ascending) {
    _frameSortColumn = column;
    _frameSortAscending = ascending;
    _sortedFramesCache = null;
    _framesChanged.notify();
  }

  void _onAircraftSort(int column, bool ascending) {
    _aircraftSortColumn = column;
    _aircraftSortAscending = ascending;
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  static List<T> _sortedBy<T>(Iterable<T> rows, TableColumn<T> column, bool ascending) {
    final cmp = column.compare!;
    return rows.toList()..sort(ascending ? cmp : (a, b) => cmp(b, a));
  }

  List<FrameRecord> get _sortedFrames => _sortedFramesCache ??=
      _sortedBy(_pausedFrames ?? _frames, _frameColumns[_frameSortColumn], _frameSortAscending);

  void _toggleFramesPaused() {
    _pausedFrames = _pausedFrames == null ? _frames.toList() : null;
    _framesSincePause = 0;
    _sortedFramesCache = null;
    _framesChanged.notify();
  }

  // Staleness is relative to the newest update, so it only changes when the
  // aircraft do, which already invalidates this cache.
  List<AircraftRecord> get _sortedAircraft => _sortedAircraftCache ??= _sortedBy(
    _visibleAircraft,
    _aircraftColumns[_aircraftSortColumn],
    _aircraftSortAscending,
  );

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      endDrawer: SettingsPanel(settings: _units),
      body: Column(
        children: [
          _buildTopBar(),
          const Divider(height: 1),
          Expanded(
            child: ListenableBuilder(listenable: _aircraftChanged, builder: (context, _) => _buildAircraftPane()),
          ),
        ],
      ),
    );
  }

  /// One compact bar in place of an app bar: title, aircraft count, toggles
  /// for the frames and spectrum dock, a one-line stats summary (click for
  /// the details), the connection controls, and the settings gear.
  Widget _buildTopBar() {
    final theme = Theme.of(context);
    return Material(
      color: theme.colorScheme.surfaceContainer,
      child: SizedBox(
        height: 48,
        child: Row(
          children: [
            const SizedBox(width: 16),
            Text('ADS-B', style: theme.textTheme.titleMedium),
            const SizedBox(width: 8),
            const SizedBox(width: 8),
            ListenableBuilder(
              listenable: _aircraftChanged,
              builder: (context, _) => Text('${_shownAircraft.length} aircraft'),
            ),
            const SizedBox(width: 16),
            ListenableBuilder(
              listenable: _framesChanged,
              builder: (context, _) => _dockToggle(
                icon: Icons.list_alt,
                label: 'Frames (${_frames.length})',
                selected: _showFrames,
                onSelected: (v) => setState(() => _showFrames = v),
              ),
            ),
            const SizedBox(width: 6),
            _dockToggle(
              icon: Icons.waterfall_chart,
              label: 'Spectrum',
              selected: _showSpectrum,
              onSelected: (v) => setState(() => _showSpectrum = v),
            ),
            const SizedBox(width: 12),
            ValueListenableBuilder(
              valueListenable: _inputStatus,
              builder: (context, status, _) => status == null ? const SizedBox.shrink() : _buildInputStatus(status),
            ),
            Expanded(
              child: ListenableBuilder(
                listenable: _framesChanged,
                builder: (context, _) => _statsTotalFrames > 0 ? _buildStatsSummary() : const SizedBox.shrink(),
              ),
            ),
            const SizedBox(width: 8),
            _buildConnectionControls(),
            const SizedBox(width: 4),
            // A Builder for a context under the Scaffold, to find it from.
            Builder(
              builder: (context) => IconButton(
                icon: const Icon(Icons.settings),
                tooltip: 'Settings',
                onPressed: () => Scaffold.of(context).openEndDrawer(),
              ),
            ),
            const SizedBox(width: 4),
          ],
        ),
      ),
    );
  }

  /// Status dot, URL field and connect button; a connection error shows as
  /// an icon whose tooltip has the message.
  Widget _buildConnectionControls() {
    final theme = Theme.of(context);
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (_error != null) ...[
          Tooltip(message: _error!, child: Icon(Icons.error_outline, color: theme.colorScheme.error, size: 20)),
          const SizedBox(width: 8),
        ],
        Tooltip(
          message: _connected ? 'Connected' : 'Disconnected',
          child: Icon(Icons.circle, size: 10, color: _connected ? Colors.green : Colors.grey),
        ),
        const SizedBox(width: 8),
        SizedBox(
          width: 240,
          child: TextField(
            controller: _urlController,
            enabled: !_connected,
            style: theme.textTheme.bodyMedium,
            decoration: const InputDecoration(
              hintText: 'WebSocket URL',
              isDense: true,
              contentPadding: EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              border: OutlineInputBorder(),
            ),
          ),
        ),
        const SizedBox(width: 8),
        FilledButton.tonal(
          onPressed: _connected ? _disconnect : _connect,
          style: FilledButton.styleFrom(visualDensity: VisualDensity.compact),
          child: Text(_connected ? 'Disconnect' : 'Connect'),
        ),
      ],
    );
  }

  Widget _dockToggle({
    required IconData icon,
    required String label,
    required bool selected,
    required ValueChanged<bool> onSelected,
  }) {
    return FilterChip(
      avatar: Icon(icon, size: 16),
      label: Text(label),
      selected: selected,
      showCheckmark: false,
      visualDensity: VisualDensity.compact,
      tooltip: selected ? 'Hide $label' : 'Show $label',
      onSelected: onSelected,
    );
  }

  /// The dock beside the map and table: whichever of the frame log and
  /// spectrum are shown, stacked, behind a drag handle that sets its width.
  Widget _buildDock(double totalWidth) {
    final maxWidth = math.max(_minDockWidth, totalWidth - _minMapWidth);
    final width = _dockWidth.clamp(_minDockWidth, maxWidth);
    return SizedBox(
      key: const ValueKey('dock'),
      width: width,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          MouseRegion(
            cursor: SystemMouseCursors.resizeColumn,
            child: GestureDetector(
              behavior: HitTestBehavior.translucent,
              onHorizontalDragUpdate: (details) {
                // From the state, not the captured width, as in _buildAircraftSplit.
                final current = _dockWidth.clamp(_minDockWidth, maxWidth);
                _dockWidth = (current - details.delta.dx).clamp(_minDockWidth, maxWidth);
                _aircraftChanged.notify();
              },
              child: const SizedBox(width: 8, child: Center(child: VerticalDivider(width: 1))),
            ),
          ),
          Expanded(
            child: Column(
              children: [
                if (_showFrames)
                  Expanded(
                    flex: 3,
                    child: _buildDockSection(
                      title: ListenableBuilder(
                        listenable: _framesChanged,
                        builder: (context, _) => Text('Frames (${_frames.length})'),
                      ),
                      onClose: () => setState(() => _showFrames = false),
                      child: ListenableBuilder(listenable: _framesChanged, builder: (context, _) => _buildFramesPane()),
                    ),
                  ),
                if (_showFrames && _showSpectrum) const Divider(height: 1),
                if (_showSpectrum)
                  Expanded(
                    flex: 2,
                    child: _buildDockSection(
                      title: const Text('Spectrum'),
                      onClose: () => setState(() => _showSpectrum = false),
                      child: _buildSpectrumPane(),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDockSection({required Widget title, required VoidCallback onClose, required Widget child}) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Material(
          color: theme.colorScheme.surfaceContainerLow,
          child: Padding(
            padding: const EdgeInsets.only(left: 12),
            child: Row(
              children: [
                DefaultTextStyle(style: theme.textTheme.titleSmall!, child: title),
                const Spacer(),
                IconButton(
                  icon: const Icon(Icons.close, size: 18),
                  tooltip: 'Close',
                  visualDensity: VisualDensity.compact,
                  onPressed: onClose,
                ),
              ],
            ),
          ),
        ),
        Expanded(child: child),
      ],
    );
  }
}
