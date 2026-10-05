part of 'frame_table_page.dart';

/// The spectrum waterfall in the dock.
extension _Spectrum on _FrameTablePageState {
  /// Scrolling waterfall of the raw pre-resample IQ spectrum -- lets you see
  /// at a glance whether the tuner is centered on 1090 MHz (the signal
  /// should sit on the dashed center line) and how noisy the band is outside
  /// the ADS-B channel itself. Rebuilds only on new spectrum data (via
  /// ValueListenableBuilder), independent of the once-a-second frame/
  /// aircraft flush.
  Widget _buildSpectrumPane() {
    return ValueListenableBuilder<List<SpectrumFrame>>(
      valueListenable: _spectrumHistory,
      builder: (context, rows, _) {
        if (rows.isEmpty) {
          return const Center(child: Text('No spectrum data yet'));
        }
        final latest = rows.last;
        final halfSpanMhz = latest.rateHz / 2 / 1e6;
        final centerMhz = latest.freqHz / 1e6;
        return Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Expanded(
                child: ClipRect(
                  child: CustomPaint(
                    size: Size.infinite,
                    painter: _WaterfallPainter(rows),
                  ),
                ),
              ),
              const SizedBox(height: 4),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text('${(centerMhz - halfSpanMhz).toStringAsFixed(3)} MHz'),
                  Text('${centerMhz.toStringAsFixed(3)} MHz (center)'),
                  Text('${(centerMhz + halfSpanMhz).toStringAsFixed(3)} MHz'),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Paints `rows` (oldest first) as a scrolling image: each row is one
/// horizontal line, newest at the top, colored by magnitude (dB, auto-scaled
/// per paint across all currently-buffered rows so a strong signal doesn't
/// permanently wash out the color range once it passes). A dashed line marks
/// the center frequency so an off-tuned signal is visually obvious.
class _WaterfallPainter extends CustomPainter {
  final List<SpectrumFrame> rows;

  _WaterfallPainter(this.rows);

  static const List<Color> _colormap = [
    Color(0xFF000030),
    Color(0xFF0000C0),
    Color(0xFF00C0C0),
    Color(0xFF00C000),
    Color(0xFFE0E000),
    Color(0xFFE00000),
  ];

  Color _colorFor(double t) {
    final scaled = t.clamp(0.0, 1.0) * (_colormap.length - 1);
    final i = scaled.floor().clamp(0, _colormap.length - 2);
    return Color.lerp(_colormap[i], _colormap[i + 1], scaled - i)!;
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (rows.isEmpty || size.height <= 0 || size.width <= 0) return;

    double minDb = double.infinity;
    double maxDb = -double.infinity;
    for (final r in rows) {
      for (final b in r.bins) {
        if (b < minDb) minDb = b;
        if (b > maxDb) maxDb = b;
      }
    }
    if (maxDb <= minDb) maxDb = minDb + 1;

    final rowHeight = size.height / _maxSpectrumRowsHint;
    for (int i = 0; i < rows.length; i++) {
      final bins = rows[i].bins;
      // rows[] is oldest-first; the newest row (last index) is drawn at the
      // top and each older one pushed further down, like a real waterfall
      // scrolling downward as time passes.
      final y = (rows.length - 1 - i) * rowHeight;
      if (y > size.height) continue;
      final n = bins.length;
      final stops = List<double>.generate(n, (j) => j / (n - 1));
      final colors = [for (final b in bins) _colorFor((b - minDb) / (maxDb - minDb))];
      final paint = Paint()
        ..shader = ui.Gradient.linear(Offset(0, y), Offset(size.width, y), colors, stops);
      canvas.drawRect(Rect.fromLTWH(0, y, size.width, rowHeight + 1), paint);
    }

    final centerPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.6)
      ..strokeWidth = 1;
    final centerX = size.width / 2;
    const dash = 6.0;
    for (double y = 0; y < size.height; y += dash * 2) {
      canvas.drawLine(Offset(centerX, y), Offset(centerX, math.min(y + dash, size.height)), centerPaint);
    }
  }

  // Rows scroll from the bottom up as the buffer fills, rather than
  // stretching to fill the pane immediately -- keeps each row's on-screen
  // height constant instead of changing as more history arrives.
  static const int _maxSpectrumRowsHint = _FrameTablePageState._maxSpectrumRows;

  @override
  bool shouldRepaint(covariant _WaterfallPainter oldDelegate) => !identical(oldDelegate.rows, rows);
}
