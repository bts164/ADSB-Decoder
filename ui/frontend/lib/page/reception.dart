part of 'frame_table_page.dart';

/// The Reception section's plots, shown one at a time.
enum _ReceptionPlot {
  coverage('Coverage'),
  signal('Signal'),
  side('Side');

  const _ReceptionPlot(this.label);
  final String label;
}

/// The dock's Reception section: how far, how well and how low the receiver hears, from where it is.
extension _Reception on _FrameTablePageState {
  static const _antennaAglChoicesM = [2.0, 5.0, 10.0, 20.0, 50.0, 100.0];
  // The shortest range axis, so a quiet sky still has a sensible scale.
  static const _minRangeM = 20 * 1852.0;

  Widget _buildReceptionSection() {
    return _buildDockSection(
      title: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Text('Reception'),
          const SizedBox(width: 12),
          SegmentedButton<_ReceptionPlot>(
            segments: [for (final p in _ReceptionPlot.values) ButtonSegment(value: p, label: Text(p.label))],
            selected: {_receptionPlot},
            showSelectedIcon: false,
            style: const ButtonStyle(
              visualDensity: VisualDensity.compact,
              tapTargetSize: MaterialTapTargetSize.shrinkWrap,
            ),
            onSelectionChanged: (s) {
              _receptionPlot = s.first;
              _aircraftChanged.notify();
            },
          ),
        ],
      ),
      onClose: () => _setShowReception(false),
      child: _buildReceptionPane(),
    );
  }

  Widget _buildReceptionPane() {
    final receiver = _shownReceiver;
    final position = receiver?.position;
    if (receiver == null || !receiver.hasFix || position == null) {
      return const Center(child: Text('No receiver position: the plots are drawn from it'));
    }
    final theme = Theme.of(context);
    final style = PlotStyle(
      grid: theme.colorScheme.outlineVariant,
      text: theme.textTheme.labelSmall!.copyWith(color: theme.colorScheme.onSurfaceVariant),
      area: theme.colorScheme.secondary,
      highlight: theme.colorScheme.primary,
    );
    return switch (_receptionPlot) {
      _ReceptionPlot.coverage => _buildCoveragePlot(position, style),
      _ReceptionPlot.signal => _buildSignalPlot(style),
      _ReceptionPlot.side => _buildSideViewPlot(receiver, position, style),
    };
  }

  Widget _buildCoveragePlot(LatLng receiver, PlotStyle style) {
    final coverage = coverageOf(receiver, _shownTrails);
    final aircraft = _plotAircraft(receiver);
    final farthest = [
      for (final b in coverage) b?.rangeM ?? 0,
      for (final a in aircraft) a.rangeM,
    ].fold<double>(0, math.max);
    final axis = _rangeAxis(farthest);
    final heard = coverage.whereType<CoverageBin>().length;
    return _buildReceptionPlot(
      CoveragePainter(
        coverage: coverage,
        aircraft: aircraft,
        maxRangeM: axis.max,
        rangeStepM: axis.step,
        rangeLabel: _units.formatDistanceM,
        style: style,
        selectedIcao: _selectedIcao,
      ),
      'The farthest position in each ${360 ~/ coverage.length}° sector, from the trails shown: heard in $heard '
      'of ${coverage.length}, out to ${_units.formatDistanceM(farthest)}. Dots: the aircraft shown now, by '
      'altitude.',
    );
  }

  Widget _buildSignalPlot(PlotStyle style) {
    final scrubbing = _snapshot != null;
    final samples = [
      for (final s in _signalLog.samples)
        if (!scrubbing || s.t <= _shownTimeS) s,
    ];
    final axis = _rangeAxis(samples.fold<double>(0, (m, s) => math.max(m, s.rangeM)));
    final legend = samples.isEmpty
        ? 'No samples yet: one is logged at each new aircraft position while connected.'
        : '${samples.length} samples, one at each new aircraft position since connecting'
            '${scrubbing ? ', up to the time shown' : ''}. The signal is relative to its block\'s noise and '
            'signal, not absolute power. Colour: bearing (N red, E lime, S cyan, W violet), older fainter. '
            'Line: the free-space loss, 20 dB per decade, through the median.';
    return _buildReceptionPlot(
      SignalPainter(
        samples: samples,
        fitDb: freeSpaceFitDb(samples),
        maxRangeM: axis.max,
        rangeStepM: axis.step,
        rangeLabel: _units.formatDistanceM,
        style: style,
        selectedIcao: _selectedIcao,
      ),
      legend,
      controls: Align(
        alignment: Alignment.centerRight,
        child: TextButton.icon(
          icon: const Icon(Icons.restart_alt, size: 18),
          label: const Text('Clear'),
          style: TextButton.styleFrom(visualDensity: VisualDensity.compact),
          onPressed: _signalLog.isEmpty
              ? null
              : () {
                  _signalLog.clear();
                  _aircraftChanged.notify();
                },
        ),
      ),
    );
  }

  Widget _buildSideViewPlot(ReceiverPosition receiver, LatLng position, PlotStyle style) {
    final aircraft = [
      for (final a in _plotAircraft(position))
        if (a.altFt != null) a,
    ];
    final antennaM = receiver.altM;
    final groundM = antennaM == null ? 0.0 : antennaM - _antennaAglM;
    final rangeAxis = _rangeAxis(aircraft.fold<double>(0, (m, a) => math.max(m, a.rangeM)));
    final topFt = aircraft.fold<double>(10000, (m, a) => math.max(m, a.altFt!.toDouble()));
    final altAxis = _axis(topFt, _units.altitude.perFt);
    final theme = Theme.of(context);
    return _buildReceptionPlot(
      SideViewPainter(
        aircraft: aircraft,
        groundM: groundM,
        antennaAglM: _antennaAglM,
        maxAltFt: altAxis.max,
        altStepFt: altAxis.step,
        altLabel: (ft) => _units.formatAltitude(ft.round()),
        maxRangeM: rangeAxis.max,
        rangeStepM: rangeAxis.step,
        rangeLabel: _units.formatDistanceM,
        style: style,
        selectedIcao: _selectedIcao,
      ),
      'Shaded: below the radio horizon, over smooth ground with standard refraction (a 4/3 earth). '
      '${antennaM == null ? 'The receiver reports no altitude, so the ground is taken as sea level.' : 'The ground '
          'is at ${_units.formatAltitude((groundM / 0.3048).round())}, from the receiver\'s altitude.'} '
      'Altitudes are pressure altitudes, within a few hundred feet of the true ones.',
      controls: Row(
        mainAxisAlignment: MainAxisAlignment.end,
        children: [
          Text('Antenna above ground', style: theme.textTheme.bodySmall),
          const SizedBox(width: 8),
          DropdownButton<double>(
            value: _antennaAglM,
            isDense: true,
            items: [
              for (final m in _antennaAglChoicesM)
                DropdownMenuItem(value: m, child: Text(_units.formatAltitude((m / 0.3048).round()))),
            ],
            onChanged: (m) {
              if (m == null) return;
              _antennaAglM = m;
              _aircraftChanged.notify();
            },
          ),
        ],
      ),
    );
  }

  /// A plot over its legend, with `controls` above; tapping an aircraft on it selects it.
  Widget _buildReceptionPlot(ReceptionPainter painter, String legend, {Widget? controls}) {
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (controls != null) ...[controls, const SizedBox(height: 4)],
          Expanded(
            child: LayoutBuilder(
              builder: (context, constraints) => GestureDetector(
                onTapUp: (details) {
                  final icao = painter.icaoAt(details.localPosition, constraints.biggest);
                  if (icao != null) _toggleSelected(icao);
                },
                child: ClipRect(child: CustomPaint(size: Size.infinite, painter: painter)),
              ),
            ),
          ),
          const SizedBox(height: 4),
          Text(legend, style: Theme.of(context).textTheme.bodySmall),
        ],
      ),
    );
  }

  /// The aircraft shown now that have a position, placed from the receiver.
  List<PlotAircraft> _plotAircraft(LatLng receiver) => [
        for (final a in _visibleAircraft)
          if (a.lat != null && a.lon != null)
            if (sphereRangeBearing(receiver, LatLng(a.lat!, a.lon!)) case final rb
                when rb.rangeM <= receptionMaxRangeM)
              (icao: a.icao, rangeM: rb.rangeM, bearingDeg: rb.bearingDeg, altFt: a.altitudeFt),
      ];

  ({double max, double step}) _rangeAxis(double farthestM) =>
      _axis(math.max(farthestM, _minRangeM), _units.distance.perNm / 1852);

  /// An axis from 0 to at least `span`, in round steps of the displayed unit, `perBase` of which make one of
  /// `span`'s.
  static ({double max, double step}) _axis(double span, double perBase) {
    final step = niceStep(span * perBase, 5) / perBase;
    return (max: math.max(1, (span / step - 1e-9).ceil()) * step, step: step);
  }
}
