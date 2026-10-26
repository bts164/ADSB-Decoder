import 'dart:async';
import 'dart:collection';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';

// Prefixed: the generated Error would hide dart:core's.
import '../api/adsb/api/v1/api.pb.dart' as api;
import '../api/adsb/api/v1/api.wsrpc.dart';
import '../lazy_table.dart';
import '../models.dart';
import '../painters.dart';
import '../settings.dart';
import '../settings_panel.dart';
import '../wsrpc/channel.dart';

part 'aircraft_table.dart';
part 'capture.dart';
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
  WsRpcChannel? _channel;
  // A client for each service the server serves, while connected: Receiver with a live input, History with a
  // history db.
  ReceiverClient? _receiver;
  HistoryClient? _history;
  api.ReceiverInfo? _receiverInfo;
  // The live watches, in progress while connected to a server with Receiver.
  final List<StreamSubscription<Object>> _watches = [];
  // The spectrum call, in progress while the spectrum pane is shown and connected.
  StreamSubscription<api.SpectrumRow>? _spectrum;
  // Whether the last server connected to serves Receiver, and so the spectrum; the toggle is shown only if it does.
  bool _spectrumAvailable = false;
  // The settings as the settings watch last sent them, while connected: another client can change them.
  final _settings = ValueNotifier<api.Settings?>(null);
  // The capture call in progress, if any.
  final _capture = ValueNotifier<_CaptureInProgress?>(null);
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

  // History scrubbing, with the backend's --history-db. On connect a
  // snapshot of the recent sky backfills the live state and turns the
  // scrubber on; after that, each seek asks for one.
  bool _backfilled = false;
  // Span the scrubber covers (Unix s): the db's first update to the newest,
  // live ones included. Null without a history db.
  double? _historyStartS;
  double? _historyEndS;
  // Scrubber position, or null when live.
  double? _scrubTimeS;
  // The scrubber narrows down in three levels. The top strip spans the whole
  // history and moves the range: _scrubRangeS starting at _scrubRangeStartS,
  // or the newest _scrubRangeS when that's null (live, or never moved). The
  // middle strip spans the range and moves the window within it, likewise
  // _scrubWindowS from _scrubWindowStartS, which the fine slider covers.
  static const _scrubRangeChoicesS = [6 * 3600.0, 12 * 3600.0, 86400.0, 2 * 86400.0, 7 * 86400.0];
  double _scrubRangeS = 86400;
  double? _scrubRangeStartS;
  static const _scrubWindowChoicesS = [600.0, 1800.0, 3600.0, 3 * 3600.0, 12 * 3600.0];
  double _scrubWindowS = 1800;
  double? _scrubWindowStartS;
  // Distinct aircraft per _activityBucketS bucket, consecutive from
  // _activityT0 (Unix s), for the overview strips' histogram. Replaced, not
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

  // Spectrum rows arrive at most every 50 ms (kSpectrumInterval in
  // pipeline.cpp), so unlike frames/aircraft these go straight into their own
  // ValueNotifier on arrival instead of through the once-a-second
  // _flushUpdates batch: only the waterfall widget listening to it repaints,
  // not the whole page. 400 rows is 20 s of history.
  static const int _maxSpectrumRows = 400;
  final ValueNotifier<List<_WaterfallRow>> _spectrumHistory = ValueNotifier<List<_WaterfallRow>>([]);
  // The noise floor the rows are colored against, smoothed across rows so the
  // colors don't flicker; null until the first row.
  double? _spectrumFloorDb;
  // Rows are colored in arrival order, each after the one before, and dropped
  // once done if the history was cleared meanwhile (the generation changed).
  Future<void> _spectrumColoring = Future.value();
  int _spectrumGeneration = 0;
  // A recoloring of the history waiting its turn: changes while one waits join it, so a drag of the range
  // recolors as fast as recoloring goes and no faster.
  bool _spectrumRecolorQueued = false;

  // Display units and time format. The page's own defaults if it wasn't given any.
  late final DisplaySettings _units = widget.settings ?? DisplaySettings();
  // Rebuilt when the units change, for the headers.
  late List<TableColumn<AircraftRecord>> _aircraftColumns = _buildAircraftColumns();
  // The spectrum's color range, ahead of _units while the slider is dragged: saving it then would rebuild
  // the page on every step.
  late final ValueNotifier<RangeValues> _spectrumRange =
      ValueNotifier(RangeValues(_units.spectrumLowDb, _units.spectrumHighDb));

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
    for (final w in _watches) {
      w.cancel();
    }
    _spectrum?.cancel();
    _capture.value?.subscription.cancel();
    _channel?.close();
    _urlController.dispose();
    _searchController.dispose();
    // Not _clearSpectrum(): nothing is listening to be told.
    _spectrumGeneration++;
    for (final row in _spectrumHistory.value) {
      row.image.dispose();
    }
    _spectrumHistory.dispose();
    _spectrumRange.dispose();
    _framesChanged.dispose();
    _inputStatus.dispose();
    _capture.dispose();
    _settings.dispose();
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
    _scrubRangeStartS = null;
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
    _clearSpectrum();
    _framesChanged.notify();
    _aircraftChanged.notify();
    setState(() => _error = null);
    final WsRpcChannel channel;
    try {
      channel = await WsRpcChannel.connect(Uri.parse(url), [ReceiverClient.service, HistoryClient.service]);
    } catch (e) {
      setState(() {
        _error = '$e';
        _connected = false;
      });
      return;
    }
    _channel = channel;
    final receiver = channel.services.contains(ReceiverClient.service) ? ReceiverClient(channel) : null;
    final history = channel.services.contains(HistoryClient.service) ? HistoryClient(channel) : null;
    _receiver = receiver;
    _history = history;
    channel.done.then((_) {
      if (identical(channel, _channel)) _disconnect(error: 'the connection closed');
    });
    setState(() {
      _connected = true;
      _spectrumAvailable = receiver != null;
      if (receiver == null) _showSpectrum = false;
    });
    try {
      if (receiver != null) {
        _receiverInfo = await receiver.getReceiverInfo(api.GetReceiverInfoRequest());
        if (!identical(channel, _channel)) return;
        _watch(channel, receiver.watchSettings(api.WatchSettingsRequest()), (s) => _settings.value = s);
        _watch(channel, receiver.watchFrames(api.WatchFramesRequest()), _onFrames);
        _watch(channel, receiver.watchAircraft(api.WatchAircraftRequest()), _onAircraft);
        _watch(channel, receiver.watchInputStatus(api.WatchInputStatusRequest()),
            (s) => _inputStatus.value = InputStatus.fromProto(s));
        if (_showSpectrum) _startSpectrum();
      }
      // Watched first so nothing is missed in between. The live data waits in the buffers until the flush timer
      // starts, so it applies on top of the backfill.
      if (history != null) {
        _onSnapshot(AircraftSnapshot.fromProto(await history.getSnapshot(api.GetSnapshotRequest())));
      }
    } on RpcException catch (e) {
      if (identical(channel, _channel)) _disconnect(error: '$e');
      return;
    }
    if (!identical(channel, _channel)) return;
    _uiUpdateTimer = Timer.periodic(_uiUpdateInterval, (_) => _flushUpdates());
    _activityTimer = Timer.periodic(_activityPollInterval, (_) {
      _requestActivity();
      // A live heatmap grows with the history; a scrubbed one doesn't change.
      if (_snapshot == null) _requestHeatmap();
    });
  }

  /// A live watch for as long as the connection: the server ends one only when it fails, which disconnects.
  void _watch<T extends Object>(WsRpcChannel channel, Stream<T> watch, void Function(T reply) onReply) {
    void ended(String why) {
      if (identical(channel, _channel)) _disconnect(error: why);
    }

    _watches.add(watch.listen(
      onReply,
      onError: (Object e) => ended('$e'),
      onDone: () => ended('the server ended a watch'),
      cancelOnError: true,
    ));
  }

  /// Makes a history call on this connection. Null without a history db, if it failed, or if the connection
  /// has changed since: the channel reports a disconnect, and any other failure is printed here.
  Future<T?> _ask<T>(Future<T> Function(HistoryClient history) call) async {
    final history = _history;
    if (history == null) return null;
    try {
      final reply = await call(history);
      return identical(history, _history) ? reply : null;
    } on RpcException catch (e) {
      if (e.code != null) debugPrint('$e');
      return null;
    }
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

  // Frames/aircraft can arrive at rates far higher than the UI can (or
  // should) redraw at, so they are just buffered here -- _flushUpdates, on a
  // timer, is what actually triggers a rebuild.
  void _onFrames(api.FrameBatch batch) => _pendingFrames.addAll(batch.frames.map(FrameRecord.fromProto));

  void _onAircraft(api.AircraftChanges changes) {
    for (final a in changes.aircraft) {
      final ac = AircraftRecord.fromProto(a);
      _pendingAircraft[ac.icao] = ac;
    }
  }

  // The server computes the spectrum only while a spectrum call is in progress, so the call follows the pane.
  void _setShowSpectrum(bool show) {
    setState(() => _showSpectrum = show);
    show ? _startSpectrum() : _stopSpectrum();
  }

  void _startSpectrum() {
    final receiver = _receiver;
    if (receiver == null || _spectrum != null) return;
    _spectrum = receiver.watchSpectrum(api.WatchSpectrumRequest()).listen(
      _onSpectrumRow,
      // The channel reports a disconnect; any other failure is printed here.
      onError: (Object e) {
        if (e is RpcException && e.code != null) debugPrint('$e');
        _spectrum = null;
      },
      onDone: () => _spectrum = null,
      cancelOnError: true,
    );
  }

  void _stopSpectrum() {
    _spectrum?.cancel();
    _spectrum = null;
  }

  void _onSpectrumRow(api.SpectrumRow row) {
    if (row.binsDb.isEmpty) return;
    final median = _WaterfallRow.medianDb(row.binsDb);
    final floor = _spectrumFloorDb;
    final floorDb = _spectrumFloorDb = floor == null ? median : floor + 0.1 * (median - floor);
    final generation = _spectrumGeneration;
    _spectrumColoring = _spectrumColoring.then((_) async {
      final colored = await _WaterfallRow.fromProto(row, floorDb, _units.spectrumColormap, _spectrumRange.value);
      if (!mounted || generation != _spectrumGeneration) {
        colored.image.dispose();
        return;
      }
      final rows = [..._spectrumHistory.value, colored];
      if (rows.length > _maxSpectrumRows) {
        for (final old in rows.take(rows.length - _maxSpectrumRows)) {
          old.image.dispose();
        }
        rows.removeRange(0, rows.length - _maxSpectrumRows);
      }
      _spectrumHistory.value = rows;
    });
  }

  /// Recolors the history in the current colormap and range, after the rows already being colored.
  void _recolorSpectrum() {
    if (_spectrumRecolorQueued) return;
    _spectrumRecolorQueued = true;
    final generation = _spectrumGeneration;
    _spectrumColoring = _spectrumColoring.then((_) async {
      _spectrumRecolorQueued = false;
      final old = _spectrumHistory.value;
      final colormap = _units.spectrumColormap;
      final range = _spectrumRange.value;
      final rows = await Future.wait([for (final row in old) row.recolored(colormap, range)]);
      if (!mounted || generation != _spectrumGeneration) {
        for (final row in rows) {
          row.image.dispose();
        }
        return;
      }
      _spectrumHistory.value = rows;
      for (final row in old) {
        row.image.dispose();
      }
    });
  }

  void _clearSpectrum() {
    _spectrumGeneration++;
    _spectrumFloorDb = null;
    for (final row in _spectrumHistory.value) {
      row.image.dispose();
    }
    _spectrumHistory.value = [];
  }

  /// Applies a snapshot. The first, on connect, backfills the live aircraft
  /// and trails; the rest answer seeks and are shown while scrubbing.

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
    } else if (_scrubTimeS != null) {
      // Not after going back to live.
      _snapshot = s;
      _requestSelectedHistory();
      _scheduleHeatmap();
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
    for (final w in _watches) {
      w.cancel();
    }
    _watches.clear();
    _stopSpectrum();
    _dropCapture();
    _channel?.close();
    _settings.value = null;
    _channel = null;
    _receiver = null;
    _history = null;
    _receiverInfo = null;
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
            if (_spectrumAvailable) ...[
              const SizedBox(width: 6),
              _dockToggle(
                icon: Icons.waterfall_chart,
                label: 'Spectrum',
                selected: _showSpectrum,
                onSelected: _setShowSpectrum,
              ),
            ],
            if (_connected) _buildCaptureControl(),
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
                      onClose: () => _setShowSpectrum(false),
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
