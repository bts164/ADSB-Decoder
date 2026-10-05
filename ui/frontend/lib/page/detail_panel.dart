part of 'frame_table_page.dart';

// " MCP" or " FMS", for after a selected altitude.
String _sourceSuffix(AircraftRecord a) =>
    a.selectedAltitudeSource == null ? '' : ' ${a.selectedAltitudeSource}';

String _modesText(List<String> modes) => modes.isEmpty ? 'none engaged' : modes.join(' ');

/// Horizontal position accuracy of each NACp (95% bound) in metres, DO-260B
/// table 2-14; null for 0, unknown.
const _nacPBoundsM = [
  null, 10 * 1852.0, 4 * 1852.0, 2 * 1852.0, 1852.0, 0.5 * 1852, 0.3 * 1852, 0.1 * 1852, //
  93.0, 30.0, 10.0, 3.0,
];

/// The selected aircraft's panel on the map: facts and history charts.
extension _DetailPanel on _FrameTablePageState {
  // IAS, TAS and Mach, those known.
  List<String> _airspeeds(AircraftRecord a) => [
        if (a.iasKt != null) '${_units.formatSpeed(a.iasKt!)} IAS',
        if (a.tasKt != null) '${_units.formatSpeed(a.tasKt!)} TAS',
        if (a.mach != null) 'M${a.mach!.toStringAsFixed(3)}',
      ];

  String _nacPText(int nacP) {
    final m = _nacPBoundsM[nacP];
    return m == null ? 'unknown' : '< ${_units.formatDistanceM(m)}';
  }

  /// The detail panel's values beyond those charted: air data, autopilot
  /// settings and ADS-B quality, as label-over-value pairs. Empty if none
  /// are known.
  List<Widget> _buildDetailFacts(AircraftRecord a) {
    final theme = Theme.of(context);
    final facts = <(String, String)>[
      if (a.headingDeg != null) ('heading', '${a.headingDeg!.toStringAsFixed(0)}° mag'),
      if (a.trackDeg != null) ('track', '${a.trackDeg!.toStringAsFixed(0)}°'),
      if (a.iasKt != null) ('IAS', _units.formatSpeed(a.iasKt!)),
      if (a.tasKt != null) ('TAS', _units.formatSpeed(a.tasKt!)),
      if (a.mach != null) ('Mach', a.mach!.toStringAsFixed(3)),
      if (a.rollDeg != null) ('roll', '${a.rollDeg!.toStringAsFixed(1)}°'),
      if (a.selectedAltitudeFt != null) ('selected alt', '${_units.formatAltitude(a.selectedAltitudeFt!)}${_sourceSuffix(a)}'),
      if (a.selectedHeadingDeg != null) ('selected hdg', '${a.selectedHeadingDeg!.toStringAsFixed(0)}°'),
      if (a.baroSettingHpa != null) ('baro', _units.formatPressure(a.baroSettingHpa!)),
      if (a.autopilotModes != null) ('autopilot', _modesText(a.autopilotModes!)),
      if (a.adsbVersion != null) ('ADS-B', 'v${a.adsbVersion}'),
      if (a.nacP != null && a.nacP! < _nacPBoundsM.length) ('NACp', '${a.nacP} (${_nacPText(a.nacP!)})'),
      if (a.sil != null) ('SIL', '${a.sil}'),
    ];
    if (facts.isEmpty) return [];
    return [
      Wrap(
        spacing: 16,
        runSpacing: 6,
        children: [
          for (final (label, value) in facts)
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(label, style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline)),
                Text(value, style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold)),
              ],
            ),
        ],
      ),
      const SizedBox(height: 12),
    ];
  }

  /// The selected aircraft's details: header with status badges, the
  /// values from _buildDetailFacts, then charts of altitude (with the
  /// selected altitude dashed), ground speed, vertical rate and signal over
  /// _selectedHistory.
  Widget _buildDetailPanel(AircraftRecord a) {
    final theme = Theme.of(context);
    final history = _selectedHistory;
    final times = [for (final p in history) p.t];
    final tMax = times.isEmpty ? a.lastSeenUnixS : times.last;
    final tMin = times.isEmpty ? tMax : times.first;
    final grid = theme.dividerColor;

    Widget chart(String label, String? current, List<double?> values, Color Function(double) colorOf,
        {bool zeroLine = false, String Function(double)? format, List<double?>? targets}) {
      final present = values.whereType<double>();
      final fmt = format ?? (double v) => v.toStringAsFixed(0);
      return Padding(
        padding: const EdgeInsets.only(bottom: 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(label, style: theme.textTheme.labelMedium),
                const Spacer(),
                if (present.isNotEmpty)
                  Text(
                    '${fmt(present.reduce(math.min))} – ${fmt(present.reduce(math.max))}   ',
                    style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline),
                  ),
                Text(current ?? '–', style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold)),
              ],
            ),
            const SizedBox(height: 4),
            SizedBox(
              height: 44,
              width: double.infinity,
              child: CustomPaint(
                painter: TimeSeriesPainter(
                  times: times,
                  values: values,
                  tMin: tMin,
                  tMax: tMax,
                  colorOf: colorOf,
                  gridColor: grid,
                  zeroLine: zeroLine,
                  targets: targets,
                  targetColor: theme.colorScheme.outline,
                ),
              ),
            ),
          ],
        ),
      );
    }

    final spanMin = (tMax - tMin) / 60;
    return Card(
      margin: EdgeInsets.zero,
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 8, 4, 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                if (a.category != null) ...[_typeVisual(a, a.category!), const SizedBox(width: 8)],
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(a.callsign ?? a.icao, style: theme.textTheme.titleSmall),
                      Text(
                        [a.icao, if (a.category != null) categoryNames[a.category] ?? a.category!, if (a.squawk != null) 'squawk ${a.squawk}']
                            .join(' · '),
                        style: theme.textTheme.labelSmall,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                      if (_statusBadges(a).isNotEmpty)
                        Padding(
                          padding: const EdgeInsets.only(top: 2),
                          child: Wrap(spacing: 4, children: _statusBadges(a)),
                        ),
                    ],
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close, size: 18),
                  visualDensity: VisualDensity.compact,
                  tooltip: 'Close',
                  onPressed: () => _toggleSelected(a.icao),
                ),
              ],
            ),
            const Divider(),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.only(right: 8),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    ..._buildDetailFacts(a),
                    chart(
                      'Altitude (${_units.altitude.label})${history.any((p) => p.selAltFt != null) ? ', - - selected' : ''}',
                      a.altitudeFt == null ? null : _units.formatAltitude(a.altitudeFt!, unit: false),
                      [for (final p in history) p.altFt?.toDouble()],
                      (v) => altitudeColor(v.round()),
                      targets: history.any((p) => p.selAltFt != null)
                          ? [for (final p in history) p.selAltFt?.toDouble()]
                          : null,
                      format: (v) => _units.formatAltitude(v.round(), unit: false),
                    ),
                    chart(
                      'Ground speed (${_units.speed.label})',
                      a.groundSpeedKt == null ? null : _units.formatSpeed(a.groundSpeedKt!, unit: false),
                      [for (final p in history) p.gsKt],
                      (_) => Colors.blueGrey,
                      format: (v) => _units.formatSpeed(v, unit: false),
                    ),
                    chart(
                      'Vertical rate (${_units.verticalRate.label})',
                      a.verticalRateFpm == null ? null : _units.formatVerticalRate(a.verticalRateFpm!, unit: false),
                      [for (final p in history) p.vrFpm?.toDouble()],
                      (v) => v > 100 ? Colors.green.shade600 : (v < -100 ? Colors.orange.shade800 : Colors.grey),
                      zeroLine: true,
                      format: (v) => _units.formatVerticalRate(v.round(), unit: false),
                    ),
                    chart(
                      'Signal (dB)',
                      a.signalDb?.toStringAsFixed(1),
                      [for (final p in history) p.signalDb],
                      (_) => Colors.teal,
                      format: (v) => v.toStringAsFixed(1),
                    ),
                    Text(
                      times.length < 2
                          ? 'Collecting history…'
                          : 'Last ${spanMin < 1 ? '${(spanMin * 60).round()} s' : '${spanMin.toStringAsFixed(0)} min'}'
                              ', ${history.length} updates',
                      style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
