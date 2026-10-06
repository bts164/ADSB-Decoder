part of 'frame_table_page.dart';

const double _historyWindowS = 1800;

String _windowLabel(double s) => s >= 86400
    ? '${(s / 86400).round()} d'
    : s >= 3600
    ? '${(s / 3600).round()} h'
    : '${(s / 60).round()} min';

/// Time travel through the history db: the scrubber bar and its activity
/// strip, and the selected aircraft's history.
extension _History on _FrameTablePageState {
  /// Asks for the activity histogram: all of it at first, then from the
  /// last bucket (which may have been still filling) on. Needs a history db.
  void _requestActivity() {
    if (!_backfilled || _historyStartS == null || _activityInFlight) return;
    _activityInFlight = true;
    final t0 = _activityT0;
    final from = t0 == null ? null : t0 + math.max(0, _activity.length - 1) * _activityBucketS;
    _channel?.sink.add(jsonEncode({'type': 'activity', 'from': ?from}));
  }

  /// Merges an activity reply: its counts replace ours from its t0 on.
  void _onActivity(Map<String, dynamic> j) {
    _activityInFlight = false;
    final t0 = (j['t0'] as num?)?.toDouble();
    if (t0 == null) return;
    final bucketS = (j['bucket_s'] as num).toDouble();
    final counts = [for (final c in j['counts'] as List) (c as num).toInt()];
    final oldT0 = _activityT0;
    final List<int> merged;
    if (oldT0 == null || bucketS != _activityBucketS || t0 < oldT0) {
      merged = counts;
      _activityT0 = t0;
    } else {
      final keep = ((t0 - oldT0) / bucketS).round();
      merged = [
        ..._activity.take(keep),
        ...List.filled(math.max(0, keep - _activity.length), 0),
        ...counts,
      ];
    }
    _activityBucketS = bucketS;
    _activity = merged;
    _activityPeakCount = merged.fold(0, math.max);
    _aircraftChanged.notify();
  }

  void _seek(double t) {
    if (_seekInFlight) {
      _queuedSeekS = t;
      return;
    }
    _seekInFlight = true;
    _channel?.sink.add(jsonEncode({'type': 'seek', 't': t}));
  }

  void _onScrub(double t) {
    _scrubTimeS = t;
    _seek(t);
    _aircraftChanged.notify();
  }

  /// The middle strip's range within [start, end]: _scrubRangeS long (or all
  /// of the history when shorter), from _scrubRangeStartS or ending at end.
  (double, double) _scrubRange(double start, double end) {
    final length = math.min(_scrubRangeS, end - start);
    final lo = (_scrubRangeStartS ?? end - length).clamp(start, end - length);
    return (lo, lo + length);
  }

  /// The fine slider's window within the range [rangeLo, rangeHi]:
  /// _scrubWindowS long (or all of the range when shorter), from
  /// _scrubWindowStartS or ending at rangeHi.
  (double, double) _scrubWindow(double rangeLo, double rangeHi) {
    final length = math.min(_scrubWindowS, rangeHi - rangeLo);
    final lo = (_scrubWindowStartS ?? rangeHi - length).clamp(rangeLo, rangeHi - length);
    return (lo, lo + length);
  }

  /// Jumps to `t` from a strip, ◀/▶ or the time picker: centres the window on
  /// it and scrubs there. The range is centred on it too with `centreRange`
  /// (the top strip), and otherwise moves just far enough to hold the window.
  /// At or past the end, goes live.
  void _jumpTo(double t, {bool centreRange = false}) {
    final start = _historyStartS;
    final end = _historyEndS;
    if (start == null || end == null) return;
    if (t >= end) {
      _goLive();
      return;
    }
    t = math.max(t, start);
    final (rangeLo, rangeHi) = _scrubRange(start, end);
    final rangeLength = rangeHi - rangeLo;
    final half = math.min(_scrubWindowS, rangeLength) / 2;
    _scrubRangeStartS = centreRange ? t - rangeLength / 2 : rangeLo.clamp(t + half - rangeLength, t - half);
    _scrubWindowStartS = t - half;
    _onScrub(t);
  }

  Future<void> _pickScrubTime() async {
    final start = _historyStartS;
    final end = _historyEndS;
    if (start == null || end == null) return;
    // The pickers deal in plain dates and times of day; these are in the display time zone.
    final current = _units.toDisplayTime(_scrubTimeS ?? end);
    final first = _units.toDisplayTime(start);
    final last = _units.toDisplayTime(end);
    final date = await showDatePicker(
      context: context,
      initialDate: DateTime(current.year, current.month, current.day),
      firstDate: DateTime(first.year, first.month, first.day),
      lastDate: DateTime(last.year, last.month, last.day),
    );
    if (date == null || !mounted) return;
    final time = await showTimePicker(
      context: context,
      initialTime: TimeOfDay(hour: current.hour, minute: current.minute),
      helpText: _units.timeZone == TimeZoneMode.utc ? 'Select time (UTC)' : null,
      builder: (context, child) => MediaQuery(
        data: MediaQuery.of(context).copyWith(alwaysUse24HourFormat: _units.clock == ClockFormat.h24),
        child: child!,
      ),
    );
    if (time == null) return;
    _jumpTo(_units.fromDisplayTime(date.year, date.month, date.day, time.hour, time.minute));
  }

  void _goLive() {
    _scrubRangeStartS = null;
    _scrubWindowStartS = null;
    _scrubTimeS = null;
    _snapshot = null;
    _requestSelectedHistory();
    _scheduleHeatmap();
    _queuedSeekS = null;
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  void _toggleSelected(String icao) {
    _selectedIcao = _selectedIcao == icao ? null : icao;
    _requestSelectedHistory();
    _aircraftChanged.notify();
  }

  /// Asks the backend for the selected aircraft's history up to the shown
  /// time. A newly selected aircraft's history restarts from what's shown
  /// now; otherwise (scrubbing, going live) the current charts stay up until
  /// the reply replaces them, so they don't flash empty while dragging.
  /// Without a history db there's no reply and it builds up from live
  /// updates alone.
  void _requestSelectedHistory() {
    final icao = _selectedIcao;
    if (icao != _selectedHistoryIcao) {
      final a = icao == null ? null : _shownAircraft[icao];
      _selectedHistory = a == null ? [] : [historyPointOf(a)];
      _selectedHistoryIcao = icao;
    }
    if (icao == null || !_backfilled) return;
    _channel?.sink.add(jsonEncode({'type': 'aircraft_history', 'icao': icao, 't': ?_snapshot?.timeS}));
  }

  /// A reply to _requestSelectedHistory: its points, then (when live) any
  /// live ones that arrived after it. Replies for an earlier selection are
  /// dropped.
  void _onAircraftHistory(Map<String, dynamic> j) {
    if (j['icao'] != _selectedIcao) return;
    final points = <HistoryPoint>[
      for (final p in j['points'] as List)
        (
          t: (p[0] as num).toDouble(),
          altFt: (p[1] as num?)?.round(),
          gsKt: (p[2] as num?)?.toDouble(),
          vrFpm: (p[3] as num?)?.round(),
          signalDb: (p[4] as num?)?.toDouble(),
          selAltFt: (p[5] as num?)?.round(),
        ),
    ];
    final last = points.isEmpty ? double.negativeInfinity : points.last.t;
    _selectedHistory = [
      ...points,
      // While scrubbing, anything after the reply is from the previous position.
      if (_scrubTimeS == null) ..._selectedHistory.where((p) => p.t > last),
    ];
    _aircraftChanged.notify();
  }

  void _appendHistory(HistoryPoint p) {
    if (_selectedHistory.isNotEmpty && p.t <= _selectedHistory.last.t) return;
    _selectedHistory.add(p);
    // Same window as the backend's kHistoryAircraftWindowS.
    final cutoff = p.t - _historyWindowS;
    final firstKept = _selectedHistory.indexWhere((q) => q.t > cutoff);
    if (firstKept > 0) _selectedHistory.removeRange(0, firstKept);
  }

  /// Bar under the table and map, for what both show: whether stale aircraft
  /// are shown, and, with a history db, the time scrubber. Dragging the
  /// scrubber shows the aircraft and trails as of that time; Live, or letting
  /// go at the right end, goes back to live.
  Widget _buildAircraftControls() {
    final staleCount = _shownAircraft.values.where(_isStale).length;
    final start = _historyStartS;
    final end = math.max(_historyEndS ?? start ?? 0, start ?? 0);
    final live = _scrubTimeS == null;
    final value = start == null ? 0.0 : (_scrubTimeS ?? end).clamp(start, end);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: Row(
        children: [
          SizedBox(
            width: 200,
            child: TextField(
              controller: _searchController,
              decoration: InputDecoration(
                isDense: true,
                border: const OutlineInputBorder(),
                prefixIcon: const Icon(Icons.search, size: 18),
                hintText: 'icao, callsign, squawk, type',
                suffixIcon: _search.isEmpty
                    ? null
                    : IconButton(
                        icon: const Icon(Icons.clear, size: 18),
                        tooltip: 'Clear',
                        onPressed: () {
                          _searchController.clear();
                          _search = '';
                          _onAircraftFilterChanged();
                        },
                      ),
              ),
              onChanged: (text) {
                _search = text.trim().toLowerCase();
                _onAircraftFilterChanged();
              },
            ),
          ),
          const SizedBox(width: 8),
          Tooltip(
            message: 'Aircraft not seen for ${_FrameTablePageState._staleAfterS.toStringAsFixed(0)}s',
            child: FilterChip(
              label: Text('Stale ($staleCount)'),
              selected: _showStale,
              onSelected: (v) {
                _showStale = v;
                _onAircraftFilterChanged();
              },
            ),
          ),
          const SizedBox(width: 8),
          Tooltip(
            message: 'Only aircraft with a position',
            child: FilterChip(
              label: const Text('Has position'),
              selected: _positionOnly,
              onSelected: (v) {
                _positionOnly = v;
                _onAircraftFilterChanged();
              },
            ),
          ),
          if (start != null) ...[
            const SizedBox(width: 16),
            SizedBox(
              width: _units.clock == ClockFormat.h12 ? 210 : 180,
              child: Tooltip(
                message: 'Jump to a date and time${_units.timeZone == TimeZoneMode.utc ? ' (UTC)' : ''}',
                child: TextButton(
                  onPressed: _connected && end > start ? _pickScrubTime : null,
                  child: Text(live ? 'live' : _units.formatDateTime(value), style: const TextStyle(fontFamily: 'monospace')),
                ),
              ),
            ),
            Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                _buildSpanMenu(
                  tooltip: 'Middle strip span',
                  value: _scrubRangeS,
                  choices: _FrameTablePageState._scrubRangeChoicesS,
                  onSelected: (s) {
                    _scrubRangeS = s;
                    _recentreOnScrubTime();
                  },
                ),
                const SizedBox(height: 4),
                _buildSpanMenu(
                  tooltip: 'Slider span',
                  value: _scrubWindowS,
                  choices: _FrameTablePageState._scrubWindowChoicesS,
                  onSelected: (s) {
                    _scrubWindowS = s;
                    _recentreOnScrubTime();
                  },
                ),
              ],
            ),
            IconButton(
              icon: const Icon(Icons.chevron_left),
              tooltip: 'Back ${_windowLabel(_scrubWindowS)}',
              onPressed: _connected && value > start ? () => _jumpTo(value - _scrubWindowS) : null,
            ),
            Expanded(child: _buildScrubber(start, end, value)),
            IconButton(
              icon: const Icon(Icons.chevron_right),
              tooltip: 'Forward ${_windowLabel(_scrubWindowS)}',
              onPressed: _connected && !live ? () => _jumpTo(value + _scrubWindowS) : null,
            ),
            FilledButton.tonal(onPressed: live ? null : _goLive, child: const Text('Live')),
          ],
        ],
      ),
    );
  }

  /// After a span change: recentres the range and the window on the scrub
  /// position, so it stays within both. Live, they keep following the end.
  void _recentreOnScrubTime() {
    final t = _scrubTimeS;
    if (t != null) {
      _scrubRangeStartS = t - _scrubRangeS / 2;
      _scrubWindowStartS = t - _scrubWindowS / 2;
    }
    _aircraftChanged.notify();
  }

  Widget _buildSpanMenu({
    required String tooltip,
    required double value,
    required List<double> choices,
    required void Function(double) onSelected,
  }) {
    return PopupMenuButton<double>(
      tooltip: tooltip,
      initialValue: value,
      onSelected: onSelected,
      itemBuilder: (context) => [for (final s in choices) PopupMenuItem(value: s, child: Text(_windowLabel(s)))],
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [Text(_windowLabel(value)), const Icon(Icons.arrow_drop_down, size: 18)],
        ),
      ),
    );
  }

  /// Three-level scrubber. The top strip is the whole history, where a click
  /// or drag jumps and brings the range along; the middle strip is that
  /// range, where one jumps within it, moving the window; under them a fine
  /// slider covers just the window.
  Widget _buildScrubber(double start, double end, double value) {
    final (rangeLo, rangeHi) = _scrubRange(start, end);
    final (lo, hi) = _scrubWindow(rangeLo, rangeHi);
    final enabled = _connected && end > start;
    final utc = _units.timeZone == TimeZoneMode.utc ? ' UTC' : '';

    // One strip over [from, to], marking [markLo, markHi] and the scrub position.
    Widget strip(double from, double to, double markLo, double markHi, String hint, {required bool centreRange}) {
      return LayoutBuilder(
        builder: (context, constraints) {
          // Inset to line up with the slider's track.
          const inset = 24.0;
          final width = math.max(1.0, constraints.maxWidth - 2 * inset);
          void jump(Offset local) {
            if (!enabled) return;
            _jumpTo(from + ((local.dx - inset) / width).clamp(0.0, 1.0) * (to - from), centreRange: centreRange);
          }

          return Tooltip(
            message: '${_units.formatDateTime(from)} – ${_units.formatDateTime(to)}$utc\n$hint',
            waitDuration: const Duration(milliseconds: 600),
            child: GestureDetector(
              onTapDown: (d) => jump(d.localPosition),
              onHorizontalDragUpdate: (d) => jump(d.localPosition),
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: inset),
                child: CustomPaint(
                  size: const Size(double.infinity, 20),
                  painter: TimelinePainter(
                    startS: from,
                    endS: to,
                    windowStartS: markLo,
                    windowEndS: markHi,
                    positionS: value,
                    activity: _activity,
                    activityT0: _activityT0,
                    activityBucketS: _activityBucketS,
                    color: Theme.of(context).colorScheme.primary,
                    trackColor: Theme.of(context).colorScheme.outlineVariant,
                    utcOffsetS: _units.utcOffsetS(from),
                  ),
                ),
              ),
            ),
          );
        },
      );
    }

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        strip(
          start,
          end,
          rangeLo,
          rangeHi,
          'Shading: aircraft seen per minute (peak $_activityPeakCount)\n'
          'Click or drag to jump; the box is the strip below',
          centreRange: true,
        ),
        const SizedBox(height: 4),
        strip(rangeLo, rangeHi, lo, hi, 'Click or drag to jump; the box is the slider', centreRange: false),
        SizedBox(
          height: 32,
          child: Slider(
            min: lo,
            max: hi,
            value: value.clamp(lo, hi),
            onChanged: enabled && hi > lo
                ? (t) {
                    // Pin the range and window, so they stop following the live end while dragging.
                    _scrubRangeStartS ??= rangeLo;
                    _scrubWindowStartS ??= lo;
                    _onScrub(t);
                  }
                : null,
            onChangeEnd: (t) {
              if (t >= end) _goLive();
            },
          ),
        ),
      ],
    );
  }
}
