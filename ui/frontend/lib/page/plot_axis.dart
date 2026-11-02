part of 'frame_table_page.dart';

/// What a drag on a plot does, picked on its mode bar: a span to zoom to, a pan, or a span to measure.
enum _PlotMode {
  zoom(Icons.zoom_in, 'Zoom: drag across a span to zoom to it (Z)', 'drag zooms to a span, right-drag pans',
      LogicalKeyboardKey.keyZ),
  pan(Icons.pan_tool_outlined, 'Pan: drag to move along (P)', 'drag pans, right-drag zooms to a span',
      LogicalKeyboardKey.keyP),
  measure(Icons.straighten, 'Measure: drag across a span to measure it (M)', 'drag measures a span, right-drag pans',
      LogicalKeyboardKey.keyM);

  /// What a right-drag or a shift-drag does instead: the other of zoom and pan, and a pan from a measure.
  _PlotMode get other => this == _PlotMode.pan ? _PlotMode.zoom : _PlotMode.pan;

  const _PlotMode(this.icon, this.tooltip, this.hint, this.key);
  final IconData icon;
  final String tooltip;
  /// For a readout's line of what the mouse does.
  final String hint;
  final LogicalKeyboardKey key;
}

/// A plot's x axis: the part of it in view and what's marked on it, from 0 to [extent] in [cells] equal cells
/// (a spectrum's bins, a recording's samples), whose centres markers snap to. Plots sharing one stay aligned;
/// [_PlotGestures] moves it with the mouse.
class _PlotAxis {
  _PlotAxis({required this.minCells});

  static const _maxMarkers = 2;
  static const _zoomOutFactor = 2.0;

  /// The narrowest view, in cells: past it a zoom only makes the cells wider.
  final int minCells;

  /// The view moved: the plots redraw.
  final zoomChanged = _Changed();
  /// The hover, markers or spans changed: only the overlays and readouts redraw.
  final marksChanged = _Changed();
  final modeChanged = _Changed();

  double extent = 1;
  int cells = 0;
  double lo = 0;
  double hi = 1;
  double? hoverAt;
  /// Cell centres.
  final List<double> markers = [];
  /// Low end first.
  (double, double)? measured;
  /// While a drag zooms: the span it zooms to when it ends.
  (double, double)? zoomBox;
  _PlotMode mode = _PlotMode.zoom;

  bool get zoomed => lo > 0 || hi < extent;
  double get _cellWidth => extent / math.max(cells, 1);
  double get _minSpan => math.min(extent, minCells * _cellWidth);

  double valueAt(double x, double width) => lo + x.clamp(0, width) / width * (hi - lo);
  double xOf(double value, double width) => (value - lo) / (hi - lo) * width;
  int cellOf(double value) => (value / _cellWidth).floor().clamp(0, math.max(0, cells - 1));

  /// Starts over on a new axis, whose old view and marks would be of other things.
  void reset(double extent, int cells) {
    this.extent = extent;
    this.cells = cells;
    markers.clear();
    measured = null;
    zoomBox = null;
    _setView(0, extent);
  }

  void setMode(_PlotMode mode) {
    this.mode = mode;
    modeChanged.notify();
  }

  /// Zooms by `factor` (below 1 in) keeping `value` where it is.
  void zoomAbout(double value, double factor) {
    final span = ((hi - lo) * factor).clamp(_minSpan, extent).toDouble();
    _setView(value - (value - lo) / (hi - lo) * span, span);
  }

  void zoomTo((double, double) span) {
    final width = math.max(span.$2 - span.$1, _minSpan);
    _setView((span.$1 + span.$2) / 2 - width / 2, width);
  }

  void zoomToMeasured() {
    final m = measured;
    if (m != null) zoomTo(m);
  }

  void whole() {
    _setView(0, extent);
  }

  /// About the view's centre, so where it is stays put.
  void zoomOut() => zoomAbout((lo + hi) / 2, _zoomOutFactor);

  /// `dx` is the step as a fraction of the plot's width.
  void pan(double dx) => _setView(lo - dx * (hi - lo), hi - lo);

  /// The view from before a pan that's taken back.
  void restore((double, double) view) => _setView(view.$1, view.$2 - view.$1);

  void _setView(double left, double span) {
    lo = left.clamp(0.0, extent - span);
    hi = lo + span;
    zoomChanged.notify();
    marksChanged.notify();
  }

  void setZoomBox((double, double)? box) {
    zoomBox = box == null || box.$1 <= box.$2 ? box : (box.$2, box.$1);
    marksChanged.notify();
  }

  /// Within one cell is a slip, and clears it.
  void setMeasured((double, double)? span) {
    measured = span == null || cellOf(span.$1) == cellOf(span.$2)
        ? null
        : span.$1 <= span.$2
            ? span
            : (span.$2, span.$1);
    marksChanged.notify();
  }

  void clearMeasured() => setMeasured(null);

  /// A new marker, at the centre of the cell under `value`; with both placed, the nearer one moves there.
  void placeMarker(double value) {
    if (cells == 0) return;
    final at = (cellOf(value) + 0.5) * _cellWidth;
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

  void hover(double value) {
    hoverAt = value;
    marksChanged.notify();
  }

  void unhover() {
    hoverAt = null;
    marksChanged.notify();
  }

  void dispose() {
    zoomChanged.dispose();
    marksChanged.dispose();
    modeChanged.dispose();
  }
}

/// The measured span, and a zooming drag's box, over a plot of `axis`.
void _paintAxisSpans(Canvas canvas, Size size, _PlotAxis axis, Color color) {
  void span((double, double) s, double alpha) {
    final rect = Rect.fromLTRB(axis.xOf(s.$1, size.width), 0, axis.xOf(s.$2, size.width), size.height);
    canvas.drawRect(rect, Paint()..color = color.withValues(alpha: alpha));
    final edge = Paint()..color = color.withValues(alpha: 0.6);
    canvas.drawLine(rect.topLeft, rect.bottomLeft, edge);
    canvas.drawLine(rect.topRight, rect.bottomRight, edge);
  }

  if (axis.measured case final m?) span(m, 0.12);
  if (axis.zoomBox case final b?) span(b, 0.25);
}

/// `child`, a plot of `axis`, under `overlay` and taking the mouse, as most plotting tools do. The wheel zooms
/// about the pointer; a drag does what the mode says, and a right-drag or a shift-drag does its
/// [_PlotMode.other]; a middle-button drag always pans; a click places a marker, and a right-click does
/// nothing; a double-click shows the whole axis. A drag that ends within a few pixels of its start is a click.
/// Over the plot, Z, P and M pick the mode, minus or Backspace zooms out, Home shows the whole axis, Escape
/// abandons a drag and ? shows all this in [_showPlotHelp].
class _PlotGestures extends StatefulWidget {
  const _PlotGestures({
    required this.axis,
    required this.overlay,
    required this.child,
    this.onHover,
    this.modeBar = false,
    this.actions = const [],
  });

  final _PlotAxis axis;
  /// Repaints on its own as the marks change, without `child`.
  final CustomPainter overlay;
  final Widget child;
  /// In place of the axis's own hover, for a plot that also tracks the pointer's height.
  final void Function(Offset local, Size size, double value)? onHover;
  /// Whether this plot shows the mode bar: one of the plots sharing an axis.
  final bool modeBar;
  /// The plot's own buttons, at the end of the mode bar, made with [_plotBarButton].
  final List<Widget> actions;

  @override
  State<_PlotGestures> createState() => _PlotGesturesState();
}

class _PlotGesturesState extends State<_PlotGestures> {
  // How far, in pixels, a drag must go to be one rather than a click.
  static const _clickSlop = 4.0;

  final _focus = FocusNode(debugLabel: 'plot');
  // The buttons of the press a drag starts with, which a DragStartDetails doesn't say.
  int _buttons = 0;
  // The drag in progress: what it does, where it started and got to, and what Escape puts back.
  _PlotMode? _dragging;
  double _startX = 0;
  double _lastX = 0;
  double _from = 0;
  (double, double) _viewBefore = (0, 1);
  (double, double)? _measuredBefore;

  _PlotAxis get _axis => widget.axis;

  @override
  void dispose() {
    _focus.dispose();
    super.dispose();
  }

  // Not from a text field being typed in.
  void _focusIfFree() {
    final current = FocusManager.instance.primaryFocus?.context;
    if (current?.findAncestorWidgetOfExactType<EditableText>() == null) _focus.requestFocus();
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent) return KeyEventResult.ignored;
    final key = event.logicalKey;
    if (key == LogicalKeyboardKey.escape && _dragging != null) {
      _abandon();
    } else if (event.character == '?') {
      _showPlotHelp(context);
    } else if (key == LogicalKeyboardKey.minus ||
        key == LogicalKeyboardKey.numpadSubtract ||
        key == LogicalKeyboardKey.backspace) {
      _axis.zoomOut();
    } else if (key == LogicalKeyboardKey.home) {
      _axis.whole();
    } else if (_PlotMode.values.where((m) => m.key == key).firstOrNull case final mode?) {
      _axis.setMode(mode);
    } else {
      return KeyEventResult.ignored;
    }
    return KeyEventResult.handled;
  }

  void _start(DragStartDetails d, double width) {
    _focus.requestFocus();
    final kind = _buttons == kMiddleMouseButton
        ? _PlotMode.pan
        : _buttons == kSecondaryMouseButton || HardwareKeyboard.instance.isShiftPressed
            ? _axis.mode.other
            : _axis.mode;
    _startX = _lastX = d.localPosition.dx;
    _from = _axis.valueAt(_startX, width);
    _viewBefore = (_axis.lo, _axis.hi);
    _measuredBefore = _axis.measured;
    setState(() => _dragging = kind);
  }

  void _update(DragUpdateDetails d, double width) {
    _lastX = d.localPosition.dx;
    // Until it's gone far enough to be a drag, a span would flash up under a click.
    final span = (_lastX - _startX).abs() < _clickSlop ? null : (_from, _axis.valueAt(_lastX, width));
    switch (_dragging) {
      case null:
        return;
      case _PlotMode.pan:
        _axis.pan(d.delta.dx / width);
      case _PlotMode.zoom:
        _axis.setZoomBox(span);
      case _PlotMode.measure:
        if (span != null) _axis.setMeasured(span);
    }
  }

  void _end() {
    final kind = _dragging;
    if (kind == null) return;
    final click = (_lastX - _startX).abs() < _clickSlop;
    switch (kind) {
      case _PlotMode.pan:
        if (click) _axis.restore(_viewBefore);
      case _PlotMode.zoom:
        final box = _axis.zoomBox;
        _axis.setZoomBox(null);
        if (box != null) _axis.zoomTo(box);
      case _PlotMode.measure:
        break;
    }
    if (click && _buttons & kPrimaryButton != 0) _axis.placeMarker(_from);
    setState(() => _dragging = null);
  }

  void _abandon() {
    switch (_dragging) {
      case null:
        return;
      case _PlotMode.pan:
        _axis.restore(_viewBefore);
      case _PlotMode.zoom:
        _axis.setZoomBox(null);
      case _PlotMode.measure:
        _axis.setMeasured(_measuredBefore);
    }
    setState(() => _dragging = null);
  }

  @override
  Widget build(BuildContext context) {
    final axis = _axis;
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = constraints.biggest;
        double at(Offset p) => axis.valueAt(p.dx, size.width);
        final plot = Focus(
          focusNode: _focus,
          onKeyEvent: _onKey,
          child: Listener(
            onPointerDown: (e) => _buttons = e.buttons,
            onPointerSignal: (event) {
              if (event is! PointerScrollEvent) return;
              GestureBinding.instance.pointerSignalResolver.register(event, (event) {
                final scroll = event as PointerScrollEvent;
                axis.zoomAbout(at(scroll.localPosition), math.exp(scroll.scrollDelta.dy / 400));
              });
            },
            child: ListenableBuilder(
              listenable: axis.modeChanged,
              builder: (context, child) => MouseRegion(
                cursor: _dragging == _PlotMode.pan
                    ? SystemMouseCursors.grabbing
                    : axis.mode == _PlotMode.pan
                        ? SystemMouseCursors.grab
                        : SystemMouseCursors.precise,
                onEnter: (_) => _focusIfFree(),
                onHover: (e) {
                  final onHover = widget.onHover;
                  if (onHover == null) {
                    axis.hover(at(e.localPosition));
                  } else {
                    onHover(e.localPosition, size, at(e.localPosition));
                  }
                },
                onExit: (_) => axis.unhover(),
                child: child,
              ),
              child: RawGestureDetector(
                gestures: {
                  TapGestureRecognizer: GestureRecognizerFactoryWithHandlers<TapGestureRecognizer>(
                    TapGestureRecognizer.new,
                    (r) => r.onTapUp = (d) => axis.placeMarker(at(d.localPosition)),
                  ),
                  DoubleTapGestureRecognizer: GestureRecognizerFactoryWithHandlers<DoubleTapGestureRecognizer>(
                    DoubleTapGestureRecognizer.new,
                    (r) => r.onDoubleTap = axis.whole,
                  ),
                  HorizontalDragGestureRecognizer:
                      GestureRecognizerFactoryWithHandlers<HorizontalDragGestureRecognizer>(
                    () => HorizontalDragGestureRecognizer(
                      allowedButtonsFilter: (buttons) =>
                          buttons == kPrimaryButton ||
                          buttons == kSecondaryMouseButton ||
                          buttons == kMiddleMouseButton,
                    ),
                    (r) => r
                      // From where the button went down, not where the drag was recognised.
                      ..dragStartBehavior = DragStartBehavior.down
                      ..onStart = ((d) => _start(d, size.width))
                      ..onUpdate = ((d) => _update(d, size.width))
                      ..onEnd = ((_) => _end())
                      ..onCancel = _abandon,
                  ),
                },
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    RepaintBoundary(child: widget.child),
                    RepaintBoundary(child: CustomPaint(painter: widget.overlay)),
                  ],
                ),
              ),
            ),
          ),
        );
        if (!widget.modeBar) return plot;
        return Stack(
          fit: StackFit.expand,
          children: [
            plot,
            Positioned(top: 4, right: 4, child: _PlotModeBar(axis: axis, actions: widget.actions)),
          ],
        );
      },
    );
  }
}

/// The drag modes, a zoom out and the whole axis, as a row of buttons in a plot's corner.
class _PlotModeBar extends StatelessWidget {
  const _PlotModeBar({required this.axis, required this.actions});

  final _PlotAxis axis;
  final List<Widget> actions;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    Widget button(IconData icon, String tooltip, VoidCallback? onPressed, {bool selected = false}) =>
        _plotBarButton(context, icon, tooltip, onPressed, selected: selected);
    const divider = SizedBox(height: 24, child: VerticalDivider(width: 9));
    return Material(
      elevation: 2,
      borderRadius: BorderRadius.circular(8),
      color: theme.colorScheme.surfaceContainerHigh.withValues(alpha: 0.9),
      child: ListenableBuilder(
        listenable: Listenable.merge([axis.modeChanged, axis.zoomChanged]),
        builder: (context, _) => Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (final mode in _PlotMode.values)
              button(mode.icon, mode.tooltip, () => axis.setMode(mode), selected: axis.mode == mode),
            divider,
            button(Icons.zoom_out, 'Zoom out (-)', axis.zoomed ? axis.zoomOut : null),
            button(Icons.zoom_out_map, 'Show it all (Home, or double-click)', axis.zoomed ? axis.whole : null),
            if (actions.isNotEmpty) ...[divider, ...actions],
            button(Icons.help_outline, 'The mouse and keys (?)', () => _showPlotHelp(context)),
          ],
        ),
      ),
    );
  }
}

/// A button in a plot's mode bar, filled while `selected`.
Widget _plotBarButton(BuildContext context, IconData icon, String tooltip, VoidCallback? onPressed,
    {bool selected = false}) {
  return IconButton(
    icon: Icon(icon, size: 18),
    tooltip: tooltip,
    isSelected: selected,
    visualDensity: VisualDensity.compact,
    style: IconButton.styleFrom(
      backgroundColor: selected ? Theme.of(context).colorScheme.secondaryContainer : null,
      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
    ),
    onPressed: onPressed,
  );
}

/// What the mouse and keys do on a plot, as [_PlotGestures] has them, for reference.
Future<void> _showPlotHelp(BuildContext context) {
  const mouse = [
    ('Wheel', 'Zoom about the pointer'),
    ('Drag', 'What the mode says: zoom to the span, pan, or measure the span'),
    ('Right-drag', 'Pan in Zoom and Measure; zoom to the span in Pan'),
    ('Shift-drag', 'As a right-drag, for a trackpad'),
    ('Middle-drag', 'Pan, in any mode'),
    ('Click', 'Place a marker; with two placed, the nearer one moves'),
    ('Double-click', 'Show it all'),
  ];
  const keys = [
    ('Z, P, M', 'Zoom, Pan or Measure mode'),
    ('- or Backspace', 'Zoom out, twice as wide about the centre'),
    ('Home', 'Show it all'),
    ('Escape', 'Abandon a drag, putting the view or span back'),
    ('?', 'This help'),
  ];
  return showDialog<void>(
    context: context,
    builder: (context) {
      final theme = Theme.of(context);
      Widget section(String title, List<(String, String)> rows) => Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Padding(
                padding: const EdgeInsets.only(top: 12, bottom: 4),
                child: Text(title, style: theme.textTheme.titleSmall),
              ),
              for (final (what, does) in rows)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 2),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      SizedBox(
                        width: 120,
                        child: Text(what, style: theme.textTheme.bodyMedium?.copyWith(fontWeight: FontWeight.w600)),
                      ),
                      Expanded(child: Text(does, style: theme.textTheme.bodyMedium)),
                    ],
                  ),
                ),
            ],
          );
      return AlertDialog(
        title: const Text('Plot controls'),
        content: SizedBox(
          width: 460,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                section('Mouse', mouse),
                section('Keys, with the pointer over the plot', keys),
              ],
            ),
          ),
        ),
        actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Close'))],
      );
    },
  );
}
