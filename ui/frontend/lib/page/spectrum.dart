part of 'frame_table_page.dart';

/// The spectrum waterfall in the dock.
extension _Spectrum on _FrameTablePageState {
  /// The raw pre-resample IQ spectrum, as a PSD over a scrolling waterfall
  /// -- lets you see at a glance whether the tuner is centered on 1090 MHz
  /// (the signal should sit on the dashed center line) and how noisy the band
  /// is outside the ADS-B channel itself. Rebuilds only on new spectrum data
  /// (via ValueListenableBuilder), independent of the once-a-second frame/
  /// aircraft flush.
  Widget _buildSpectrumPane() {
    final theme = Theme.of(context);
    return Stack(
      children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: ValueListenableBuilder<List<_WaterfallRow>>(
            valueListenable: _spectrumHistory,
            builder: (context, rows, _) {
              if (rows.isEmpty) {
                return const Center(child: Text('No spectrum data yet'));
              }
              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Expanded(
                    flex: 2,
                    child: ClipRect(
                      child: _spectrumInteractive(
                        _SpectrumPlot.psd,
                        ListenableBuilder(
                          listenable: _spectrumHolds,
                          builder: (context, _) => CustomPaint(
                            size: Size.infinite,
                            painter: _PsdPainter(
                              rows: rows,
                              view: _spectrumView,
                              holds: _spectrumHolds,
                              range: _spectrumRange.value,
                              persistRows: _units.spectrumPersistRows,
                              colormap: _units.spectrumPsdColored ? _units.spectrumColormap : null,
                              traceColor: theme.colorScheme.onSurface,
                              gridColor: theme.colorScheme.outlineVariant,
                              labelStyle: theme.textTheme.labelSmall!
                                  .copyWith(color: theme.colorScheme.onSurfaceVariant),
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(height: 4),
                  _buildSpectrumReadout(rows),
                  Expanded(
                    flex: 3,
                    child: ClipRect(
                      child: _spectrumInteractive(
                        _SpectrumPlot.waterfall,
                        CustomPaint(
                          size: Size.infinite,
                          painter: _WaterfallPainter(rows, _spectrumView),
                        ),
                      ),
                    ),
                  ),
                ],
              );
            },
          ),
        ),
        if (_spectrumControlsShown)
          Positioned(
            // Under the PSD's mode bar, so its button can close the panel again.
            top: 52,
            right: 4,
            left: 4,
            child: Align(
              alignment: Alignment.topRight,
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 480),
                child: Material(
                  elevation: 6,
                  borderRadius: BorderRadius.circular(8),
                  color: theme.colorScheme.surfaceContainerHigh,
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(12, 4, 4, 8),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Row(
                          children: [
                            Text('Display', style: theme.textTheme.titleSmall),
                            const Spacer(),
                            IconButton(
                              icon: const Icon(Icons.close, size: 18),
                              tooltip: 'Hide the display settings',
                              visualDensity: VisualDensity.compact,
                              onPressed: () => _showSpectrumControls(false),
                            ),
                          ],
                        ),
                        _buildSpectrumControls(),
                        _buildHoldControls(),
                        _buildPsdControls(),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
      ],
    );
  }

  void _showSpectrumControls(bool shown) {
    _spectrumControlsShown = shown;
    _aircraftChanged.notify();
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

  /// The PSD's persistence, and whether its traces take the colormap's colors.
  Widget _buildPsdControls() {
    const choices = DisplaySettings.spectrumPersistChoices;
    final index = choices.indexOf(_units.spectrumPersistRows);
    String rows(int n) => n == 0 ? 'off' : '$n rows';
    return Row(
      children: [
        const Tooltip(
          message: 'How many rows behind the newest trace stay shown, fading with age; a row is up to 50 ms',
          child: Text('Persistence'),
        ),
        Expanded(
          child: Slider(
            value: (index < 0 ? choices.indexOf(DisplaySettings.defaultSpectrumPersistRows) : index).toDouble(),
            max: (choices.length - 1).toDouble(),
            divisions: choices.length - 1,
            label: rows(_units.spectrumPersistRows),
            onChanged: (v) => _units.spectrumPersistRows = choices[v.round()],
          ),
        ),
        SizedBox(width: 56, child: Text(rows(_units.spectrumPersistRows))),
        FilterChip(
          avatar: const Icon(Icons.palette_outlined, size: 16),
          label: const Text('Color by level'),
          selected: _units.spectrumPsdColored,
          showCheckmark: false,
          visualDensity: VisualDensity.compact,
          tooltip: 'Color the traces by level with the colormap, as the waterfall is',
          onSelected: (v) => _units.spectrumPsdColored = v,
        ),
      ],
    );
  }

  /// A toggle for each of the PSD's hold traces, and a reset for all of them.
  Widget _buildHoldControls() {
    return ListenableBuilder(
      listenable: _spectrumHolds,
      builder: (context, _) => Row(
        children: [
          const Text('Hold'),
          const SizedBox(width: 8),
          for (final hold in _SpectrumHold.values) ...[
            FilterChip(
              avatar: Icon(Icons.show_chart, size: 16, color: hold.color),
              label: Text(hold.label),
              selected: _spectrumHolds.shown.contains(hold),
              showCheckmark: false,
              visualDensity: VisualDensity.compact,
              tooltip: hold.description,
              onSelected: (_) => _spectrumHolds.toggle(hold),
            ),
            const SizedBox(width: 6),
          ],
          IconButton(
            icon: const Icon(Icons.restart_alt),
            tooltip: 'Restart the holds',
            visualDensity: VisualDensity.compact,
            onPressed: _spectrumHolds.count == 0 ? null : _spectrumHolds.reset,
          ),
          const Spacer(),
          if (_spectrumHolds.count > 0) Text('${_spectrumHolds.count} rows'),
        ],
      ),
    );
  }
}

/// The PSD's hold traces.
enum _SpectrumHold {
  max('Max', 'Each bin\'s highest level since the holds restarted', Colors.orange),
  min('Min', 'Each bin\'s lowest level since the holds restarted', Colors.lightBlue),
  avg('Avg', 'Each bin\'s average power since the holds restarted', Colors.lightGreen);

  const _SpectrumHold(this.label, this.description, this.color);
  final String label;
  final String description;
  final Color color;
}

/// Each bin's highest, lowest and average level over the rows since the last reset or retune, and which of
/// them the PSD shows. Kept whether shown or not, so a hold turned on shows everything since the reset.
class _SpectrumHolds extends ChangeNotifier {
  final Set<_SpectrumHold> shown = {};
  int count = 0;
  Float32List? _maxDb;
  Float32List? _minDb;
  // Linear power, so the average is of power, as a spectrum analyser's is, rather than of dB.
  Float64List? _powerSum;
  double _freqHz = 0;
  double _rateHz = 0;

  void add(List<double> binsDb, double freqHz, double rateHz) {
    final maxDb = _maxDb, minDb = _minDb, powerSum = _powerSum;
    // A retune or a new FFT size starts the holds over: old bins would be other frequencies.
    if (maxDb == null || minDb == null || powerSum == null || maxDb.length != binsDb.length ||
        freqHz != _freqHz || rateHz != _rateHz) {
      _maxDb = Float32List.fromList(binsDb);
      _minDb = Float32List.fromList(binsDb);
      _powerSum = Float64List.fromList([for (final db in binsDb) math.pow(10, db / 10).toDouble()]);
      _freqHz = freqHz;
      _rateHz = rateHz;
      count = 1;
      return;
    }
    for (var i = 0; i < binsDb.length; i++) {
      final db = binsDb[i];
      if (db > maxDb[i]) maxDb[i] = db;
      if (db < minDb[i]) minDb[i] = db;
      powerSum[i] += math.pow(10, db / 10);
    }
    count++;
  }

  /// The hold's level in each bin, in dB; null before any row.
  List<double>? levels(_SpectrumHold hold) {
    final powerSum = _powerSum;
    return switch (hold) {
      _SpectrumHold.max => _maxDb,
      _SpectrumHold.min => _minDb,
      _SpectrumHold.avg =>
        powerSum == null ? null : [for (final p in powerSum) 10 * math.log(p / count) / math.ln10],
    };
  }

  void toggle(_SpectrumHold hold) {
    if (!shown.remove(hold)) shown.add(hold);
    notifyListeners();
  }

  void reset() {
    _maxDb = null;
    _minDb = null;
    _powerSum = null;
    count = 0;
    notifyListeners();
  }
}

/// The latest row as a trace over the rows just before it, fading with age, so a short burst lingers, and the
/// shown holds over both. The levels run over the color range above the newest row's noise floor, so a
/// trace's height matches its color in the waterfall below, bin for bin.
class _PsdPainter extends CustomPainter {
  _PsdPainter({
    required this.rows,
    required this.view,
    required this.holds,
    required this.range,
    required this.persistRows,
    required this.colormap,
    required this.traceColor,
    required this.gridColor,
    required this.labelStyle,
  }) : super(repaint: view.zoomChanged);

  /// Oldest first.
  final List<_WaterfallRow> rows;
  final _SpectrumView view;
  final _SpectrumHolds holds;
  final RangeValues range;
  /// Rows drawn behind the newest.
  final int persistRows;
  /// Colors each trace by level when given; otherwise they're all `traceColor`.
  final SpectrumColormap? colormap;
  final Color traceColor;
  final Color gridColor;
  final TextStyle labelStyle;

  @override
  void paint(Canvas canvas, Size size) {
    if (rows.isEmpty || size.width <= 0 || size.height <= 0) return;
    final floorDb = rows.last.floorDb;
    final low = floorDb + range.start;
    final span = math.max(range.end - range.start, 1.0);
    double y(double db) => size.height - (db - low) / span * size.height;

    final grid = Paint()..color = gridColor;
    final step = niceStep(span, 4);
    for (var above = (range.start / step).ceil() * step; above <= range.start + span + 1e-9; above += step) {
      final at = y(floorDb + above);
      canvas.drawLine(Offset(0, at), Offset(size.width, at), grid);
      final label = TextPainter(
        text: TextSpan(text: '${above > 0 ? '+' : ''}${above.round()} dB', style: labelStyle),
        textDirection: TextDirection.ltr,
      )..layout();
      label.paint(canvas, Offset(2, math.max(0, at - label.height)));
      label.dispose();
    }

    final columns = size.width.ceil();
    final recent = rows.length > persistRows + 1 ? rows.sublist(rows.length - persistRows - 1) : rows;
    for (var i = 0; i < recent.length; i++) {
      final newest = i == recent.length - 1;
      final paint = _tracePaint(size, newest ? 1.5 : 1, newest ? 1 : 0.05 + 0.3 * (i + 1) / recent.length);
      canvas.drawPath(_trace(recent[i].binsDb, columns, size, y), paint);
    }
    for (final hold in _SpectrumHold.values) {
      final levels = holds.shown.contains(hold) ? holds.levels(hold) : null;
      if (levels == null || levels.length != rows.last.binsDb.length) continue;
      final paint = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..color = hold.color;
      canvas.drawPath(_trace(levels, columns, size, y, lowest: hold == _SpectrumHold.min), paint);
    }
  }

  // The plot's y runs over the color range, so a vertical gradient of the colormap gives each point of a trace
  // the color its level has in the waterfall.
  Paint _tracePaint(Size size, double width, double alpha) {
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = width;
    final stops = colormap?.stops;
    if (stops == null) return paint..color = traceColor.withValues(alpha: alpha);
    return paint
      ..shader = LinearGradient(
        begin: Alignment.bottomCenter,
        end: Alignment.topCenter,
        colors: [for (final c in stops) c.withValues(alpha: c.a * alpha)],
      ).createShader(Offset.zero & size);
  }

  // At most one level per pixel column: the highest of the bins it covers, or with `lowest` the lowest, so a
  // burst narrower than a column still shows.
  static List<double> _columns(List<double> bins, int columns, {bool lowest = false}) {
    if (bins.length <= columns) return bins;
    final out = List<double>.filled(columns, 0);
    for (var c = 0; c < columns; c++) {
      final start = c * bins.length ~/ columns, end = (c + 1) * bins.length ~/ columns;
      var v = bins[start];
      for (var i = start + 1; i < end; i++) {
        v = lowest ? math.min(v, bins[i]) : math.max(v, bins[i]);
      }
      out[c] = v;
    }
    return out;
  }

  // The bins in view, each at the centre of its share of the width, as the waterfall stretches each bin across
  // its share. Zoomed in to fewer bins than columns, the bin past each edge is drawn too, so the trace runs off
  // the plot rather than stopping short of its edges.
  ui.Path _trace(List<double> bins, int columns, Size size, double Function(double db) y, {bool lowest = false}) {
    final n = bins.length;
    final first = (view.lo * n).floor(), end = math.min(n, (view.hi * n).ceil());
    final path = ui.Path();
    void point(int i, double x, double db) => i == 0 ? path.moveTo(x, y(db)) : path.lineTo(x, y(db));
    if (end - first > columns) {
      final levels = _columns(bins.sublist(first, end), columns, lowest: lowest);
      for (var i = 0; i < levels.length; i++) {
        point(i, (i + 0.5) / levels.length * size.width, levels[i]);
      }
    } else {
      final from = math.max(0, first - 1);
      for (var i = from; i < math.min(n, end + 1); i++) {
        point(i - from, view.xOf((i + 0.5) / n, size.width), bins[i]);
      }
    }
    return path;
  }

  // Rebuilt for each new row, and the holds change in place.
  @override
  bool shouldRepaint(covariant _PsdPainter oldDelegate) => true;
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
  final _SpectrumView view;

  _WaterfallPainter(this.rows, this.view) : super(repaint: view.zoomChanged);

  @override
  void paint(Canvas canvas, Size size) {
    if (rows.isEmpty || size.height <= 0 || size.width <= 0) return;

    // Mipmapped while there are more bins in view than pixels across; zoomed in past that, each bin a sharp
    // block, as the PSD draws it, rather than blurred into its neighbours.
    final binsInView = rows.last.image.width * (view.hi - view.lo);
    final rowPaint = Paint()
      ..filterQuality = binsInView > size.width ? FilterQuality.medium : FilterQuality.none
      ..isAntiAlias = false;
    final rowHeight = size.height / _maxSpectrumRowsHint;
    for (int i = 0; i < rows.length; i++) {
      final image = rows[i].image;
      // rows[] is oldest-first; the newest row (last index) is drawn at the
      // top and each older one pushed further down, like a real waterfall
      // scrolling downward as time passes.
      final y = (rows.length - 1 - i) * rowHeight;
      if (y > size.height) continue;
      canvas.drawImageRect(image, Rect.fromLTRB(view.lo * image.width, 0, view.hi * image.width, 1),
          Rect.fromLTWH(0, y, size.width, rowHeight), rowPaint);
    }

    final centerPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.6)
      ..strokeWidth = 1;
    final centerX = view.xOf(0.5, size.width);
    if (centerX < 0 || centerX > size.width) return;
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
