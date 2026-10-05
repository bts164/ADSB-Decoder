import 'dart:math' as math;

import 'package:flutter/material.dart';

/// Map silhouettes, one per kind of emitter category.
enum MarkerShape { unknown, prop, jet, fighter, helicopter, glider, balloon, drone, vehicle, obstacle }

/// Silhouette and size (relative to a large airliner) for each category.
/// Light aircraft are mostly props; High perf and Space get the delta.
const categoryMarkers = <String, (MarkerShape, double)>{
  'A1': (MarkerShape.prop, 0.75),
  'A2': (MarkerShape.jet, 0.85),
  'A3': (MarkerShape.jet, 1.0),
  'A4': (MarkerShape.jet, 1.05),
  'A5': (MarkerShape.jet, 1.25),
  'A6': (MarkerShape.fighter, 0.9),
  'A7': (MarkerShape.helicopter, 0.9),
  'B1': (MarkerShape.glider, 0.9),
  'B2': (MarkerShape.balloon, 0.8),
  'B3': (MarkerShape.balloon, 0.6),
  'B4': (MarkerShape.glider, 0.75),
  'B6': (MarkerShape.drone, 0.7),
  'B7': (MarkerShape.fighter, 1.0),
  'C1': (MarkerShape.vehicle, 0.6),
  'C2': (MarkerShape.vehicle, 0.6),
  'C3': (MarkerShape.obstacle, 0.7),
  'C4': (MarkerShape.obstacle, 0.7),
  'C5': (MarkerShape.obstacle, 0.7),
};

/// Draws a MarkerShape filled with `color` and outlined for contrast
/// against the map. Shapes are nose-up, so rotate by track outside.
class MarkerPainter extends CustomPainter {
  MarkerPainter(this.shape, this.color);

  final MarkerShape shape;
  final Color color;

  // Nose-up outline from its right half, (0, y) points included: the left
  // half mirrors it.
  static Path _symmetric(List<Offset> rightHalf) {
    final points = [...rightHalf, for (final p in rightHalf.reversed) Offset(-p.dx, p.dy)];
    return Path()..addPolygon(points, true);
  }

  static Path _union(List<Path> parts) => parts.reduce((a, b) => Path.combine(PathOperation.union, a, b));

  // In a 2x2 box centred on the origin, y down, nose toward -y.
  static final Map<MarkerShape, Path> _paths = {
    MarkerShape.unknown: Path()
      ..addPolygon(const [Offset(0, -1), Offset(0.75, 0.85), Offset(0, 0.45), Offset(-0.75, 0.85)], true),
    MarkerShape.prop: _symmetric(const [
      Offset(0, -1), Offset(0.1, -0.9), Offset(0.1, -0.45), Offset(0.95, -0.4), Offset(0.95, -0.15), //
      Offset(0.1, -0.1), Offset(0.08, 0.6), Offset(0.4, 0.65), Offset(0.4, 0.8), Offset(0.05, 0.85),
    ]),
    MarkerShape.jet: _symmetric(const [
      Offset(0, -1), Offset(0.1, -0.85), Offset(0.1, -0.3), Offset(0.95, 0.2), Offset(0.95, 0.32), //
      Offset(0.1, 0.1), Offset(0.1, 0.6), Offset(0.4, 0.85), Offset(0.4, 0.95), Offset(0.06, 0.95),
    ]),
    MarkerShape.fighter: _symmetric(const [
      Offset(0, -1), Offset(0.12, -0.5), Offset(0.8, 0.6), Offset(0.8, 0.75), Offset(0.15, 0.7), Offset(0.12, 0.9),
    ]),
    MarkerShape.glider: _symmetric(const [
      Offset(0, -1), Offset(0.08, -0.8), Offset(0.08, -0.3), Offset(1, -0.22), Offset(1, -0.12), //
      Offset(0.08, -0.1), Offset(0.05, 0.75), Offset(0.3, 0.8), Offset(0.3, 0.9), Offset(0.03, 0.92),
    ]),
    MarkerShape.helicopter: _union([
      Path()..addOval(Rect.fromCenter(center: const Offset(0, -0.25), width: 0.55, height: 0.9)),
      Path()..addRect(const Rect.fromLTRB(-0.06, 0, 0.06, 0.95)),
      Path()..addRect(const Rect.fromLTRB(-0.25, 0.78, 0.25, 0.88)),
      // Rotor blades.
      Path()..addRect(const Rect.fromLTRB(-0.95, -0.3, 0.95, -0.22)),
      Path()..addRect(const Rect.fromLTRB(-0.04, -1, 0.04, 0.5)),
    ]),
    MarkerShape.balloon: _union([
      Path()..addOval(Rect.fromCircle(center: const Offset(0, -0.3), radius: 0.62)),
      Path()..addPolygon(const [Offset(-0.45, 0), Offset(0.45, 0), Offset(0.14, 0.6), Offset(-0.14, 0.6)], true),
      Path()..addRect(const Rect.fromLTRB(-0.14, 0.68, 0.14, 0.92)),
    ]),
    MarkerShape.drone: _union([
      for (final (x, y) in const [(-1, -1), (1, -1), (-1, 1), (1, 1)])
        Path()..addOval(Rect.fromCircle(center: Offset(0.62 * x, 0.62 * y), radius: 0.32)),
      Path()..addPolygon(const [Offset(-0.7, -0.6), Offset(-0.6, -0.7), Offset(0.7, 0.6), Offset(0.6, 0.7)], true),
      Path()..addPolygon(const [Offset(0.7, -0.6), Offset(0.6, -0.7), Offset(-0.7, 0.6), Offset(-0.6, 0.7)], true),
      Path()..addRect(Rect.fromCircle(center: Offset.zero, radius: 0.22)),
    ]),
    MarkerShape.vehicle: Path()
      ..addRRect(RRect.fromLTRBR(-0.45, -0.85, 0.45, 0.85, const Radius.circular(0.15))),
    MarkerShape.obstacle: Path()
      ..addPolygon(const [Offset(0, -0.9), Offset(0.9, 0.75), Offset(-0.9, 0.75)], true),
  };

  /// Shapes whose heading means nothing, so they're drawn upright.
  static bool isUpright(MarkerShape shape) =>
      shape == MarkerShape.balloon || shape == MarkerShape.obstacle;

  @override
  void paint(Canvas canvas, Size size) {
    final half = size.shortestSide / 2;
    canvas
      ..translate(size.width / 2, size.height / 2)
      ..scale(half);
    final path = _paths[shape]!;
    canvas.drawPath(path, Paint()..color = color);
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.2 / half
        ..strokeJoin = StrokeJoin.round
        ..color = Colors.black.withValues(alpha: 0.6),
    );
  }

  @override
  bool shouldRepaint(MarkerPainter old) => old.shape != shape || old.color != color;
}

/// Colour for an altitude, on a scale like tar1090's: orange near the
/// ground, through green and blue, to purple at 40,000 ft and above.
Color altitudeColor(int altFt) {
  // (altitude ft, hue) stops, interpolated linearly between.
  const stops = [(0, 20.0), (2000, 30.0), (10000, 140.0), (40000, 300.0)];
  var hue = stops.last.$2;
  if (altFt <= stops.first.$1) {
    hue = stops.first.$2;
  } else {
    for (var i = 0; i + 1 < stops.length; i++) {
      final (a0, h0) = stops[i];
      final (a1, h1) = stops[i + 1];
      if (altFt <= a1) {
        hue = h0 + (h1 - h0) * (altFt - a0) / (a1 - a0);
        break;
      }
    }
  }
  return HSLColor.fromAHSL(1, hue, 0.75, 0.45).toColor();
}

/// Phone-style signal bars, `filled` of [count] lit.
class SignalBarsPainter extends CustomPainter {
  SignalBarsPainter(this.filled, this.color, this.emptyColor);

  static const count = 5;
  final int filled;
  final Color color;
  final Color emptyColor;

  @override
  void paint(Canvas canvas, Size size) {
    // Bars and gaps the same width.
    final w = size.width / (count * 2 - 1);
    for (var i = 0; i < count; i++) {
      final h = size.height * (i + 1) / count;
      canvas.drawRect(
        Rect.fromLTWH(i * 2 * w, size.height - h, w, h),
        Paint()..color = i < filled ? color : emptyColor,
      );
    }
  }

  @override
  bool shouldRepaint(SignalBarsPainter old) =>
      old.filled != filled || old.color != color || old.emptyColor != emptyColor;
}

/// An arrow through the centre, tilted with vertical rate: level at 0,
/// steepest (60 degrees) at +-[fullScaleFpm] and beyond.
class SlopePainter extends CustomPainter {
  SlopePainter(this.fpm, this.color);

  static const fullScaleFpm = 4000.0;
  final int fpm;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final angle = (fpm / fullScaleFpm).clamp(-1.0, 1.0) * math.pi / 3;
    final u = Offset(math.cos(angle), -math.sin(angle));
    final n = Offset(-u.dy, u.dx);
    final c = size.center(Offset.zero);
    final r = size.shortestSide / 2;
    final tip = c + u * r;
    canvas.drawLine(
      c - u * r,
      tip - u * 3,
      Paint()
        ..color = color
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round,
    );
    canvas.drawPath(
      Path()..addPolygon([tip, tip - u * 6 + n * 3.5, tip - u * 6 - n * 3.5], true),
      Paint()..color = color,
    );
  }

  @override
  bool shouldRepaint(SlopePainter old) => old.fpm != fpm || old.color != color;
}

/// A horizontal gauge, `fraction` full.
class GaugePainter extends CustomPainter {
  GaugePainter(this.fraction, this.color, this.trackColor);

  final double fraction;
  final Color color;
  final Color trackColor;

  @override
  void paint(Canvas canvas, Size size) {
    final radius = Radius.circular(size.height / 2);
    canvas.drawRRect(RRect.fromRectAndRadius(Offset.zero & size, radius), Paint()..color = trackColor);
    final filled = Size(size.width * fraction.clamp(0.0, 1.0), size.height);
    canvas.drawRRect(RRect.fromRectAndRadius(Offset.zero & filled, radius), Paint()..color = color);
  }

  @override
  bool shouldRepaint(GaugePainter old) =>
      old.fraction != fraction || old.color != color || old.trackColor != trackColor;
}

const compassPoints = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];

/// A horizontal bar of altitudeColor from 0 to 40,000 ft, for the map legend.
class AltitudeScalePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    const steps = 40;
    final w = size.width / steps;
    for (var i = 0; i < steps; i++) {
      canvas.drawRect(
        Rect.fromLTWH(i * w, 0, w + 0.5, size.height),
        Paint()..color = altitudeColor((i + 0.5) / steps * 40000 ~/ 1),
      );
    }
  }

  @override
  bool shouldRepaint(AltitudeScalePainter old) => false;
}

/// The scrubber's overview strip: the whole recording [startS, endS] as a
/// histogram of `activity` (aircraft per `activityBucketS` bucket from
/// `activityT0`; each pixel shows the busiest bucket it covers), with ticks
/// (full-height lines at local midnight), the fine slider's window
/// [windowStartS, windowEndS] and the scrub position.
class TimelinePainter extends CustomPainter {
  TimelinePainter({
    required this.startS,
    required this.endS,
    required this.windowStartS,
    required this.windowEndS,
    required this.positionS,
    required this.activity,
    required this.activityT0,
    required this.activityBucketS,
    required this.color,
    required this.trackColor,
    required this.utcOffsetS,
  });

  final double startS;
  final double endS;
  final double windowStartS;
  final double windowEndS;
  final double positionS;
  final List<int> activity;
  final double? activityT0;
  final double activityBucketS;
  final Color color;
  final Color trackColor;
  /// Seconds the displayed time zone is ahead of UTC, so midnight ticks land on its midnight.
  final int utcOffsetS;

  // Tick spacings to choose from, the finest that leaves 10 px between ticks.
  static const _tickStepsS = [60, 300, 900, 3600, 3 * 3600, 6 * 3600, 86400, 7 * 86400];

  @override
  void paint(Canvas canvas, Size size) {
    final span = endS - startS;
    if (span <= 0) return;
    double x(double t) => (t - startS) / span * size.width;

    _paintActivity(canvas, size, x);

    final track = Paint()..color = trackColor;
    canvas.drawRect(Rect.fromLTWH(0, size.height - 1, size.width, 1), track);

    final step = _tickStepsS.firstWhere((s) => s / span * size.width >= 10, orElse: () => _tickStepsS.last);
    // Aligned to the displayed time zone, so midnight ticks land on midnight. Ignores a DST change within the span.
    final offsetS = utcOffsetS;
    final tickStep = step >= 86400 ? 86400 : step;
    for (var t = ((startS + offsetS) / tickStep).ceil() * tickStep - offsetS.toDouble(); t <= endS; t += step) {
      final midnight = ((t + offsetS) % 86400).abs() < 1;
      final h = midnight ? size.height : 4.0;
      canvas.drawRect(Rect.fromLTWH(x(t), size.height - h, 1, h), track);
    }

    final left = x(windowStartS);
    final window = Rect.fromLTRB(left, 0, math.max(x(windowEndS), left + 4), size.height);
    canvas.drawRect(window, Paint()..color = color.withValues(alpha: 0.2));
    canvas.drawRect(
      window,
      Paint()
        ..color = color
        ..style = PaintingStyle.stroke,
    );
    canvas.drawRect(Rect.fromLTWH(x(positionS) - 1, 0, 2, size.height), Paint()..color = color);
  }

  void _paintActivity(Canvas canvas, Size size, double Function(double) x) {
    final t0 = activityT0;
    if (t0 == null || activity.isEmpty) return;
    // The busiest bucket touching each pixel column.
    final columns = List<int>.filled(math.max(1, size.width.ceil()), 0);
    for (var i = 0; i < activity.length; i++) {
      if (activity[i] == 0) continue;
      final t = t0 + i * activityBucketS;
      final c0 = x(t).floor().clamp(0, columns.length - 1);
      final c1 = (x(t + activityBucketS).ceil() - 1).clamp(c0, columns.length - 1);
      for (var c = c0; c <= c1; c++) {
        columns[c] = math.max(columns[c], activity[i]);
      }
    }
    final peak = columns.reduce(math.max);
    if (peak == 0) return;
    final bar = Paint()..color = color.withValues(alpha: 0.45);
    for (var c = 0; c < columns.length; c++) {
      if (columns[c] == 0) continue;
      final h = size.height * columns[c] / peak;
      canvas.drawRect(Rect.fromLTWH(c.toDouble(), size.height - h, 1, h), bar);
    }
  }

  @override
  bool shouldRepaint(TimelinePainter old) =>
      old.startS != startS ||
      old.endS != endS ||
      old.windowStartS != windowStartS ||
      old.windowEndS != windowEndS ||
      old.positionS != positionS ||
      !identical(old.activity, activity) ||
      old.color != color ||
      old.trackColor != trackColor ||
      old.utcOffsetS != utcOffsetS;
}

/// A line chart of `values` against `times` over [tMin, tMax]. Gaps of more
/// than [maxGapS] break the line; nulls are skipped. `colorOf` colours each
/// segment by its value; `zeroLine` draws y = 0 when it's in range.
/// `targets`, if given, is a second series on the same axis drawn as a dashed
/// step line in `targetColor`: a setting, such as the selected altitude, that
/// holds until the next point.
class TimeSeriesPainter extends CustomPainter {
  TimeSeriesPainter({
    required this.times,
    required this.values,
    required this.tMin,
    required this.tMax,
    required this.colorOf,
    required this.gridColor,
    this.zeroLine = false,
    this.targets,
    this.targetColor = Colors.grey,
  });

  static const maxGapS = 60.0;
  final List<double> times;
  final List<double?> values;
  final double tMin;
  final double tMax;
  final Color Function(double value) colorOf;
  final Color gridColor;
  final bool zeroLine;
  final List<double?>? targets;
  final Color targetColor;

  @override
  void paint(Canvas canvas, Size size) {
    final present = [...values.whereType<double>(), ...?targets?.whereType<double>()];
    if (present.isEmpty) return;
    var vMin = present.reduce(math.min);
    var vMax = present.reduce(math.max);
    if (zeroLine) {
      vMin = math.min(vMin, 0);
      vMax = math.max(vMax, 0);
    }
    if (vMax - vMin < 1e-9) {
      vMin -= 1;
      vMax += 1;
    }
    final span = math.max(tMax - tMin, 1e-9);
    Offset at(double t, double v) =>
        Offset((t - tMin) / span * size.width, size.height - (v - vMin) / (vMax - vMin) * size.height);

    final grid = Paint()
      ..color = gridColor
      ..strokeWidth = 1;
    canvas.drawLine(Offset(0, size.height), Offset(size.width, size.height), grid);
    if (zeroLine) canvas.drawLine(at(tMin, 0), at(tMax, 0), grid);

    final line = Paint()
      ..strokeWidth = 2
      ..strokeCap = StrokeCap.round;
    int? prev;
    for (var i = 0; i < times.length; i++) {
      final v = values[i];
      if (v == null) continue;
      if (prev != null && times[i] - times[prev] <= maxGapS) {
        canvas.drawLine(at(times[prev], values[prev]!), at(times[i], v), line..color = colorOf(v));
      } else {
        // A point with no neighbour still shows, as a dot.
        canvas.drawCircle(at(times[i], v), 1.5, Paint()..color = colorOf(v));
      }
      prev = i;
    }
    if (targets != null) _paintTargets(canvas, at);
  }

  void _paintTargets(Canvas canvas, Offset Function(double t, double v) at) {
    const dash = 4.0;
    final paint = Paint()
      ..color = targetColor
      ..strokeWidth = 1.5;
    void dashed(Offset a, Offset b) {
      final length = (b - a).distance;
      for (var d = 0.0; d < length; d += 2 * dash) {
        canvas.drawLine(Offset.lerp(a, b, d / length)!, Offset.lerp(a, b, math.min(d + dash, length) / length)!, paint);
      }
    }

    int? prev;
    for (var i = 0; i < times.length; i++) {
      final v = targets![i];
      if (v == null) continue;
      if (prev != null && times[i] - times[prev] <= maxGapS) {
        final held = targets![prev]!;
        dashed(at(times[prev], held), at(times[i], held));
        if (held != v) dashed(at(times[i], held), at(times[i], v));
      }
      prev = i;
    }
  }

  @override
  bool shouldRepaint(TimeSeriesPainter old) =>
      old.times != times ||
      old.values != values ||
      old.targets != targets ||
      old.tMin != tMin ||
      old.tMax != tMax;
}
