part of 'frame_table_page.dart';

/// The map over the aircraft table, with its markers, trails and legend.
extension _AircraftMap on _FrameTablePageState {
  /// The map over the aircraft table, the dock beside them when anything's
  /// in it (sliding in and out), and the scrubber bar under both.
  Widget _buildAircraftPane() {
    final docked = _showFrames || _showSpectrum;
    return Column(
      children: [
        Expanded(
          child: LayoutBuilder(
            builder: (context, constraints) => Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(child: _buildAircraftSplit()),
                AnimatedSwitcher(
                  duration: const Duration(milliseconds: 200),
                  transitionBuilder: (child, animation) => SizeTransition(
                    sizeFactor: animation,
                    axis: Axis.horizontal,
                    axisAlignment: 1,
                    child: child,
                  ),
                  child: docked ? _buildDock(constraints.maxWidth) : const SizedBox.shrink(key: ValueKey('no dock')),
                ),
              ],
            ),
          ),
        ),
        const Divider(height: 1),
        _buildAircraftControls(),
      ],
    );
  }

  Widget _buildAircraftSplit() {
    return LayoutBuilder(
      builder: (context, constraints) {
        const minPaneHeight = _FrameTablePageState._minPaneHeight;
        final height = constraints.maxHeight;
        final maxMapHeight = math.max(minPaneHeight, height - minPaneHeight - 8);
        final mapHeight = (_mapFraction * height).clamp(minPaneHeight, maxMapHeight);
        return Column(
          children: [
            SizedBox(height: mapHeight, child: _buildAircraftMap()),
            MouseRegion(
              cursor: SystemMouseCursors.resizeRow,
              child: GestureDetector(
                behavior: HitTestBehavior.translucent,
                onVerticalDragUpdate: (details) {
                  // Several drag events can arrive per frame, before the rebuild, so start from the state
                  // (already moved by the earlier ones), not the mapHeight this closure captured.
                  final current = (_mapFraction * height).clamp(minPaneHeight, maxMapHeight);
                  _mapFraction = (current + details.delta.dy).clamp(minPaneHeight, maxMapHeight) / height;
                  _aircraftChanged.notify();
                },
                child: const SizedBox(
                  height: 8,
                  child: Center(child: Divider(height: 1)),
                ),
              ),
            ),
            Expanded(child: _buildAircraftTable()),
          ],
        );
      },
    );
  }

  Widget _buildAircraftMap() {
    final positioned = _visibleAircraft.where((a) => a.lat != null && a.lon != null).toList();
    return Stack(
      children: [
        FlutterMap(
          mapController: _mapController,
          options: MapOptions(
            initialCenter: const LatLng(20, 0),
            initialZoom: 2,
            onMapReady: () {
              _mapReady = true;
              _scheduleHeatmap();
            },
            onPositionChanged: (camera, hasGesture) => _scheduleHeatmap(),
          ),
          children: [
            TileLayer(
              urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
              userAgentPackageName: 'com.example.adsb_ui',
            ),
            if (_heatmapOverlay != null) OverlayImageLayer(overlayImages: [_heatmapOverlay!]),
            PolylineLayer(
              polylines: [for (final a in positioned) ..._trailPolylines(a)],
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
        Positioned(left: 12, bottom: 12, child: _buildAltitudeLegend()),
        Positioned(right: 12, top: 12, child: _buildHeatmapMenu()),
        if (_selectedIcao != null && _shownAircraft.containsKey(_selectedIcao))
          Positioned(
            left: 12,
            top: 12,
            bottom: 56,
            width: 300,
            child: _buildDetailPanel(_shownAircraft[_selectedIcao]!),
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
  /// `a`'s trail as polylines, one per run of points in the same 1,000 ft
  /// band, coloured by that band's altitude (flutter_map's gradients run
  /// straight across the screen, not along the line). Grey when stale.
  List<Polyline> _trailPolylines(AircraftRecord a) {
    final trail = _shownTrails[a.icao];
    if (trail == null || trail.length < 2) return const [];
    final selected = a.icao == _selectedIcao;
    final stale = _isStale(a);
    int? band(TrailPoint p) => p.altFt == null ? null : p.altFt! ~/ 1000;
    Color colorOf(int? band) => stale
        ? Colors.grey
        : band == null
            ? Theme.of(context).colorScheme.primary
            : altitudeColor(band * 1000 + 500);
    final lines = <Polyline>[];
    var start = 0;
    for (var i = 1; i <= trail.length; i++) {
      if (i < trail.length && band(trail[i]) == band(trail[start])) continue;
      // Each run ends on the next run's first point, so the line is unbroken.
      final end = math.min(i, trail.length - 1);
      if (end > start) {
        lines.add(Polyline(
          points: [for (final p in trail.sublist(start, end + 1)) p.pos],
          strokeWidth: selected ? 3 : 2,
          color: colorOf(band(trail[start])).withValues(alpha: selected ? 0.9 : 0.6),
        ));
      }
      start = i;
    }
    return lines;
  }

  Widget _buildAltitudeLegend() {
    final theme = Theme.of(context);
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(8, 6, 8, 4),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            CustomPaint(size: const Size(160, 8), painter: AltitudeScalePainter()),
            const SizedBox(height: 2),
            SizedBox(
              width: 160,
              child: DefaultTextStyle.merge(
                style: theme.textTheme.labelSmall,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  // The scale's ticks are every 10,000 ft; in metres they're labelled every 3 km, which
                  // is 1.6% short of 3,048 m -- a pixel or two on the legend.
                  children: _units.altitude == AltitudeUnit.ft
                      ? const [Text('0'), Text('10k'), Text('20k'), Text('30k'), Text('40k ft')]
                      : const [Text('0'), Text('3k'), Text('6k'), Text('9k'), Text('12k m')],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// A map marker's tooltip: the aircraft's values with the same visuals as
  /// the table, in label / visual / value rows.
  Widget _buildAircraftTooltip(AircraftRecord a) {
    const labelStyle = TextStyle(color: Colors.white70, fontSize: 12);
    const valueStyle = TextStyle(color: Colors.white, fontSize: 12);
    TableRow row(String label, Widget? visual, String value, {TextStyle style = valueStyle}) => TableRow(
          children: [
            Padding(padding: const EdgeInsets.only(right: 12, bottom: 2), child: Text(label, style: labelStyle)),
            Padding(
              padding: const EdgeInsets.only(right: 6, bottom: 2),
              child: Align(alignment: Alignment.centerRight, child: visual ?? const SizedBox.shrink()),
            ),
            Padding(
              padding: const EdgeInsets.only(bottom: 2),
              child: Text(value, style: style, textAlign: TextAlign.right),
            ),
          ],
        );
    final emergency = emergencySquawks[a.squawk];
    final age = _ageS(a);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          a.callsign == null ? a.icao : '${a.callsign}  (${a.icao})',
          style: valueStyle.copyWith(fontWeight: FontWeight.bold, fontSize: 13),
        ),
        if (a.category != null)
          Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                _typeVisual(a, a.category!),
                const SizedBox(width: 6),
                Text(categoryNames[a.category] ?? a.category!, style: valueStyle),
              ],
            ),
          ),
        const SizedBox(height: 6),
        DefaultTextStyle.merge(
          style: valueStyle,
          child: Table(
            defaultColumnWidth: const IntrinsicColumnWidth(),
            defaultVerticalAlignment: TableCellVerticalAlignment.middle,
            children: [
              if (a.squawk != null)
                row(
                  'squawk',
                  null,
                  emergency == null ? a.squawk! : '${a.squawk} $emergency',
                  style: emergency == null
                      ? valueStyle
                      : valueStyle.copyWith(color: Colors.red.shade300, fontWeight: FontWeight.bold),
                ),
              if (a.altitudeFt != null) row('alt', _altitudeVisual(a, a.altitudeFt!), _units.formatAltitude(a.altitudeFt!)),
              if (a.groundSpeedKt != null)
                row('gs', _speedVisual(a, a.groundSpeedKt!), _units.formatSpeed(a.groundSpeedKt!)),
              if (a.trackDeg != null)
                row('trk', _trackVisual(a, a.trackDeg!), '${a.trackDeg!.toStringAsFixed(0)}°'),
              if (a.verticalRateFpm != null)
                row('vr', _verticalRateVisual(a, a.verticalRateFpm!), _units.formatVerticalRate(a.verticalRateFpm!)),
              if (a.headingDeg != null) row('hdg', null, '${a.headingDeg!.toStringAsFixed(0)}° mag'),
              if (_airspeeds(a).isNotEmpty) row('air', null, _airspeeds(a).join(' · ')),
              if (a.selectedAltitudeFt != null)
                row('sel alt', null, '${_units.formatAltitude(a.selectedAltitudeFt!)}${_sourceSuffix(a)}'),
              if (a.autopilotModes != null) row('ap', null, _modesText(a.autopilotModes!)),
              if (_statusBadges(a).isNotEmpty)
                row('status', Wrap(spacing: 4, children: _statusBadges(a, tooltips: false)), ''),
              if (a.signalDb != null) row('sig', _signalVisual(a, a.signalDb!), '${a.signalDb!.toStringAsFixed(1)} dB'),
              row('msgs', null, '${a.messages}'),
              if (age != null) row('seen', _ageVisual(age), '${age.toStringAsFixed(0)}s ago'),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildAircraftMarker(AircraftRecord a) {
    final stale = _isStale(a);
    final selected = a.icao == _selectedIcao;
    final color = stale
        ? Colors.grey
        : emergencySquawks.containsKey(a.squawk)
            ? Colors.red
            : a.altitudeFt != null
                ? altitudeColor(a.altitudeFt!)
                : Theme.of(context).colorScheme.primary;
    final (shape, scale) = categoryMarkers[a.category] ?? (MarkerShape.unknown, 0.9);
    final icon = CustomPaint(size: Size.square(26 * scale), painter: MarkerPainter(shape, color));
    return GestureDetector(
      onTap: () => _toggleSelected(a.icao),
      child: Tooltip(
        richMessage: WidgetSpan(child: _buildAircraftTooltip(a)),
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
            child: MarkerPainter.isUpright(shape)
                ? icon
                : Transform.rotate(angle: (a.trackDeg ?? 0) * math.pi / 180, child: icon),
          ),
        ),
      ),
    );
  }

  void _recenterMap() {
    final positioned = _visibleAircraft.where((a) => a.lat != null && a.lon != null).toList();
    if (positioned.isEmpty) return;
    final lat = positioned.map((a) => a.lat!).reduce((a, b) => a + b) / positioned.length;
    final lon = positioned.map((a) => a.lon!).reduce((a, b) => a + b) / positioned.length;
    _mapController.move(LatLng(lat, lon), _mapController.camera.zoom);
  }
}
