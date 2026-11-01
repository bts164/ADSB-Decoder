part of 'frame_table_page.dart';

/// Which spectrum plot the pointer is over.
enum _SpectrumPlot { psd, waterfall }

/// The part of the band the spectrum plots show, and what's marked on it, shared by the PSD and the waterfall
/// so they stay aligned. Positions are fractions of the full span, 0 at its low edge; a retune or a new bin
/// count clears them all, since they'd mark other frequencies.
class _SpectrumView {
  static const _maxMarkers = 2;
  // The narrowest view, in bins: past it a zoom only makes the bins wider.
  static const _minViewBins = 8;

  /// The view moved: the plots redraw.
  final zoomChanged = _Changed();
  /// The hover, markers or band changed: only the overlays and readouts redraw.
  final marksChanged = _Changed();

  double lo = 0;
  double hi = 1;
  double? hoverAt;
  _SpectrumPlot? hoverPlot;
  /// Over the waterfall, the row under the pointer, counted back from the newest; null past the oldest.
  int? hoverRowsBack;
  /// Bin centres.
  final List<double> markers = [];
  /// Low edge first.
  (double, double)? band;
  int bins = 0;
  double _freqHz = 0;
  double _rateHz = 0;
  // The band's anchor while a shift-drag measures one; null while a drag pans.
  double? _bandFrom;

  bool get zoomed => lo > 0 || hi < 1;
  double get rateHz => _rateHz;

  double fractionAt(double x, double width) => lo + x.clamp(0, width) / width * (hi - lo);
  double xOf(double fraction, double width) => (fraction - lo) / (hi - lo) * width;
  double freqOf(double fraction) => _freqHz - _rateHz / 2 + fraction * _rateHz;
  int binOf(double fraction) => (fraction * bins).floor().clamp(0, math.max(0, bins - 1));
  double _centreOf(int bin) => (bin + 0.5) / bins;

  /// Starts over if the rows now cover other frequencies.
  void follow(double freqHz, double rateHz, int bins) {
    if (freqHz == _freqHz && rateHz == _rateHz && bins == this.bins) return;
    _freqHz = freqHz;
    _rateHz = rateHz;
    this.bins = bins;
    markers.clear();
    band = null;
    _setView(0, 1);
  }

  /// Zooms by `factor` (below 1 in) keeping `fraction` where it is.
  void zoomAbout(double fraction, double factor) {
    final span = ((hi - lo) * factor).clamp(_minSpan, 1.0);
    _setView(fraction - (fraction - lo) / (hi - lo) * span, span);
  }

  void zoomToBand() {
    final b = band;
    if (b == null) return;
    final span = math.max(b.$2 - b.$1, _minSpan);
    _setView((b.$1 + b.$2) / 2 - span / 2, span);
  }

  void fullSpan() => _setView(0, 1);

  double get _minSpan => math.min(1.0, _minViewBins / math.max(bins, 1));

  void _setView(double left, double span) {
    lo = left.clamp(0.0, 1 - span);
    hi = lo + span;
    zoomChanged.notify();
    marksChanged.notify();
  }

  void dragStart(double fraction, {required bool measure}) {
    _bandFrom = measure ? fraction : null;
    if (measure) _setBand(fraction, fraction);
  }

  /// `dx` is the drag's step as a fraction of the plot's width.
  void dragUpdate(double fraction, double dx) {
    final from = _bandFrom;
    from != null ? _setBand(from, fraction) : _setView(lo - dx * (hi - lo), hi - lo);
  }

  void dragEnd() {
    final b = band;
    // Narrower than a bin is a slip, not a band.
    if (_bandFrom != null && b != null && binOf(b.$1) == binOf(b.$2)) clearBand();
    _bandFrom = null;
  }

  void _setBand(double a, double b) {
    band = a <= b ? (a, b) : (b, a);
    marksChanged.notify();
  }

  void clearBand() {
    band = null;
    marksChanged.notify();
  }

  /// A new marker, at the bin centre under `fraction`; with both placed, the nearer one moves there.
  void placeMarker(double fraction) {
    if (bins == 0) return;
    final at = _centreOf(binOf(fraction));
    if (markers.length < _maxMarkers) {
      markers.add(at);
    } else {
      final nearest = (markers[0] - at).abs() <= (markers[1] - at).abs() ? 0 : 1;
      markers[nearest] = at;
    }
    marksChanged.notify();
  }

  void clearMarkers() {
    markers.clear();
    marksChanged.notify();
  }

  void hover(_SpectrumPlot plot, double fraction, int? rowsBack) {
    hoverPlot = plot;
    hoverAt = fraction;
    hoverRowsBack = rowsBack;
    marksChanged.notify();
  }

  void unhover() {
    hoverAt = null;
    hoverPlot = null;
    hoverRowsBack = null;
    marksChanged.notify();
  }

  void dispose() {
    zoomChanged.dispose();
    marksChanged.dispose();
  }
}

/// The spectrum plots' mouse handling, and the readouts of what's under the pointer and marked.
extension _SpectrumInteraction on _FrameTablePageState {
  /// `child`, one of the plots, under the hover crosshair, markers and band, and taking the mouse: the wheel
  /// zooms, a drag pans, a shift-drag measures a band, a click places a marker and a double-click
  /// returns to the full span.
  Widget _spectrumInteractive(_SpectrumPlot plot, Widget child) {
    final view = _spectrumView;
    final theme = Theme.of(context);
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = constraints.biggest;
        double at(Offset p) => view.fractionAt(p.dx, size.width);
        int? rowsBack(Offset p) {
          if (plot != _SpectrumPlot.waterfall || p.dy < 0) return null;
          final back = p.dy ~/ (size.height / _FrameTablePageState._maxSpectrumRows);
          return back < _spectrumHistory.value.length ? back : null;
        }

        return Listener(
          onPointerSignal: (event) {
            if (event is! PointerScrollEvent) return;
            GestureBinding.instance.pointerSignalResolver.register(event, (event) {
              final scroll = event as PointerScrollEvent;
              view.zoomAbout(at(scroll.localPosition), math.exp(scroll.scrollDelta.dy / 400));
            });
          },
          child: MouseRegion(
            cursor: SystemMouseCursors.precise,
            onHover: (e) => view.hover(plot, at(e.localPosition), rowsBack(e.localPosition)),
            onExit: (_) => view.unhover(),
            child: GestureDetector(
              onTapUp: (d) => view.placeMarker(at(d.localPosition)),
              onDoubleTap: view.fullSpan,
              onHorizontalDragStart: (d) =>
                  view.dragStart(at(d.localPosition), measure: HardwareKeyboard.instance.isShiftPressed),
              onHorizontalDragUpdate: (d) => view.dragUpdate(at(d.localPosition), d.delta.dx / size.width),
              onHorizontalDragEnd: (_) => view.dragEnd(),
              child: Stack(
                fit: StackFit.expand,
                children: [
                  RepaintBoundary(child: child),
                  RepaintBoundary(
                    child: CustomPaint(
                      painter: _SpectrumOverlayPainter(
                        view: view,
                        plot: plot,
                        color: plot == _SpectrumPlot.waterfall ? Colors.white : theme.colorScheme.onSurface,
                        labelStyle: theme.textTheme.labelSmall!,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  /// The edge frequencies of the view, then a line each for the hover, the markers and the band.
  Widget _buildSpectrumReadout(List<_WaterfallRow> rows) {
    final view = _spectrumView;
    return ListenableBuilder(
      listenable: Listenable.merge([view.marksChanged, _spectrumHolds]),
      builder: (context, _) {
        final newest = rows.last;
        final bins = newest.binsDb.length;
        // A row from before a retune would be read at the wrong frequencies.
        double? levelAt(_WaterfallRow row, int bin) =>
            row.binsDb.length == bins && bin < bins ? row.binsDb[bin] - row.floorDb : null;
        String freq(double fraction) => '${(view.freqOf(fraction) / 1e6).toStringAsFixed(4)} MHz';

        final hoverAt = view.hoverAt;
        String hover = 'Wheel zooms, drag pans, shift-drag measures a band, click marks, double-click unzooms';
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
        final b = view.band;
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
            line(hover, [if (view.zoomed) action(Icons.zoom_out_map, 'Full span', view.fullSpan)]),
            if (markers != null) line(markers, [action(Icons.close, 'Clear the markers', view.clearMarkers)]),
            if (band != null)
              line(band, [
                action(Icons.zoom_in, 'Zoom to the band', view.zoomToBand),
                action(Icons.close, 'Clear the band', view.clearBand),
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
    final band = view.band;
    if (band != null) {
      final rect = Rect.fromLTRB(view.xOf(band.$1, w), 0, view.xOf(band.$2, w), size.height);
      canvas.drawRect(rect, Paint()..color = color.withValues(alpha: 0.12));
      final edge = Paint()..color = color.withValues(alpha: 0.6);
      canvas.drawLine(rect.topLeft, rect.bottomLeft, edge);
      canvas.drawLine(rect.topRight, rect.bottomRight, edge);
    }

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
