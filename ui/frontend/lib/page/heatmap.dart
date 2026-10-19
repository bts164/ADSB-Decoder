part of 'frame_table_page.dart';

// Colours of the heatmap, from few flights to many: light to dark so busy
// areas stand out on the light map (ColorBrewer's YlOrRd, run on to near black).
const _heatmapStops = [
  Color(0xffffeda0), Color(0xfffeb24c), Color(0xfffc4e2a), Color(0xffbd0026), Color(0xff4d0026),
];

final _heatmapLut = [
  for (var i = 0; i < 256; i++)
    () {
      final f = i / 255 * (_heatmapStops.length - 1);
      final k = math.min(f.floor(), _heatmapStops.length - 2);
      return Color.lerp(_heatmapStops[k], _heatmapStops[k + 1], f - k)!;
    }(),
];

/// RGBA pixels for a grid of flight counts, and the count at the dark end of
/// the colours. The counts are smoothed with a 3x3 [1 2 1] kernel, then
/// coloured on a log scale up to the 99th percentile of the cells with any
/// (the busiest few, usually by the airport, would otherwise push everything
/// else to the light end), and grow more opaque with it.
(Uint8List, double) _heatmapPixels(Float64List counts, int width, int height) {
  final smooth = Float64List(width * height);
  final nonzero = <double>[];
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      var sum = 0.0;
      for (var dy = -1; dy <= 1; dy++) {
        final yy = y + dy;
        if (yy < 0 || yy >= height) continue;
        for (var dx = -1; dx <= 1; dx++) {
          final xx = x + dx;
          if (xx < 0 || xx >= width) continue;
          sum += counts[yy * width + xx] * (dy == 0 ? 2 : 1) * (dx == 0 ? 2 : 1);
        }
      }
      smooth[y * width + x] = sum / 16;
      if (sum > 0) nonzero.add(sum / 16);
    }
  }
  final rgba = Uint8List(width * height * 4);
  if (nonzero.isEmpty) return (rgba, 0);
  nonzero.sort();
  final top = math.max(nonzero[(0.99 * (nonzero.length - 1)).round()], 2.0);
  final logTop = math.log(1 + top);
  for (var i = 0; i < smooth.length; i++) {
    if (smooth[i] <= 0) continue;
    final f = math.min(math.log(1 + smooth[i]) / logTop, 1.0);
    final c = _heatmapLut[(f * 255).round()];
    rgba[4 * i] = (c.r * 255).round();
    rgba[4 * i + 1] = (c.g * 255).round();
    rgba[4 * i + 2] = (c.b * 255).round();
    rgba[4 * i + 3] = (90 + 150 * f).round();
  }
  return (rgba, top);
}

// The server's kHistoryHeatmapMaxCells.
const _heatmapMaxCells = 512;

String _heatmapRangeLabel(double s) =>
    s.isInfinite ? 'all' : s >= 86400 ? '${(s / 86400).round()} d' : _windowLabel(s);

/// The flight-density overlay on the map.
extension _Heatmap on _FrameTablePageState {
  /// Asks for the heatmap of the map's current view over _heatmapRangeS up to
  /// the shown time. Needs a history db; does nothing while it's off.
  Future<void> _requestHeatmap() async {
    final range = _heatmapRangeS;
    if (range == null || !_mapReady || _client == null || _historyStartS == null) return;
    if (_heatmapInFlight) {
      _heatmapQueued = true;
      return;
    }
    final camera = _mapController.camera;
    final bounds = camera.visibleBounds;
    final t1 = _snapshot?.timeS; // null: up to the latest
    // The server clamps each side to its kHistoryHeatmapMaxCells, so clamp the same to read its cells.
    int cells(double px) => (px / _FrameTablePageState._heatmapCellPx).ceil().clamp(1, _heatmapMaxCells);
    final width = cells(camera.size.width);
    final height = cells(camera.size.height);
    _heatmapInFlight = true;
    final reply = await _ask((c) => c.heatmap(api.HeatmapRequest(
          west: bounds.west,
          south: bounds.south,
          east: bounds.east,
          north: bounds.north,
          width: width,
          height: height,
          t0: range.isFinite ? (t1 ?? _latestTimeS) - range : null,
          t1: t1,
        )));
    _heatmapInFlight = false;
    if (_heatmapQueued) {
      _heatmapQueued = false;
      _requestHeatmap();
    }
    if (reply != null) await _onHeatmap(reply, bounds, width, height);
  }

  void _scheduleHeatmap() {
    if (_heatmapRangeS == null) return;
    _heatmapDebounce?.cancel();
    _heatmapDebounce = Timer(const Duration(milliseconds: 300), _requestHeatmap);
  }

  /// Draws a heatmap reply as an image over the area it was asked for.
  Future<void> _onHeatmap(api.Heatmap reply, LatLngBounds bounds, int width, int height) async {
    if (_heatmapRangeS == null) return;
    final counts = Float64List(width * height);
    for (var i = 0; i < reply.cellIndex.length; i++) {
      counts[reply.cellIndex[i]] = reply.cellCount[i].toDouble();
    }
    final done = Completer<ui.Image>();
    final (pixels, top) = _heatmapPixels(counts, width, height);
    ui.decodeImageFromPixels(pixels, width, height, ui.PixelFormat.rgba8888, done.complete);
    final image = await done.future;
    // flutter_map's overlays take an ImageProvider, so hand it the image as a PNG.
    final png = await image.toByteData(format: ui.ImageByteFormat.png);
    image.dispose();
    if (png == null || _heatmapRangeS == null || !mounted) return;
    _heatmapTop = top;
    _heatmapOverlay = OverlayImage(
      bounds: bounds,
      imageProvider: MemoryImage(png.buffer.asUint8List()),
      gaplessPlayback: true,
    );
    _aircraftChanged.notify();
  }

  /// The map's heatmap switch: off, or how far back it looks. Shows the
  /// colour scale while on.
  Widget _buildHeatmapMenu() {
    final theme = Theme.of(context);
    final range = _heatmapRangeS;
    return PopupMenuButton<double>(
      tooltip: 'Heatmap of where aircraft flew',
      initialValue: range ?? -1,
      onSelected: (s) {
        _heatmapRangeS = s < 0 ? null : s;
        if (_heatmapRangeS == null) {
          _heatmapDebounce?.cancel();
          _heatmapOverlay = null;
        } else {
          _requestHeatmap();
        }
        _aircraftChanged.notify();
      },
      itemBuilder: (context) => [
        const PopupMenuItem(value: -1.0, child: Text('Off')),
        for (final s in _FrameTablePageState._heatmapRangeChoicesS)
          PopupMenuItem(value: s, child: Text(s.isInfinite ? 'All history' : 'Last ${_heatmapRangeLabel(s)}')),
      ],
      child: Card(
        margin: EdgeInsets.zero,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(8, 6, 10, 6),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.blur_on, size: 18, color: range == null ? null : theme.colorScheme.primary),
                  const SizedBox(width: 6),
                  Text(
                    range == null ? 'Heatmap' : 'Heatmap · ${_heatmapRangeLabel(range)}',
                    style: theme.textTheme.labelMedium,
                  ),
                ],
              ),
              if (range != null) ...[
                const SizedBox(height: 4),
                Container(
                  width: 110,
                  height: 6,
                  decoration: const BoxDecoration(gradient: LinearGradient(colors: _heatmapStops)),
                ),
                SizedBox(
                  width: 110,
                  child: DefaultTextStyle.merge(
                    style: theme.textTheme.labelSmall,
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [const Text('1'), Text('${_heatmapTop.round()}+ flights')],
                    ),
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
