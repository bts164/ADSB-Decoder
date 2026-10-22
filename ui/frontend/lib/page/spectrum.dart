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
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _buildSpectrumControls(),
          const SizedBox(height: 4),
          Expanded(
            child: ValueListenableBuilder<List<_WaterfallRow>>(
              valueListenable: _spectrumHistory,
              builder: (context, rows, _) {
                if (rows.isEmpty) {
                  return const Center(child: Text('No spectrum data yet'));
                }
                final latest = rows.last;
                final halfSpanMhz = latest.rateHz / 2 / 1e6;
                final centerMhz = latest.freqHz / 1e6;
                return Column(
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
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  /// The colormap, the color range in dB above the noise floor, and a color bar of the two. The range follows
  /// a drag at once, recoloring the history, and is saved when the drag ends.
  Widget _buildSpectrumControls() {
    String db(double v) => '${v > 0 ? '+' : ''}${v.round()} dB';
    return ValueListenableBuilder<RangeValues>(
      valueListenable: _spectrumRange,
      builder: (context, range, _) => Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              DropdownButton<SpectrumColormap>(
                value: _units.spectrumColormap,
                isDense: true,
                items: [
                  for (final c in SpectrumColormap.values) DropdownMenuItem(value: c, child: Text(c.label)),
                ],
                onChanged: (c) {
                  if (c == null) return;
                  _units.spectrumColormap = c;
                  _recolorSpectrum();
                },
              ),
              Expanded(
                child: RangeSlider(
                  values: range,
                  min: DisplaySettings.spectrumMinDb,
                  max: DisplaySettings.spectrumMaxDb,
                  divisions: (DisplaySettings.spectrumMaxDb - DisplaySettings.spectrumMinDb).round(),
                  labels: RangeLabels(db(range.start), db(range.end)),
                  onChanged: (v) {
                    _spectrumRange.value = v;
                    _recolorSpectrum();
                  },
                  onChangeEnd: (v) => _units.setSpectrumRange(v.start, v.end),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.restart_alt),
                tooltip: 'Reset the color range',
                visualDensity: VisualDensity.compact,
                onPressed: () {
                  const defaults =
                      RangeValues(DisplaySettings.defaultSpectrumLowDb, DisplaySettings.defaultSpectrumHighDb);
                  _spectrumRange.value = defaults;
                  _units.setSpectrumRange(defaults.start, defaults.end);
                  _recolorSpectrum();
                },
              ),
            ],
          ),
          Tooltip(
            message: 'Colors by dB above the noise floor',
            child: Row(
              children: [
                Text(db(range.start)),
                const SizedBox(width: 8),
                Expanded(
                  child: Container(
                    height: 8,
                    decoration: BoxDecoration(gradient: LinearGradient(colors: _units.spectrumColormap.stops)),
                  ),
                ),
                const SizedBox(width: 8),
                Text(db(range.end)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// One waterfall row: a peak-hold spectrum of the raw pre-resample IQ
/// (api.SpectrumRow), colored into a one-pixel-high image as it arrives, and
/// again whenever the colors change. Its bins are fftshifted, so the image
/// runs from freqHz - rateHz/2 on the left to freqHz + rateHz/2 on the right.
class _WaterfallRow {
  final ui.Image image;
  final Float32List binsDb;
  /// The noise floor when the row arrived, which its colors stay relative to.
  final double floorDb;
  final double freqHz;
  final double rateHz;

  _WaterfallRow._(this.image, this.binsDb, this.floorDb, this.freqHz, this.rateHz);

  // RGBA for each of 256 levels, per colormap, built when first used.
  static final Map<SpectrumColormap, Uint8List> _palettes = {};

  static Uint8List _palette(SpectrumColormap colormap) => _palettes.putIfAbsent(colormap, () {
        final stops = colormap.stops;
        final p = Uint8List(256 * 4);
        for (int i = 0; i < 256; i++) {
          final scaled = i / 255 * (stops.length - 1);
          final j = scaled.floor().clamp(0, stops.length - 2);
          final c = Color.lerp(stops[j], stops[j + 1], scaled - j)!;
          p[i * 4] = (c.r * 255).round();
          p[i * 4 + 1] = (c.g * 255).round();
          p[i * 4 + 2] = (c.b * 255).round();
          p[i * 4 + 3] = 255;
        }
        return p;
      });

  /// The median bin, which a row's bursts are too narrow to move: the noise floor.
  static double medianDb(List<double> bins) {
    final sorted = List.of(bins)..sort();
    return sorted[sorted.length ~/ 2];
  }

  /// Colors `row` against `floorDb`, spanning `range` dB above it.
  static Future<_WaterfallRow> fromProto(
          api.SpectrumRow row, double floorDb, SpectrumColormap colormap, RangeValues range) =>
      _color(Float32List.fromList(row.binsDb), floorDb, row.freqHz, row.rateHz, colormap, range);

  /// This row in new colors. The caller disposes of this row's image.
  Future<_WaterfallRow> recolored(SpectrumColormap colormap, RangeValues range) =>
      _color(binsDb, floorDb, freqHz, rateHz, colormap, range);

  static Future<_WaterfallRow> _color(Float32List bins, double floorDb, double freqHz, double rateHz,
      SpectrumColormap colormap, RangeValues range) {
    final palette = _palette(colormap);
    final low = floorDb + range.start;
    // The slider can put both ends together; that colors by which side of the one level each bin is.
    final scale = 255 / math.max(range.end - range.start, 0.01);
    final pixels = Uint8List(bins.length * 4);
    for (int i = 0; i < bins.length; i++) {
      final level = ((bins[i] - low) * scale).round().clamp(0, 255);
      pixels.setRange(i * 4, i * 4 + 4, palette, level * 4);
    }
    final done = Completer<_WaterfallRow>();
    ui.decodeImageFromPixels(pixels, bins.length, 1, ui.PixelFormat.rgba8888,
        (image) => done.complete(_WaterfallRow._(image, bins, floorDb, freqHz, rateHz)));
    return done.future;
  }
}

/// Paints `rows` (oldest first) as a scrolling image: each row is one
/// horizontal line, newest at the top. A dashed line marks the center
/// frequency so an off-tuned signal is visually obvious.
class _WaterfallPainter extends CustomPainter {
  final List<_WaterfallRow> rows;

  _WaterfallPainter(this.rows);

  @override
  void paint(Canvas canvas, Size size) {
    if (rows.isEmpty || size.height <= 0 || size.width <= 0) return;

    // Mipmapped, since there are usually more bins than pixels across.
    final rowPaint = Paint()
      ..filterQuality = FilterQuality.medium
      ..isAntiAlias = false;
    final rowHeight = size.height / _maxSpectrumRowsHint;
    for (int i = 0; i < rows.length; i++) {
      final image = rows[i].image;
      // rows[] is oldest-first; the newest row (last index) is drawn at the
      // top and each older one pushed further down, like a real waterfall
      // scrolling downward as time passes.
      final y = (rows.length - 1 - i) * rowHeight;
      if (y > size.height) continue;
      canvas.drawImageRect(image, Rect.fromLTWH(0, 0, image.width.toDouble(), 1),
          Rect.fromLTWH(0, y, size.width, rowHeight), rowPaint);
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
