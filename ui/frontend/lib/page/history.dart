part of 'frame_table_page.dart';

const double _historyWindowS = 1800;

String _windowLabel(double s) => s >= 3600 ? '${(s / 3600).round()} h' : '${(s / 60).round()} min';

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

  /// The fine slider's range within [start, end]: _scrubWindowS long (or all
  /// of the history when shorter), from _scrubWindowStartS or ending at end.
  (double, double) _scrubWindow(double start, double end) {
    final length = math.min(_scrubWindowS, end - start);
    final lo = (_scrubWindowStartS ?? end - length).clamp(start, end - length);
    return (lo, lo + length);
  }

  /// Jumps to `t` from the overview strip, ◀/▶ or the time picker: centres
  /// the window on it and scrubs there. At or past the end, goes live.
  void _jumpTo(double t) {
    final start = _historyStartS;
    final end = _historyEndS;
    if (start == null || end == null) return;
    if (t >= end) {
      _goLive();
      return;
    }
    t = math.max(t, start);
    _scrubWindowStartS = t - _scrubWindowS / 2;
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
            _buildScrubWindowMenu(),
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

  Widget _buildScrubWindowMenu() {
    return PopupMenuButton<double>(
      tooltip: 'Slider span',
      initialValue: _scrubWindowS,
      onSelected: (s) {
        final t = _scrubTimeS;
        _scrubWindowS = s;
        // Recentre on the scrub position, so it stays within the new window.
        if (t != null) _scrubWindowStartS = t - s / 2;
        _aircraftChanged.notify();
      },
      itemBuilder: (context) => [
        for (final s in _FrameTablePageState._scrubWindowChoicesS) PopupMenuItem(value: s, child: Text(_windowLabel(s))),
      ],
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [Text(_windowLabel(_scrubWindowS)), const Icon(Icons.arrow_drop_down, size: 18)],
        ),
      ),
    );
  }

  /// Two-level scrubber: an overview strip of the whole history, where a
  /// click or drag jumps (moving the window there), over a fine slider that
  /// covers just the window.
  Widget _buildScrubber(double start, double end, double value) {
    final (lo, hi) = _scrubWindow(start, end);
    final enabled = _connected && end > start;
    final color = Theme.of(context).colorScheme.primary;
    final trackColor = Theme.of(context).colorScheme.outlineVariant;
    return LayoutBuilder(
      builder: (context, constraints) {
        // Inset to line up with the slider's track.
        const inset = 24.0;
        final width = math.max(1.0, constraints.maxWidth - 2 * inset);
        void jump(Offset local) {
          if (enabled) _jumpTo(start + ((local.dx - inset) / width).clamp(0.0, 1.0) * (end - start));
        }

        return Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Tooltip(
              message: '${_units.formatDateTime(start)} – ${_units.formatDateTime(end)}'
                  '${_units.timeZone == TimeZoneMode.utc ? ' UTC' : ''}\n'
                  'Shading: aircraft seen per minute (peak $_activityPeakCount)\nClick or drag to jump',
              waitDuration: const Duration(milliseconds: 600),
              child: GestureDetector(
                onTapDown: (d) => jump(d.localPosition),
                onHorizontalDragUpdate: (d) => jump(d.localPosition),
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: inset),
                  child: CustomPaint(
                    size: const Size(double.infinity, 20),
                    painter: TimelinePainter(
                      startS: start,
                      endS: end,
                      windowStartS: lo,
                      windowEndS: hi,
                      positionS: value,
                      activity: _activity,
                      activityT0: _activityT0,
                      activityBucketS: _activityBucketS,
                      color: color,
                      trackColor: trackColor,
                      utcOffsetS: _units.utcOffsetS(start),
                    ),
                  ),
                ),
              ),
            ),
            SizedBox(
              height: 32,
              child: Slider(
                min: lo,
                max: hi,
                value: value.clamp(lo, hi),
                onChanged: enabled && hi > lo
                    ? (t) {
                        // Pin the window, so it stops following the live end while dragging.
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
      },
    );
  }
}
