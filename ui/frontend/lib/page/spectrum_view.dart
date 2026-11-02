part of 'frame_table_page.dart';

/// Which spectrum plot the pointer is over.
enum _SpectrumPlot { psd, waterfall }

/// The part of the band the spectrum plots show, and what's marked on it, shared by the PSD and the waterfall
/// so they stay aligned. Positions are fractions of the full span, 0 at its low edge; a retune or a new bin
/// count clears them all, since they'd mark other frequencies.
class _SpectrumView extends _PlotAxis {
  _SpectrumView() : super(minCells: 8);

  _SpectrumPlot? hoverPlot;
  /// Over the waterfall, the row under the pointer, counted back from the newest; null past the oldest.
  int? hoverRowsBack;
  double _freqHz = 0;
  double _rateHz = 0;

  int get bins => cells;
  double get rateHz => _rateHz;

  double freqOf(double fraction) => _freqHz - _rateHz / 2 + fraction * _rateHz;
  int binOf(double fraction) => cellOf(fraction);

  /// Starts over if the rows now cover other frequencies.
  void follow(double freqHz, double rateHz, int bins) {
    if (freqHz == _freqHz && rateHz == _rateHz && bins == this.bins) return;
    _freqHz = freqHz;
    _rateHz = rateHz;
    reset(1, bins);
  }

  void hoverOn(_SpectrumPlot plot, double fraction, int? rowsBack) {
    hoverPlot = plot;
    hoverRowsBack = rowsBack;
    hover(fraction);
  }

  @override
  void unhover() {
    hoverPlot = null;
    hoverRowsBack = null;
    super.unhover();
  }
}

/// The spectrum plots' mouse handling, and the readouts of what's under the pointer and marked.
extension _SpectrumInteraction on _FrameTablePageState {
  /// `child`, one of the plots, under the hover crosshair, markers and band, and taking the mouse as
  /// [_PlotGestures] does; the PSD carries the mode bar.
  Widget _spectrumInteractive(_SpectrumPlot plot, Widget child) {
    final view = _spectrumView;
    final theme = Theme.of(context);
    return _PlotGestures(
      axis: view,
      modeBar: plot == _SpectrumPlot.psd,
      actions: [
        _plotBarButton(
          context,
          Icons.tune,
          _spectrumControlsShown ? 'Hide the display settings' : 'Display settings: colors, holds and persistence',
          () => _showSpectrumControls(!_spectrumControlsShown),
          selected: _spectrumControlsShown,
        ),
      ],
      onHover: (p, size, fraction) {
        int? rowsBack;
        if (plot == _SpectrumPlot.waterfall && p.dy >= 0) {
          final back = p.dy ~/ (size.height / _FrameTablePageState._maxSpectrumRows);
          rowsBack = back < _spectrumHistory.value.length ? back : null;
        }
        view.hoverOn(plot, fraction, rowsBack);
      },
      overlay: _SpectrumOverlayPainter(
        view: view,
        plot: plot,
        color: plot == _SpectrumPlot.waterfall ? Colors.white : theme.colorScheme.onSurface,
        labelStyle: theme.textTheme.labelSmall!,
      ),
      child: child,
    );
  }

  /// The edge frequencies of the view, then a line each for the hover, the markers and the band.
  Widget _buildSpectrumReadout(List<_WaterfallRow> rows) {
    final view = _spectrumView;
    return ListenableBuilder(
      listenable: Listenable.merge([view.marksChanged, view.modeChanged, _spectrumHolds]),
      builder: (context, _) {
        final newest = rows.last;
        final bins = newest.binsDb.length;
        // A row from before a retune would be read at the wrong frequencies.
        double? levelAt(_WaterfallRow row, int bin) =>
            row.binsDb.length == bins && bin < bins ? row.binsDb[bin] - row.floorDb : null;
        String freq(double fraction) => '${(view.freqOf(fraction) / 1e6).toStringAsFixed(4)} MHz';

        final hoverAt = view.hoverAt;
        String hover = 'Wheel zooms, ${view.mode.hint}, click marks, double-click unzooms';
        if (hoverAt != null) {
          final bin = view.binOf(hoverAt);
          final centre = (bin + 0.5) / bins;
          final back = view.hoverRowsBack;
          if (view.hoverPlot == _SpectrumPlot.waterfall) {
            final level = back == null ? null : levelAt(rows[rows.length - 1 - back], bin);
            hover = back == null || level == null
                ? freq(centre)
                : '${freq(centre)}, ${back == 0 ? 'newest row' : '$back rows back'}: ${_signedDb(level)}';
          } else {
            final level = levelAt(newest, bin);
            final held = [
              for (final hold in _SpectrumHold.values)
                if (_spectrumHolds.shown.contains(hold))
                  if (_spectrumHolds.levels(hold) case final levels? when levels.length == bins)
                    '${hold.label.toLowerCase()} ${_signedDb(levels[bin] - newest.floorDb)}',
            ];
            hover = [
              freq(centre),
              if (level != null) _signedDb(level),
              if (held.isNotEmpty) '(${held.join(', ')})',
            ].join('  ');
          }
        }

        String? markers;
        if (view.markers.isNotEmpty) {
          final levels = [for (final m in view.markers) levelAt(newest, view.binOf(m))];
          markers = [
            for (final (i, m) in view.markers.indexed)
              'M${i + 1} ${freq(m)} ${levels[i] == null ? '' : _signedDb(levels[i]!)}',
            if (levels.length == 2 && levels[0] != null && levels[1] != null)
              'Δ ${_spanText((view.markers[1] - view.markers[0]) * view.rateHz)} '
                  '${_signedDb(levels[1]! - levels[0]!)}',
          ].join('   ');
        }

        String? band;
        final b = view.measured;
        if (b != null && newest.binsDb.length == bins) {
          final first = view.binOf(b.$1), last = view.binOf(b.$2);
          var peak = first;
          var power = 0.0;
          for (var i = first; i <= last; i++) {
            if (newest.binsDb[i] > newest.binsDb[peak]) peak = i;
            power += math.pow(10, (newest.binsDb[i] - newest.floorDb) / 10);
          }
          final count = last - first + 1;
          band = 'Band ${_spanText(count * view.rateHz / bins)} from ${freq(first / bins)}: '
              'peak ${_signedDb(newest.binsDb[peak] - newest.floorDb)} at ${freq((peak + 0.5) / bins)}, '
              'mean ${_signedDb(10 * math.log(power / count) / math.ln10)}';
        }

        final style = Theme.of(context).textTheme.bodySmall;
        Widget line(String text, List<Widget> actions) => SizedBox(
              height: 28,
              child: Row(
                children: [
                  Expanded(child: Text(text, style: style, overflow: TextOverflow.ellipsis)),
                  ...actions,
                ],
              ),
            );
        Widget action(IconData icon, String tooltip, VoidCallback onPressed) => IconButton(
              icon: Icon(icon, size: 18),
              tooltip: tooltip,
              visualDensity: VisualDensity.compact,
              padding: EdgeInsets.zero,
              onPressed: onPressed,
            );

        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(freq(view.lo)),
                Text(view.zoomed
                    ? 'span ${_spanText((view.hi - view.lo) * view.rateHz)}'
                    : '${freq(0.5)} (center)'),
                Text(freq(view.hi)),
              ],
            ),
            line(hover, []),
            if (markers != null) line(markers, [action(Icons.close, 'Clear the markers', view.clearMarkers)]),
            if (band != null)
              line(band, [
                action(Icons.zoom_in, 'Zoom to the band', view.zoomToMeasured),
                action(Icons.close, 'Clear the band', view.clearMeasured),
              ]),
          ],
        );
      },
    );
  }

  static String _signedDb(double db) => '${db >= 0 ? '+' : '-'}${db.abs().toStringAsFixed(1)} dB';

  static String _spanText(double hz) {
    final a = hz.abs();
    final sign = hz < 0 ? '-' : '';
    if (a >= 1e6) return '$sign${(a / 1e6).toStringAsFixed(3)} MHz';
    if (a >= 1e3) return '$sign${(a / 1e3).toStringAsFixed(1)} kHz';
    return '$sign${a.round()} Hz';
  }
}

/// Over one spectrum plot: the band, the markers and the hover crosshair, which over the waterfall also marks
/// the row. Repaints on its own as they change, without the plot under it.
class _SpectrumOverlayPainter extends CustomPainter {
  _SpectrumOverlayPainter({required this.view, required this.plot, required this.color, required this.labelStyle})
      : super(repaint: Listenable.merge([view.zoomChanged, view.marksChanged]));

  final _SpectrumView view;
  final _SpectrumPlot plot;
  final Color color;
  final TextStyle labelStyle;

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    _paintAxisSpans(canvas, size, view, color);

    for (final (i, m) in view.markers.indexed) {
      final x = view.xOf(m, w);
      if (x < 0 || x > w) continue;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), Paint()..color = color.withValues(alpha: 0.9));
      if (plot == _SpectrumPlot.psd) {
        final label = TextPainter(
          text: TextSpan(text: 'M${i + 1}', style: labelStyle.copyWith(color: color)),
          textDirection: TextDirection.ltr,
        )..layout();
        label.paint(canvas, Offset(x + 3, 0));
        label.dispose();
      }
    }

    final hoverAt = view.hoverAt;
    if (hoverAt != null) {
      final hover = Paint()..color = color.withValues(alpha: 0.5);
      final x = view.xOf(hoverAt, w);
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), hover);
      final back = view.hoverRowsBack;
      if (plot == _SpectrumPlot.waterfall && view.hoverPlot == _SpectrumPlot.waterfall && back != null) {
        final y = (back + 0.5) * size.height / _FrameTablePageState._maxSpectrumRows;
        canvas.drawLine(Offset(0, y), Offset(w, y), hover);
      }
    }
  }

  @override
  bool shouldRepaint(_SpectrumOverlayPainter old) =>
      old.view != view || old.plot != plot || old.color != color || old.labelStyle != labelStyle;
}
