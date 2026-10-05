part of 'frame_table_page.dart';

// A numeric cell with a small visual just left of the value. The value is
// right-aligned in a fixed `textWidth` (fitting the column's widest
// value), so values and visuals both line up down the column.
Widget _visualCell(Widget visual, String text, double textWidth) => Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        visual,
        const SizedBox(width: 6),
        SizedBox(width: textWidth, child: Text(text, textAlign: TextAlign.right)),
      ],
    );

// The small visuals shown beside values, in the table and the map
// tooltips. Grey when `a` is stale.

// Sort order of the status column: alerts first, then ident, then on the ground.
int _statusRank(AircraftRecord a) =>
    (a.alert ? 4 : 0) + (a.spi ? 2 : 0) + (a.onGround == true ? 1 : 0);

/// The aircraft table: its columns and the small visuals in their cells,
/// which the map tooltip reuses.
extension _AircraftTable on _FrameTablePageState {
  // Colour for a visual in `a`'s row: `color`, or grey if `a` is stale, to
  // match the row's greyed-out text.
  Color _rowColor(AircraftRecord a, Color color) => _isStale(a) ? Colors.grey.shade400 : color;

  // Gauge to 45,000 ft, in the altitude's colour.
  Widget _altitudeVisual(AircraftRecord a, int alt) => CustomPaint(
        size: const Size(32, 6),
        painter: GaugePainter(alt / 45000, _rowColor(a, altitudeColor(alt)), Colors.grey.shade200),
      );

  // Gauge to 600 kt.
  Widget _speedVisual(AircraftRecord a, double gs) => CustomPaint(
        size: const Size(32, 6),
        painter: GaugePainter(gs / 600, _rowColor(a, Colors.blueGrey), Colors.grey.shade200),
      );

  // Arrow pointing along the track (north up), and the compass point.
  Widget _trackVisual(AircraftRecord a, double trk) => Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Transform.rotate(
            angle: trk * math.pi / 180,
            child: Icon(Icons.navigation, size: 16, color: _rowColor(a, Colors.blueGrey)),
          ),
          const SizedBox(width: 4),
          SizedBox(width: 22, child: Text(compassPoints[((trk + 22.5) / 45).floor() % 8])),
        ],
      );

  // Arrow tilted with the climb or descent: green up, orange down, grey
  // within +-100 fpm (level flight).
  Widget _verticalRateVisual(AircraftRecord a, int vr) {
    final color = vr > 100 ? Colors.green.shade600 : (vr < -100 ? Colors.orange.shade800 : Colors.grey);
    return CustomPaint(size: const Size(20, 20), painter: SlopePainter(vr, _rowColor(a, color)));
  }

  // One bar per 3 dB, from red (1) to green (4-5).
  Widget _signalVisual(AircraftRecord a, double db) {
    final bars = (db / 3).floor().clamp(0, SignalBarsPainter.count - 1) + 1;
    final color = const [Colors.red, Colors.orange, Colors.amber, Colors.green, Colors.green][bars - 1];
    return CustomPaint(
      size: const Size(18, 14),
      painter: SignalBarsPainter(bars, _rowColor(a, color), Colors.grey.shade300),
    );
  }

  // Dot fading from green to grey as the aircraft goes quiet.
  Widget _ageVisual(double age) {
    final color = age < 5
        ? Colors.green
        : age < 15
            ? Colors.lightGreen
            : age < _FrameTablePageState._staleAfterS
                ? Colors.orange
                : Colors.grey.shade400;
    return Container(width: 8, height: 8, decoration: BoxDecoration(color: color, shape: BoxShape.circle));
  }

  Widget _typeVisual(AircraftRecord a, String category) {
    final (shape, _) = categoryMarkers[category] ?? (MarkerShape.unknown, 0.9);
    return CustomPaint(
      size: const Size.square(16),
      painter: MarkerPainter(shape, _rowColor(a, Theme.of(context).colorScheme.primary)),
    );
  }

  // Badges for the flags that are set: on the ground, alert (squawk changed or an emergency) and SPI
  // (ident). `tooltips` is off inside the map marker's own tooltip.
  List<Widget> _statusBadges(AircraftRecord a, {bool tooltips = true}) {
    Widget badge(String text, Color color, String meaning) {
      final c = _rowColor(a, color);
      final box = Container(
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
        decoration: BoxDecoration(border: Border.all(color: c), borderRadius: BorderRadius.circular(3)),
        child: Text(text, style: TextStyle(color: c, fontSize: 10, fontWeight: FontWeight.w600)),
      );
      return tooltips ? Tooltip(message: meaning, child: box) : box;
    }

    return [
      if (a.onGround == true) badge('GND', Colors.brown, 'On the ground'),
      if (a.alert) badge('ALERT', Colors.red, 'Alert: squawk changed, or an emergency'),
      if (a.spi) badge('SPI', Colors.purple, 'Ident (special position indicator)'),
    ];
  }

  List<TableColumn<AircraftRecord>> _buildAircraftColumns() => [
    TableColumn('icao', 70, compare: (a, b) => a.icao.compareTo(b.icao), cell: (a) => Text(a.icao)),
    TableColumn('callsign', 85,
        compare: (a, b) => (a.callsign ?? '').compareTo(b.callsign ?? ''), cell: (a) => Text(a.callsign ?? '')),
    TableColumn('squawk', 70, compare: (a, b) => (a.squawk ?? '').compareTo(b.squawk ?? ''), cell: (a) {
      final emergency = emergencySquawks[a.squawk];
      if (emergency == null) return Text(a.squawk ?? '');
      return Tooltip(
        message: emergency,
        child: Text(a.squawk!, style: const TextStyle(color: Colors.red, fontWeight: FontWeight.bold)),
      );
    }),
    TableColumn('type', 135, compare: (a, b) => (a.category ?? '').compareTo(b.category ?? ''), cell: (a) {
      final code = a.category;
      if (code == null) return const Text('');
      return Tooltip(
        message: code,
        child: Row(
          children: [
            _typeVisual(a, code),
            const SizedBox(width: 6),
            Flexible(child: Text(categoryNames[code] ?? code)),
          ],
        ),
      );
    }),
    TableColumn('alt (${_units.altitude.label})', 120,
        numeric: true,
        compare: (a, b) => compareNullable(a.altitudeFt, b.altitudeFt),
        cell: (a) {
          final alt = a.altitudeFt;
          if (alt == null) return const Text('');
          return _visualCell(_altitudeVisual(a, alt), _units.formatAltitude(alt, unit: false), 42);
        }),
    TableColumn('lat', 85,
        numeric: true,
        compare: (a, b) => compareNullable(a.lat, b.lat),
        cell: (a) => Text(a.lat?.toStringAsFixed(4) ?? '')),
    TableColumn('lon', 95,
        numeric: true,
        compare: (a, b) => compareNullable(a.lon, b.lon),
        cell: (a) => Text(a.lon?.toStringAsFixed(4) ?? '')),
    TableColumn('gs (${_units.speed.label})', 105,
        numeric: true,
        compare: (a, b) => compareNullable(a.groundSpeedKt, b.groundSpeedKt),
        cell: (a) {
          final gs = a.groundSpeedKt;
          if (gs == null) return const Text('');
          return _visualCell(_speedVisual(a, gs), _units.formatSpeed(gs, unit: false), 28);
        }),
    TableColumn('trk', 105,
        numeric: true,
        compare: (a, b) => compareNullable(a.trackDeg, b.trackDeg),
        cell: (a) {
          final trk = a.trackDeg;
          if (trk == null) return const Text('');
          return _visualCell(_trackVisual(a, trk), '${trk.toStringAsFixed(0)}°', 32);
        }),
    TableColumn('vr (${_units.verticalRate.label})', 110,
        numeric: true,
        compare: (a, b) => compareNullable(a.verticalRateFpm, b.verticalRateFpm),
        cell: (a) {
          final vr = a.verticalRateFpm;
          if (vr == null) return const Text('');
          return _visualCell(_verticalRateVisual(a, vr), _units.formatVerticalRate(vr, unit: false), 42);
        }),
    TableColumn('sel alt (${_units.altitude.label})', 105,
        numeric: true,
        compare: (a, b) => compareNullable(a.selectedAltitudeFt, b.selectedAltitudeFt),
        cell: (a) {
          final sel = a.selectedAltitudeFt;
          return Text(sel == null ? '' : _units.formatAltitude(sel, unit: false));
        }),
    TableColumn('status', 120,
        compare: (a, b) => _statusRank(a).compareTo(_statusRank(b)),
        cell: (a) => Wrap(spacing: 4, children: _statusBadges(a))),
    TableColumn('msgs', 70,
        numeric: true, compare: (a, b) => a.messages.compareTo(b.messages), cell: (a) => Text('${a.messages}')),
    TableColumn('sig (dB)', 100,
        numeric: true,
        compare: (a, b) => compareNullable(a.signalDb, b.signalDb),
        cell: (a) {
          final db = a.signalDb;
          if (db == null) return const Text('');
          return _visualCell(_signalVisual(a, db), db.toStringAsFixed(1), 32);
        }),
    TableColumn('last seen', 100,
        numeric: true,
        compare: (a, b) => a.lastSeenUnixS.compareTo(b.lastSeenUnixS),
        cell: (a) => ListenableBuilder(
              listenable: _clockTick,
              builder: (context, _) {
                final age = _ageS(a);
                if (age == null) return const Text('');
                return _visualCell(_ageVisual(age), '${age.toStringAsFixed(0)}s ago', 60);
              },
            )),
  ];

  Widget _buildAircraftTable() {
    return LazyTable<AircraftRecord>(
      columns: _aircraftColumns,
      rows: _sortedAircraft,
      sortColumn: _aircraftSortColumn,
      sortAscending: _aircraftSortAscending,
      onSort: _onAircraftSort,
      onTapRow: (a) => _toggleSelected(a.icao),
      isSelected: (a) => a.icao == _selectedIcao,
      rowStyle: (a) => _isStale(a) ? TextStyle(color: Colors.grey[500]) : null,
    );
  }
}
