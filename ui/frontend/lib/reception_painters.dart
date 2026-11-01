import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'painters.dart';
import 'reception.dart';

/// The Reception plots' colours and label text, from the theme.
class PlotStyle {
  const PlotStyle({required this.grid, required this.text, required this.area, required this.highlight});

  final Color grid;
  final TextStyle text;
  /// The coverage area and the side view's shading below the horizon; filled translucent, edged solid.
  final Color area;
  /// The selected aircraft's ring.
  final Color highlight;
}

/// An aircraft placed relative to the receiver.
typedef PlotAircraft = ({String icao, double rangeM, double bearingDeg, int? altFt});

/// A Reception plot, which can say which aircraft is under a tap.
abstract class ReceptionPainter extends CustomPainter {
  ReceptionPainter(this.style, this.selectedIcao);

  static const _hitPx = 8.0;

  final PlotStyle style;
  final String? selectedIcao;

  /// Where each aircraft that can be tapped is drawn, in `size`; an aircraft may be drawn in several places.
  Iterable<(String, Offset)> targets(Size size);

  /// The aircraft drawn nearest `p`, if any is close enough to have been meant.
  String? icaoAt(Offset p, Size size) {
    String? best;
    var bestPx = _hitPx;
    for (final (icao, at) in targets(size)) {
      final px = (at - p).distance;
      if (px <= bestPx) {
        best = icao;
        bestPx = px;
      }
    }
    return best;
  }

  /// `text` placed with the point of its box given by `align` at `at`.
  void label(Canvas canvas, String text, Offset at, {Alignment align = Alignment.center}) {
    final tp = TextPainter(text: TextSpan(text: text, style: style.text), textDirection: TextDirection.ltr)
      ..layout();
    tp.paint(canvas, at - Offset((align.x + 1) / 2 * tp.width, (align.y + 1) / 2 * tp.height));
    tp.dispose();
  }

  void dot(Canvas canvas, Offset at, Color color, {double radius = 3.5, bool selected = false}) {
    canvas.drawCircle(at, radius, Paint()..color = color);
    canvas.drawCircle(
      at,
      radius,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 0.8
        ..color = Colors.black.withValues(alpha: 0.5),
    );
    if (selected) {
      canvas.drawCircle(
        at,
        radius + 3,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = style.highlight,
      );
    }
  }

  void line(Canvas canvas, Offset a, Offset b) => canvas.drawLine(a, b, Paint()..color = style.grid);

  // The plots are rebuilt with new data once a second at most, so comparing it all buys nothing.
  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}

/// Ranges along x and some quantity along y, inside margins for the axis labels.
abstract class _XyPainter extends ReceptionPainter {
  _XyPainter(
    super.style,
    super.selectedIcao, {
    required this.maxRangeM,
    required this.rangeStepM,
    required this.rangeLabel,
  });

  static const _left = 52.0, _top = 8.0, _right = 10.0, _bottom = 20.0;

  final double maxRangeM;
  final double rangeStepM;
  final String Function(double m) rangeLabel;

  Rect plotRect(Size size) => Rect.fromLTRB(_left, _top, math.max(_left + 1, size.width - _right),
      math.max(_top + 1, size.height - _bottom));

  double x(Rect plot, double rangeM) => plot.left + rangeM.clamp(0, maxRangeM) / maxRangeM * plot.width;

  /// The frame, and a vertical grid line and label at each range step.
  void paintRangeAxis(Canvas canvas, Rect plot) {
    for (var i = 1; i * rangeStepM <= maxRangeM * (1 + 1e-9); i++) {
      final at = x(plot, i * rangeStepM);
      line(canvas, Offset(at, plot.top), Offset(at, plot.bottom));
      label(canvas, rangeLabel(i * rangeStepM), Offset(at, plot.bottom + 2), align: Alignment.topCenter);
    }
    label(canvas, '0', Offset(plot.left, plot.bottom + 2), align: Alignment.topCenter);
    canvas.drawRect(
      plot,
      Paint()
        ..style = PaintingStyle.stroke
        ..color = style.grid,
    );
  }
}

/// The coverage rosette: the farthest reception in each bearing sector, around the receiver at the centre with
/// north up, and the aircraft shown now as dots coloured by altitude.
class CoveragePainter extends ReceptionPainter {
  CoveragePainter({
    required this.coverage,
    required this.aircraft,
    required this.maxRangeM,
    required this.rangeStepM,
    required this.rangeLabel,
    required PlotStyle style,
    String? selectedIcao,
  }) : super(style, selectedIcao);

  // Room outside the outer ring for the compass letters.
  static const _marginPx = 16.0;

  final List<CoverageBin?> coverage;
  final List<PlotAircraft> aircraft;
  final double maxRangeM;
  final double rangeStepM;
  final String Function(double m) rangeLabel;

  double _radius(Size size) => math.max(1, size.shortestSide / 2 - _marginPx);

  Offset _at(Size size, double rangeM, double bearingDeg) {
    final r = rangeM.clamp(0, maxRangeM) / maxRangeM * _radius(size);
    final a = bearingDeg * math.pi / 180;
    return size.center(Offset.zero) + Offset(math.sin(a) * r, -math.cos(a) * r);
  }

  @override
  void paint(Canvas canvas, Size size) {
    final centre = size.center(Offset.zero);
    final radius = _radius(size);

    // Canvas angles run clockwise from +x; bearings clockwise from north, which is -y.
    final sector = 2 * math.pi / coverage.length;
    final area = Path();
    for (var i = 0; i < coverage.length; i++) {
      final bin = coverage[i];
      if (bin == null) continue;
      final r = bin.rangeM.clamp(0, maxRangeM) / maxRangeM * radius;
      area
        ..moveTo(centre.dx, centre.dy)
        ..arcTo(Rect.fromCircle(center: centre, radius: r), i * sector - sector / 2 - math.pi / 2, sector, false)
        ..close();
    }
    canvas.drawPath(area, Paint()..color = style.area.withValues(alpha: 0.25));
    canvas.drawPath(
      area,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = style.area,
    );

    final ring = Paint()
      ..style = PaintingStyle.stroke
      ..color = style.grid;
    for (var i = 1; i * rangeStepM <= maxRangeM * (1 + 1e-9); i++) {
      final r = i * rangeStepM / maxRangeM * radius;
      canvas.drawCircle(centre, r, ring);
      label(canvas, rangeLabel(i * rangeStepM), centre + Offset(3, -r), align: Alignment.bottomLeft);
    }
    for (var i = 0; i < compassPoints.length; i++) {
      final a = i * math.pi / 4;
      final dir = Offset(math.sin(a), -math.cos(a));
      line(canvas, centre, centre + dir * radius);
      if (i.isEven) label(canvas, compassPoints[i], centre + dir * (radius + _marginPx / 2));
    }

    PlotAircraft? selected;
    for (final a in aircraft) {
      if (a.icao == selectedIcao) {
        selected = a;
        continue;
      }
      dot(canvas, _at(size, a.rangeM, a.bearingDeg), _colorOf(a));
    }
    if (selected != null) {
      dot(canvas, _at(size, selected.rangeM, selected.bearingDeg), _colorOf(selected), selected: true);
    }
  }

  static Color _colorOf(PlotAircraft a) => a.altFt == null ? Colors.grey : altitudeColor(a.altFt!);

  @override
  Iterable<(String, Offset)> targets(Size size) => [
        for (final a in aircraft) (a.icao, _at(size, a.rangeM, a.bearingDeg)),
      ];
}

/// Signal against range, a dot per sample coloured by bearing (hue, red at north) and faded with age, and the
/// free-space line fitted to them.
class SignalPainter extends _XyPainter {
  SignalPainter({
    required this.samples,
    required this.fitDb,
    required super.maxRangeM,
    required super.rangeStepM,
    required super.rangeLabel,
    required PlotStyle style,
    String? selectedIcao,
  }) : super(style, selectedIcao) {
    var low = 0.0, high = 0.0;
    if (samples.isNotEmpty) {
      low = samples.map((s) => s.signalDb).reduce(math.min);
      high = samples.map((s) => s.signalDb).reduce(math.max);
    }
    _dbStep = niceStep(math.max(high - low, 10), 5);
    _lowDb = (low / _dbStep).floor() * _dbStep;
    _highDb = math.max((high / _dbStep).ceil() * _dbStep, _lowDb + _dbStep);
  }

  /// Oldest first.
  final List<SignalSample> samples;
  final double? fitDb;
  late final double _lowDb, _highDb, _dbStep;

  double _y(Rect plot, double db) => plot.bottom - (db - _lowDb) / (_highDb - _lowDb) * plot.height;

  @override
  void paint(Canvas canvas, Size size) {
    final plot = plotRect(size);
    for (var db = _lowDb; db <= _highDb + 1e-9; db += _dbStep) {
      final y = _y(plot, db);
      line(canvas, Offset(plot.left, y), Offset(plot.right, y));
      label(canvas, '${db.round()} dB', Offset(plot.left - 4, y), align: Alignment.centerRight);
    }
    paintRangeAxis(canvas, plot);

    canvas.save();
    canvas.clipRect(plot);
    final n = samples.length;
    final selected = <SignalSample>[];
    for (var i = 0; i < n; i++) {
      final s = samples[i];
      if (s.icao == selectedIcao) {
        selected.add(s);
        continue;
      }
      final alpha = n > 1 ? 0.15 + 0.85 * i / (n - 1) : 1.0;
      canvas.drawCircle(
        Offset(x(plot, s.rangeM), _y(plot, s.signalDb)),
        2,
        Paint()..color = HSLColor.fromAHSL(alpha, s.bearingDeg, 0.8, 0.5).toColor(),
      );
    }
    final fit = fitDb;
    if (fit != null) {
      final path = Path();
      var started = false;
      for (var px = 0.0; px <= plot.width; px += 2) {
        final rangeM = px / plot.width * maxRangeM;
        if (rangeM < 1000) continue;
        final at = Offset(plot.left + px, _y(plot, fit - 20 * math.log(rangeM / 1000) / math.ln10));
        started ? path.lineTo(at.dx, at.dy) : path.moveTo(at.dx, at.dy);
        started = true;
      }
      canvas.drawPath(
        path,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5
          ..color = style.text.color ?? Colors.grey,
      );
    }
    for (final s in selected) {
      dot(canvas, Offset(x(plot, s.rangeM), _y(plot, s.signalDb)), style.highlight, radius: 2.5);
    }
    canvas.restore();
  }

  @override
  Iterable<(String, Offset)> targets(Size size) {
    final plot = plotRect(size);
    return [for (final s in samples) (s.icao, Offset(x(plot, s.rangeM), _y(plot, s.signalDb)))];
  }
}

/// The side view: the aircraft shown now by range and altitude, over the shaded region below the radio horizon
/// of an antenna `antennaAglM` above ground at `groundM` above sea level.
class SideViewPainter extends _XyPainter {
  SideViewPainter({
    required this.aircraft,
    required this.groundM,
    required this.antennaAglM,
    required this.maxAltFt,
    required this.altStepFt,
    required this.altLabel,
    required super.maxRangeM,
    required super.rangeStepM,
    required super.rangeLabel,
    required PlotStyle style,
    String? selectedIcao,
  }) : super(style, selectedIcao);

  /// With an altitude.
  final List<PlotAircraft> aircraft;
  final double groundM;
  final double antennaAglM;
  final double maxAltFt;
  final double altStepFt;
  final String Function(double ft) altLabel;

  double _y(Rect plot, double altFt) => plot.bottom - altFt.clamp(0, maxAltFt) / maxAltFt * plot.height;

  @override
  void paint(Canvas canvas, Size size) {
    final plot = plotRect(size);

    final horizon = Path()..moveTo(plot.left, plot.bottom);
    for (var px = 0.0; px <= plot.width + 1e-9; px += 2) {
      final rangeM = px / plot.width * maxRangeM;
      final altFt = (groundM + radioHorizonAglM(rangeM, antennaAglM)) / 0.3048;
      horizon.lineTo(plot.left + px, _y(plot, altFt));
    }
    horizon
      ..lineTo(plot.right, plot.bottom)
      ..close();
    canvas.drawPath(horizon, Paint()..color = style.area.withValues(alpha: 0.25));
    canvas.save();
    canvas.clipRect(plot);
    canvas.drawPath(
      horizon,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = style.area,
    );
    canvas.restore();

    for (var i = 1; i * altStepFt <= maxAltFt * (1 + 1e-9); i++) {
      final y = _y(plot, i * altStepFt);
      line(canvas, Offset(plot.left, y), Offset(plot.right, y));
      label(canvas, altLabel(i * altStepFt), Offset(plot.left - 4, y), align: Alignment.centerRight);
    }
    paintRangeAxis(canvas, plot);

    PlotAircraft? selected;
    for (final a in aircraft) {
      if (a.icao == selectedIcao) {
        selected = a;
        continue;
      }
      dot(canvas, _at(plot, a), altitudeColor(a.altFt!));
    }
    if (selected != null) dot(canvas, _at(plot, selected), altitudeColor(selected.altFt!), selected: true);
  }

  Offset _at(Rect plot, PlotAircraft a) => Offset(x(plot, a.rangeM), _y(plot, a.altFt!.toDouble()));

  @override
  Iterable<(String, Offset)> targets(Size size) {
    final plot = plotRect(size);
    return [for (final a in aircraft) (a.icao, _at(plot, a))];
  }
}
