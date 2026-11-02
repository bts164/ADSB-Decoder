part of 'frame_table_page.dart';

/// The span of a recording the viewer shows, and what's marked on it, in samples from its start: sample i
/// covers [i, i + 1).
class _RecordingView extends _PlotAxis {
  _RecordingView(int samples, this.rateHz) : super(minCells: 16) {
    reset(samples.toDouble(), samples);
  }

  final double rateHz;

  int get samples => cells;
  int indexOf(double sample) => cellOf(sample);
}

/// A recording's SigMF annotations, in sample order as CaptureInfo has them, and the search for those over a
/// span of samples.
class _Annotations {
  _Annotations(this.list)
      : starts = [for (final a in list) a.start.toInt()],
        longest = list.fold(1, (m, a) => math.max(m, a.count.toInt()));

  final List<api.CaptureAnnotation> list;
  final List<int> starts;
  final int longest;

  int get length => list.length;
  // At least a sample, so an empty one can still be found under the pointer.
  int end(int i) => starts[i] + math.max(1, list[i].count.toInt());

  /// The first that starts at or after `sample`; [length] for none.
  int firstFrom(double sample) {
    var lo = 0, hi = starts.length;
    while (lo < hi) {
      final mid = (lo + hi) ~/ 2;
      starts[mid] < sample ? lo = mid + 1 : hi = mid;
    }
    return lo;
  }

  /// The first that may overlap `sample` or follow it: none before it ends after it.
  int firstOver(double sample) => firstFrom(sample - longest + 1);

  /// Those over sample `i`, in order.
  List<int> at(int i) => [
        for (var k = firstOver(i.toDouble()); k < length && starts[k] <= i; k++)
          if (end(k) > i) k,
      ];
}

/// A recording in the capture directory, plotted (doc/design/iq_viewer.md): OpenCapture's progress while the
/// server builds its summary, then its power over time, or zoomed in far enough its samples. Closes itself when
/// the connection goes, as the Captures dialog under it does.
class _CaptureViewerDialog extends StatefulWidget {
  const _CaptureViewerDialog({required this.client, required this.settings, required this.name});

  final ReceiverClient client;
  final ValueListenable<api.Settings?> settings;
  final String name;

  @override
  State<_CaptureViewerDialog> createState() => _CaptureViewerDialogState();
}

class _CaptureViewerDialogState extends State<_CaptureViewerDialog> {
  // GetCaptureViewRequest's limit.
  static const _maxColumns = 8192;

  late final StreamSubscription<api.OpenCaptureProgress> _open;
  api.OpenCaptureProgress? _progress;
  String? _openError;
  api.CaptureInfo? _info;
  _RecordingView? _view;
  // I and Q rather than the magnitude, once the view is of the samples.
  bool _iq = false;
  double? _plotWidth;
  // One request in flight; the views asked for meanwhile replace each other, and only the latest is sent.
  UnaryCall<api.CaptureView>? _call;
  api.GetCaptureViewRequest? _pending;
  api.CaptureView? _shown;
  String? _viewError;
  // The dB range of the plot, fitted to the whole recording once, so a zoom doesn't rescale it.
  (double, double)? _dbRange;
  _Annotations _annotations = _Annotations(const []);
  bool _annotationsShown = true;

  @override
  void initState() {
    super.initState();
    widget.settings.addListener(_onSettings);
    _open = widget.client.openCapture(api.OpenCaptureRequest(name: widget.name)).listen(
      (p) {
        if (!p.hasCapture()) {
          setState(() => _progress = p);
          return;
        }
        final info = p.capture;
        final view = _RecordingView(info.samples.toInt(), info.rateHz);
        view.zoomChanged.addListener(_ask);
        setState(() {
          _info = info;
          _view = view;
          _annotations = _Annotations(info.annotations);
        });
        _ask();
      },
      onError: (Object e) => setState(() => _openError = e is RpcException ? e.message : '$e'),
      onDone: () {
        if (_info == null && _openError == null) setState(() => _openError = 'the call ended without the recording');
      },
      cancelOnError: true,
    );
  }

  @override
  void dispose() {
    widget.settings.removeListener(_onSettings);
    _open.cancel();
    _pending = null;
    _call?.cancel();
    _view?.dispose();
    super.dispose();
  }

  void _onSettings() {
    // Disconnected: the client's calls would fail.
    if (widget.settings.value == null) Navigator.of(context).pop();
  }

  /// [width] in logical pixels: a column each, as many as the server allows.
  void _plotResized(double width) {
    if (width == _plotWidth) return;
    _plotWidth = width;
    // Not during the build that found the width.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _ask();
    });
  }

  void _ask() {
    final view = _view, width = _plotWidth;
    if (view == null || width == null || width < 1) return;
    final first = view.lo.floor();
    final count = math.max(1, math.min(view.samples, view.hi.ceil()) - first);
    final request = api.GetCaptureViewRequest(
      name: widget.name,
      first: Int64(first),
      count: Int64(count),
      columns: width.round().clamp(1, _maxColumns),
      iq: _iq,
    );
    if (_call != null) {
      _pending = request;
      return;
    }
    _send(request);
  }


  Future<void> _send(api.GetCaptureViewRequest request) async {
    final call = widget.client.getCaptureView(request);
    _call = call;
    try {
      final shown = await call;
      if (!mounted) return;
      setState(() {
        _shown = shown;
        _viewError = null;
        _dbRange ??= _fitDb(shown);
      });
    } on RpcException catch (e) {
      if (!mounted) return;
      setState(() => _viewError = e.message);
    }
    _call = null;
    final next = _pending;
    _pending = null;
    if (next != null) _send(next);
  }

  // The power counted as 0 dB: the full scale's, when it's known.
  double get _ref {
    final info = _info;
    return info != null && info.hasFullScale() ? info.fullScale * info.fullScale : 1;
  }

  String get _unit => _info?.hasFullScale() ?? false ? 'dBFS' : 'dB';

  double _db(double power) => 10 * math.log(math.max(power, 1e-30) / _ref) / math.ln10;

  /// From the highest max to the lowest mean, in whole 10 dB, at least 20 dB apart.
  (double, double) _fitDb(api.CaptureView shown) {
    final List<double> highs, lows;
    if (shown.hasColumns()) {
      highs = [for (final p in shown.columns.maxPower) _db(p)];
      lows = [for (final p in shown.columns.meanPower) _db(p)];
    } else if (!shown.samples.iq) {
      highs = lows = [for (final v in shown.samples.values) _db(v * v)];
    } else {
      final v = shown.samples.values;
      highs = lows = [for (var i = 0; i + 1 < v.length; i += 2) _db(v[i] * v[i] + v[i + 1] * v[i + 1])];
    }
    // Silence reads as -300 dB, which would squash everything else.
    final floor = _db(0) + 1;
    final finiteLows = lows.where((db) => db > floor);
    final high = highs.isEmpty ? 0.0 : (highs.reduce(math.max) / 10).ceil() * 10.0;
    final low = finiteLows.isEmpty ? high - 60 : (finiteLows.reduce(math.min) / 10).floor() * 10.0;
    return (math.min(low, high - 20), high);
  }

  void _setIq(bool iq) {
    setState(() => _iq = iq);
    _ask();
  }

  @override
  Widget build(BuildContext context) {
    final size = MediaQuery.sizeOf(context);
    return Dialog(
      insetPadding: const EdgeInsets.all(24),
      child: SizedBox(
        width: math.min(size.width - 48, 1600),
        height: math.min(size.height - 48, 900),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 8, 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _buildTitle(context),
              const SizedBox(height: 8),
              Expanded(child: _buildBody(context)),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildTitle(BuildContext context) {
    final info = _info;
    final theme = Theme.of(context);
    final about = info == null
        ? null
        : '${info.samples} samples · ${_durationText(info.samples.toDouble() / info.rateHz)} · ${info.datatype} '
            'at ${_CapturesDialogState._hz(info.rateHz)}'
            '${info.hasFullScale() ? '' : ' · full scale unknown, so nothing is marked clipped'}';
    return Row(
      children: [
        Text(widget.name, style: theme.textTheme.titleMedium),
        const SizedBox(width: 12),
        if (about != null)
          Expanded(child: Text(about, style: theme.textTheme.bodySmall, overflow: TextOverflow.ellipsis))
        else
          const Spacer(),
        if (_annotations.length > 0)
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Tooltip(
              message: 'The frames decoded in it, and any other SigMF annotations',
              child: FilterChip(
                label: Text('Annotations (${_annotations.length})'),
                selected: _annotationsShown,
                onSelected: (shown) => setState(() => _annotationsShown = shown),
              ),
            ),
          ),
        if (info != null && info.complex)
          Tooltip(
            message: 'Zoomed in to the samples: I and Q rather than the magnitude',
            child: FilterChip(label: const Text('I/Q'), selected: _iq, onSelected: _setIq),
          ),
        IconButton(icon: const Icon(Icons.close), tooltip: 'Close', onPressed: () => Navigator.pop(context)),
      ],
    );
  }

  Widget _buildBody(BuildContext context) {
    final openError = _openError;
    if (openError != null) return Center(child: Text("Can't open ${widget.name}: $openError"));
    final view = _view;
    if (view == null) {
      final p = _progress;
      final total = p?.samplesTotal.toInt() ?? 0;
      final done = total > 0 ? p!.samplesDone.toInt() / total : null;
      return Center(
        child: SizedBox(
          width: 360,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              LinearProgressIndicator(value: done),
              const SizedBox(height: 8),
              Text(done == null
                  ? 'Opening ${widget.name}…'
                  : 'Summarising ${widget.name}: ${(done * 100).toStringAsFixed(0)}% of $total samples'),
            ],
          ),
        ),
      );
    }
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Expanded(
          child: LayoutBuilder(
            builder: (context, constraints) {
              _plotResized(constraints.maxWidth);
              return _PlotGestures(
                axis: view,
                modeBar: true,
                overlay: _RecordingOverlayPainter(
                  view: view,
                  annotations: _annotationsShown ? _annotations : _Annotations(const []),
                  color: theme.colorScheme.onSurface,
                  frameColor: theme.colorScheme.primary,
                  failedColor: theme.colorScheme.error,
                  otherColor: theme.colorScheme.secondary,
                  labelStyle: theme.textTheme.labelSmall!,
                ),
                child: CustomPaint(
                  painter: _RecordingPainter(
                    view: view,
                    shown: _shown,
                    db: _db,
                    dbRange: _dbRange ?? (-60, 0),
                    unit: _unit,
                    fullScale: _info!.hasFullScale() ? _info!.fullScale : null,
                    traceColor: theme.colorScheme.primary,
                    qColor: theme.colorScheme.tertiary,
                    clipColor: theme.colorScheme.error,
                    gridColor: theme.colorScheme.outlineVariant,
                    labelStyle: theme.textTheme.labelSmall!,
                  ),
                ),
              );
            },
          ),
        ),
        const SizedBox(height: 4),
        _buildReadout(view),
      ],
    );
  }

  /// The edge times of the view, then a line each for the hover, the markers and the measured span.
  Widget _buildReadout(_RecordingView view) {
    return ListenableBuilder(
      listenable: Listenable.merge([view.marksChanged, view.modeChanged]),
      builder: (context, _) {
        String time(double sample) => '${(sample / view.rateHz).toStringAsFixed(7)} s';
        String at(int i) {
          final level = _levelAt(i);
          return '${time(i.toDouble())} (sample $i)${level == null ? '' : ': $level'}';
        }

        final hoverAt = view.hoverAt;
        final hover = hoverAt == null
            ? 'Wheel zooms, ${view.mode.hint}, click marks, double-click shows it all'
            : at(view.indexOf(hoverAt));

        String? annotated;
        if (hoverAt != null && _annotationsShown) {
          final over = _annotations.at(view.indexOf(hoverAt));
          if (over.isNotEmpty) {
            final more = over.length > 1 ? '   +${over.length - 1} more' : '';
            annotated = '${_annotationText(_annotations.list[over.first])}$more';
          }
        }

        String? markers;
        if (view.markers.isNotEmpty) {
          markers = [
            for (final (i, m) in view.markers.indexed) 'M${i + 1} ${at(view.indexOf(m))}',
            if (view.markers.length == 2)
              'Δ ${_durationText((view.markers[1] - view.markers[0]) / view.rateHz)} '
                  '(${(view.markers[1] - view.markers[0]).round()} samples)',
          ].join('   ');
        }

        String? measured;
        final m = view.measured;
        if (m != null) {
          final first = view.indexOf(m.$1), last = view.indexOf(m.$2);
          final stats = _statsOver(first.toDouble(), last + 1.0);
          measured = 'Span ${_durationText((last - first + 1) / view.rateHz)}, ${last - first + 1} samples '
              'from ${time(first.toDouble())}${stats == null ? '' : ': $stats'}';
        }

        final viewError = _viewError;
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
                Text(time(view.lo)),
                Text('span ${_durationText((view.hi - view.lo) / view.rateHz)}'),
                Text(time(view.hi)),
              ],
            ),
            if (viewError != null)
              line("Can't show this view: $viewError", [])
            else
              line(hover, []),
            // Kept while blank, so the plot doesn't change height as the pointer crosses an annotation.
            if (_annotationsShown && _annotations.length > 0) line(annotated ?? '', []),
            if (markers != null) line(markers, [action(Icons.close, 'Clear the markers', view.clearMarkers)]),
            if (measured != null)
              line(measured, [
                action(Icons.zoom_in, 'Zoom to the span', view.zoomToMeasured),
                action(Icons.close, 'Clear the span', view.clearMeasured),
              ]),
          ],
        );
      },
    );
  }

  /// What an annotation says: for a frame, what was decoded of it.
  String _annotationText(api.CaptureAnnotation a) {
    final view = _view!;
    final from = 'at ${(a.start.toDouble() / view.rateHz).toStringAsFixed(7)} s, '
        '${_durationText(a.count.toDouble() / view.rateHz)}';
    if (!a.hasFrame()) return '${a.label.isEmpty ? 'Annotation' : a.label} $from';
    final f = a.frame;
    final crc = !f.crcChecked
        ? 'no CRC'
        : !f.crcOk
            ? 'CRC failed'
            : f.hasCrcFixedBit()
                ? 'CRC ok, bit ${f.crcFixedBit} fixed'
                : 'CRC ok';
    return [
      a.label,
      if (f.hasIcao()) icaoHex(f.icao),
      crc,
      'confidence ${f.confidence.toStringAsFixed(2)}',
      [for (final b in f.payload) b.toRadixString(16).padLeft(2, '0')].join(),
      from,
    ].join(' · ');
  }

  String _dbText(double power) => '${_db(power).toStringAsFixed(1)} $_unit';

  /// The level at sample `i` in the view shown, which lags the one asked for; null outside it.
  String? _levelAt(int i) {
    final shown = _shown;
    if (shown == null) return null;
    final first = shown.first.toInt(), count = shown.count.toInt();
    if (i < first || i >= first + count) return null;
    if (shown.hasColumns()) {
      final c = shown.columns;
      final n = c.maxPower.length;
      if (n == 0) return null;
      final col = ((i - first) * n ~/ count).clamp(0, n - 1);
      final clipped = col < c.clipped.length ? c.clipped[col] : 0;
      return 'max ${_dbText(c.maxPower[col])}, mean ${_dbText(c.meanPower[col])}'
          '${clipped > 0 ? ', $clipped clipped' : ''}';
    }
    final v = shown.samples.values;
    final k = i - first;
    if (shown.samples.iq) {
      if (2 * k + 1 >= v.length) return null;
      final re = v[2 * k], im = v[2 * k + 1];
      return 'I ${re.toStringAsPrecision(4)}, Q ${im.toStringAsPrecision(4)}, ${_dbText(re * re + im * im)}';
    }
    return k < v.length ? '${v[k].toStringAsPrecision(4)}, ${_dbText(v[k] * v[k])}' : null;
  }

  /// The peak and mean power over the samples from `from` to `to` in the view shown: over the columns that
  /// overlap them when it's columns.
  String? _statsOver(double from, double to) {
    final shown = _shown;
    if (shown == null) return null;
    final first = shown.first.toDouble(), count = shown.count.toDouble();
    var peak = 0.0, sum = 0.0;
    var n = 0;
    if (shown.hasColumns()) {
      final c = shown.columns;
      final columns = c.maxPower.length;
      for (var i = 0; i < columns; i++) {
        final start = first + i * count / columns, end = first + (i + 1) * count / columns;
        if (end <= from || start >= to) continue;
        peak = math.max(peak, c.maxPower[i]);
        sum += c.meanPower[i];
        n++;
      }
    } else {
      final v = shown.samples.values;
      final iq = shown.samples.iq;
      final samples = iq ? v.length ~/ 2 : v.length;
      for (var k = 0; k < samples; k++) {
        if (first + k + 1 <= from || first + k >= to) continue;
        final p = iq ? v[2 * k] * v[2 * k] + v[2 * k + 1] * v[2 * k + 1] : v[k] * v[k];
        peak = math.max(peak, p);
        sum += p;
        n++;
      }
    }
    return n == 0 ? null : 'peak ${_dbText(peak)}, mean ${_dbText(sum / n)}';
  }

  static String _durationText(double s) {
    final a = s.abs();
    if (a >= 1) return '${s.toStringAsFixed(3)} s';
    if (a >= 1e-3) return '${(s * 1e3).toStringAsFixed(3)} ms';
    return '${(s * 1e6).toStringAsFixed(2)} µs';
  }
}

/// The view a GetCaptureView call returned, drawn where its span is in the current view, so a pan or zoom moves
/// it at once and the reply for the new span fills it in. Columns are the max and mean power, with the clipped
/// ones marked at the top; samples are their power, or with I/Q their values against the full scale.
class _RecordingPainter extends CustomPainter {
  _RecordingPainter({
    required this.view,
    required this.shown,
    required this.db,
    required this.dbRange,
    required this.unit,
    required this.fullScale,
    required this.traceColor,
    required this.qColor,
    required this.clipColor,
    required this.gridColor,
    required this.labelStyle,
  }) : super(repaint: view.zoomChanged);

  final _RecordingView view;
  final api.CaptureView? shown;
  final double Function(double power) db;
  final (double, double) dbRange;
  final String unit;
  final double? fullScale;
  final Color traceColor;
  final Color qColor;
  final Color clipColor;
  final Color gridColor;
  final TextStyle labelStyle;

  // Samples closer than this, in pixels, are drawn as a line alone.
  static const _dotSpacing = 4.0;

  @override
  void paint(Canvas canvas, Size size) {
    final shown = this.shown;
    if (shown == null || size.width <= 0 || size.height <= 0) return;
    canvas.clipRect(Offset.zero & size);
    final first = shown.first.toDouble(), count = shown.count.toDouble();
    if (shown.hasSamples() && shown.samples.iq) {
      _paintIq(canvas, size, first, shown.samples.values);
      return;
    }

    final (low, high) = dbRange;
    double yDb(double level) => size.height - (level - low) / (high - low) * size.height;
    double y(double power) => yDb(db(power));
    final step = niceStep(high - low, 4);
    _grid(canvas, size, [
      for (var at = (low / step).ceil() * step; at <= high + 1e-9; at += step) (yDb(at), '${at.round()} $unit'),
    ]);

    final w = size.width;
    if (shown.hasColumns()) {
      final c = shown.columns;
      final n = c.maxPower.length;
      final maxPath = ui.Path(), meanPath = ui.Path();
      final clip = Paint()..color = clipColor;
      var started = false;
      for (var i = 0; i < n; i++) {
        final x0 = view.xOf(first + i * count / n, w), x1 = view.xOf(first + (i + 1) * count / n, w);
        if (x1 < 0 || x0 > w) continue;
        final yMax = y(c.maxPower[i]), yMean = y(c.meanPower[i]);
        if (started) {
          maxPath.lineTo(x0, yMax);
          meanPath.lineTo(x0, yMean);
        } else {
          maxPath.moveTo(x0, yMax);
          meanPath.moveTo(x0, yMean);
          started = true;
        }
        maxPath.lineTo(x1, yMax);
        meanPath.lineTo(x1, yMean);
        if (i < c.clipped.length && c.clipped[i] > 0) {
          canvas.drawRect(Rect.fromLTRB(x0, 0, math.max(x1, x0 + 1), 4), clip);
        }
      }
      canvas.drawPath(maxPath, _stroke(traceColor.withValues(alpha: 0.45)));
      canvas.drawPath(meanPath, _stroke(traceColor));
      return;
    }

    final v = shown.samples.values;
    _paintSamples(canvas, size, first, [for (var k = 0; k < v.length; k++) (k, y(v[k] * v[k]))], traceColor);
  }

  // I and Q, linear, against the full scale, or without one the largest value shown.
  void _paintIq(Canvas canvas, Size size, double first, List<double> v) {
    final scale = fullScale ?? v.fold<double>(1e-12, (a, x) => math.max(a, x.abs()));
    final mid = size.height / 2;
    double y(double value) => mid - value / scale * mid * 0.95;
    String label(double value) => value.toStringAsPrecision(3);
    _grid(canvas, size, [(y(scale), label(scale)), (mid, '0'), (y(-scale), label(-scale))]);
    final samples = v.length ~/ 2;
    _paintSamples(canvas, size, first, [for (var k = 0; k < samples; k++) (k, y(v[2 * k]))], traceColor);
    _paintSamples(canvas, size, first, [for (var k = 0; k < samples; k++) (k, y(v[2 * k + 1]))], qColor);
  }

  void _paintSamples(Canvas canvas, Size size, double first, List<(int, double)> points, Color color) {
    final path = ui.Path();
    final dots = size.width / (view.hi - view.lo) >= _dotSpacing;
    final dot = Paint()..color = color;
    for (final (k, y) in points) {
      final x = view.xOf(first + k + 0.5, size.width);
      k == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
      if (dots) canvas.drawCircle(Offset(x, y), 2, dot);
    }
    canvas.drawPath(path, _stroke(color));
  }

  void _grid(Canvas canvas, Size size, List<(double, String)> lines) {
    final grid = Paint()..color = gridColor;
    for (final (at, text) in lines) {
      canvas.drawLine(Offset(0, at), Offset(size.width, at), grid);
      final label = TextPainter(text: TextSpan(text: text, style: labelStyle), textDirection: TextDirection.ltr)
        ..layout();
      label.paint(canvas, Offset(2, (at - label.height).clamp(0, size.height - label.height)));
      label.dispose();
    }
  }

  static Paint _stroke(Color color) => Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = 1.25
    ..color = color;

  @override
  bool shouldRepaint(_RecordingPainter old) =>
      old.view != view ||
      old.shown != shown ||
      old.dbRange != dbRange ||
      old.unit != unit ||
      old.fullScale != fullScale ||
      old.traceColor != traceColor ||
      old.qColor != qColor ||
      old.clipColor != clipColor ||
      old.gridColor != gridColor ||
      old.labelStyle != labelStyle;
}

/// Over the plot: the annotations, the measured span, the markers and the hover line. Repaints on its own as they
/// change, without the plot under it.
class _RecordingOverlayPainter extends CustomPainter {
  _RecordingOverlayPainter({
    required this.view,
    required this.annotations,
    required this.color,
    required this.frameColor,
    required this.failedColor,
    required this.otherColor,
    required this.labelStyle,
  }) : super(repaint: Listenable.merge([view.zoomChanged, view.marksChanged]));

  final _RecordingView view;
  final _Annotations annotations;
  final Color color;
  final Color frameColor;
  final Color failedColor;
  final Color otherColor;
  final TextStyle labelStyle;

  // The bar under each annotation, at the plot's foot.
  static const _barHeight = 6.0;

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    // A CustomPaint isn't clipped to its size, and an annotation panned partly out of view is drawn whole.
    canvas.clipRect(Offset.zero & size);
    _paintAnnotations(canvas, size);
    _paintAxisSpans(canvas, size, view, color);

    for (final (i, marker) in view.markers.indexed) {
      final x = view.xOf(marker, w);
      if (x < 0 || x > w) continue;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), Paint()..color = color.withValues(alpha: 0.9));
      final label = TextPainter(
        text: TextSpan(text: 'M${i + 1}', style: labelStyle.copyWith(color: color)),
        textDirection: TextDirection.ltr,
      )..layout();
      label.paint(canvas, Offset(x + 3, 8));
      label.dispose();
    }

    final hoverAt = view.hoverAt;
    if (hoverAt != null) {
      final x = view.xOf(hoverAt, w);
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), Paint()..color = color.withValues(alpha: 0.5));
    }
  }

  Color _colorOf(api.CaptureAnnotation a) {
    if (!a.hasFrame()) return otherColor;
    return a.frame.crcChecked && !a.frame.crcOk ? failedColor : frameColor;
  }

  // Zoomed out, a pixel may hold many: those of one color that touch are drawn as one run, labelled only when
  // it's one annotation with room for its label.
  void _paintAnnotations(Canvas canvas, Size size) {
    if (annotations.length == 0) return;
    final w = size.width, h = size.height;
    final hoverAt = view.hoverAt;
    final hovered = hoverAt == null ? const <int>[] : annotations.at(view.indexOf(hoverAt));

    // The run being drawn: from annotation `runFirst`, -1 for none, and alone in it while `runOne`.
    var runFirst = -1;
    var runOne = true;
    var runX0 = 0.0, runX1 = 0.0;
    var runColor = frameColor;
    void flush() {
      if (runFirst < 0) return;
      final rect = Rect.fromLTRB(runX0, 0, math.max(runX1, runX0 + 1), h);
      canvas.drawRect(rect, Paint()..color = runColor.withValues(alpha: 0.08));
      canvas.drawRect(Rect.fromLTRB(rect.left, h - _barHeight, rect.right, h), Paint()..color = runColor);
      final text = annotations.list[runFirst].label;
      if (runOne && text.isNotEmpty) {
        // On a solid tab of the bar's color, since the trace under it is often that color too.
        final onColor =
            ThemeData.estimateBrightnessForColor(runColor) == Brightness.dark ? Colors.white : Colors.black;
        final label = TextPainter(
          text: TextSpan(text: text, style: labelStyle.copyWith(color: onColor)),
          textDirection: TextDirection.ltr,
          maxLines: 1,
        )..layout();
        if (label.width + 4 <= rect.width) {
          final top = h - _barHeight - label.height;
          canvas.drawRect(Rect.fromLTWH(rect.left, top, label.width + 4, label.height), Paint()..color = runColor);
          label.paint(canvas, Offset(rect.left + 2, top));
        }
        label.dispose();
      }
      runFirst = -1;
    }

    for (var k = annotations.firstOver(view.lo); k < annotations.length && annotations.starts[k] < view.hi; k++) {
      final x0 = view.xOf(annotations.starts[k].toDouble(), w);
      final x1 = view.xOf(annotations.end(k).toDouble(), w);
      if (x1 < 0) continue;
      final c = _colorOf(annotations.list[k]);
      if (runFirst >= 0 && c == runColor && x0 <= runX1 + 1) {
        runX1 = math.max(runX1, x1);
        runOne = false;
        continue;
      }
      flush();
      runFirst = k;
      runOne = true;
      runX0 = x0;
      runX1 = x1;
      runColor = c;
    }
    flush();

    for (final k in hovered) {
      final x0 = view.xOf(annotations.starts[k].toDouble(), w);
      final x1 = math.max(view.xOf(annotations.end(k).toDouble(), w), x0 + 1);
      canvas.drawRect(
        Rect.fromLTRB(x0, 0, x1, h),
        Paint()
          ..style = PaintingStyle.stroke
          ..color = _colorOf(annotations.list[k]),
      );
    }
  }

  @override
  bool shouldRepaint(_RecordingOverlayPainter old) =>
      old.view != view ||
      old.annotations != annotations ||
      old.color != color ||
      old.frameColor != frameColor ||
      old.failedColor != failedColor ||
      old.otherColor != otherColor ||
      old.labelStyle != labelStyle;
}
