import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
// hide Path: latlong2's geodesic Path shadows dart:ui's, which the map markers draw with.
import 'package:latlong2/latlong.dart' hide Path;
import 'package:web_socket_channel/web_socket_channel.dart';

void main() {
  runApp(const AdsbApp());
}

class AdsbApp extends StatelessWidget {
  const AdsbApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ADS-B Frames',
      theme: ThemeData(colorSchemeSeed: Colors.blue, useMaterial3: true),
      home: const FrameTablePage(),
    );
  }
}

/// One decoded frame, as published by the backend's --ws-port
/// (see proto/src/decode/frame_decode.cpp: to_json).
class FrameRecord {
  final int idx;
  final int bits;
  final double confidence;
  final int df;
  final String? icao;
  final bool crcOk;
  /// False for DFs with no parity rule to check (military DF19/22).
  final bool crcChecked;
  /// The bit single-bit error correction flipped to pass the CRC (DF17/18), or null if none.
  final int? crcFixedBit;
  final String payload;
  /// One-line description of what the frame says, e.g. "Position · 7200 ft"; null when the CRC failed.
  final String? summary;

  FrameRecord({
    required this.idx,
    required this.bits,
    required this.confidence,
    required this.df,
    required this.icao,
    required this.crcOk,
    required this.crcChecked,
    required this.crcFixedBit,
    required this.payload,
    required this.summary,
  });

  factory FrameRecord.fromJson(Map<String, dynamic> j) {
    return FrameRecord(
      idx: j['idx'] as int,
      bits: j['bits'] as int,
      confidence: (j['confidence'] as num).toDouble(),
      df: j['df'] as int,
      icao: j['icao'] as String?,
      crcOk: j['crc_ok'] as bool,
      crcChecked: j['crc_checked'] as bool,
      crcFixedBit: j['crc_fixed_bit'] as int?,
      payload: j['payload'] as String,
      summary: j['summary'] as String?,
    );
  }
}

/// Current tracked state of one aircraft, as published by the backend's
/// --ws-port on every change (see proto/src/decode/aircraft.cpp: to_json).
/// Unlike FrameRecord this isn't appended to a log -- each message carries
/// the aircraft's full current state, so the frontend just replaces its map
/// entry for that ICAO wholesale.
class AircraftRecord {
  final String icao;
  final String? callsign;
  /// ADS-B emitter category code, e.g. "A3"; see _categoryNames.
  final String? category;
  /// Mode A code, four octal digits.
  final String? squawk;
  final int? altitudeFt;
  final double? lat;
  final double? lon;
  final double? groundSpeedKt;
  final double? trackDeg;
  final int? verticalRateFpm;
  /// Magnetic heading.
  final double? headingDeg;
  final int? iasKt;
  final int? tasKt;
  final double? mach;
  /// Positive is right wing down.
  final double? rollDeg;
  /// Null until a frame says either way.
  final bool? onGround;
  /// Squawk changed, or an emergency.
  final bool alert;
  /// Ident: the pilot pressed the transponder's ident button.
  final bool spi;
  final int? selectedAltitudeFt;
  /// "MCP" (the autopilot panel) or "FMS".
  final String? selectedAltitudeSource;
  final double? selectedHeadingDeg;
  final double? baroSettingHpa;
  /// Of "AP", "VNAV", "ALT", "APP", "LNAV"; null if not reported.
  final List<String>? autopilotModes;
  final int? adsbVersion;
  /// Navigation accuracy category for position, 0 (unknown) to 11 (< 3 m).
  final int? nacP;
  /// Source integrity level, 0 (unknown) to 3.
  final int? sil;
  /// Wall-clock time (Unix seconds) of the aircraft's last update.
  final double lastSeenUnixS;
  /// Frames received from the aircraft.
  final int messages;
  /// Average per-bit pulse amplitude relative to the noise-and-signal RMS of
  /// its block, in dB; a relative signal level, not absolute power.
  final double? signalDb;

  AircraftRecord({
    required this.icao,
    required this.callsign,
    required this.category,
    required this.squawk,
    required this.altitudeFt,
    required this.lat,
    required this.lon,
    required this.groundSpeedKt,
    required this.trackDeg,
    required this.verticalRateFpm,
    required this.headingDeg,
    required this.iasKt,
    required this.tasKt,
    required this.mach,
    required this.rollDeg,
    required this.onGround,
    required this.alert,
    required this.spi,
    required this.selectedAltitudeFt,
    required this.selectedAltitudeSource,
    required this.selectedHeadingDeg,
    required this.baroSettingHpa,
    required this.autopilotModes,
    required this.adsbVersion,
    required this.nacP,
    required this.sil,
    required this.lastSeenUnixS,
    required this.messages,
    required this.signalDb,
  });

  factory AircraftRecord.fromJson(Map<String, dynamic> j) {
    return AircraftRecord(
      icao: j['icao'] as String,
      callsign: j['callsign'] as String?,
      category: j['category'] as String?,
      squawk: j['squawk'] as String?,
      altitudeFt: j['altitude_ft'] as int?,
      lat: (j['lat'] as num?)?.toDouble(),
      lon: (j['lon'] as num?)?.toDouble(),
      groundSpeedKt: (j['ground_speed_kt'] as num?)?.toDouble(),
      trackDeg: (j['track_deg'] as num?)?.toDouble(),
      verticalRateFpm: j['vertical_rate_fpm'] as int?,
      headingDeg: (j['heading_deg'] as num?)?.toDouble(),
      iasKt: j['ias_kt'] as int?,
      tasKt: j['tas_kt'] as int?,
      mach: (j['mach'] as num?)?.toDouble(),
      rollDeg: (j['roll_deg'] as num?)?.toDouble(),
      onGround: j['on_ground'] as bool?,
      alert: j['alert'] as bool? ?? false,
      spi: j['spi'] as bool? ?? false,
      selectedAltitudeFt: j['selected_altitude_ft'] as int?,
      selectedAltitudeSource: j['selected_altitude_source'] as String?,
      selectedHeadingDeg: (j['selected_heading_deg'] as num?)?.toDouble(),
      baroSettingHpa: (j['baro_setting_hpa'] as num?)?.toDouble(),
      autopilotModes: (j['autopilot_modes'] as List?)?.cast<String>(),
      adsbVersion: j['adsb_version'] as int?,
      nacP: j['nac_p'] as int?,
      sil: j['sil'] as int?,
      lastSeenUnixS: (j['last_seen_unix_s'] as num).toDouble(),
      messages: j['messages'] as int,
      signalDb: (j['signal_db'] as num?)?.toDouble(),
    );
  }
}

/// Short names for ADS-B emitter categories (DO-260B 2.2.3.2.5.2); codes
/// missing here are reserved and shown as-is.
const _categoryNames = {
  'A1': 'Light',
  'A2': 'Small',
  'A3': 'Large',
  'A4': 'B757-class',
  'A5': 'Heavy',
  'A6': 'High perf',
  'A7': 'Rotorcraft',
  'B1': 'Glider',
  'B2': 'Balloon',
  'B3': 'Parachutist',
  'B4': 'Ultralight',
  'B6': 'UAV',
  'B7': 'Space',
  'C1': 'Emerg. vehicle',
  'C2': 'Service vehicle',
  'C3': 'Obstacle',
  'C4': 'Obstacle group',
  'C5': 'Line obstacle',
};

/// Map silhouettes, one per kind of emitter category.
enum _MarkerShape { unknown, prop, jet, fighter, helicopter, glider, balloon, drone, vehicle, obstacle }

/// Silhouette and size (relative to a large airliner) for each category.
/// Light aircraft are mostly props; High perf and Space get the delta.
const _categoryMarkers = <String, (_MarkerShape, double)>{
  'A1': (_MarkerShape.prop, 0.75),
  'A2': (_MarkerShape.jet, 0.85),
  'A3': (_MarkerShape.jet, 1.0),
  'A4': (_MarkerShape.jet, 1.05),
  'A5': (_MarkerShape.jet, 1.25),
  'A6': (_MarkerShape.fighter, 0.9),
  'A7': (_MarkerShape.helicopter, 0.9),
  'B1': (_MarkerShape.glider, 0.9),
  'B2': (_MarkerShape.balloon, 0.8),
  'B3': (_MarkerShape.balloon, 0.6),
  'B4': (_MarkerShape.glider, 0.75),
  'B6': (_MarkerShape.drone, 0.7),
  'B7': (_MarkerShape.fighter, 1.0),
  'C1': (_MarkerShape.vehicle, 0.6),
  'C2': (_MarkerShape.vehicle, 0.6),
  'C3': (_MarkerShape.obstacle, 0.7),
  'C4': (_MarkerShape.obstacle, 0.7),
  'C5': (_MarkerShape.obstacle, 0.7),
};

/// Draws a _MarkerShape filled with `color` and outlined for contrast
/// against the map. Shapes are nose-up, so rotate by track outside.
class _MarkerPainter extends CustomPainter {
  _MarkerPainter(this.shape, this.color);

  final _MarkerShape shape;
  final Color color;

  // Nose-up outline from its right half, (0, y) points included: the left
  // half mirrors it.
  static Path _symmetric(List<Offset> rightHalf) {
    final points = [...rightHalf, for (final p in rightHalf.reversed) Offset(-p.dx, p.dy)];
    return Path()..addPolygon(points, true);
  }

  static Path _union(List<Path> parts) => parts.reduce((a, b) => Path.combine(PathOperation.union, a, b));

  // In a 2x2 box centred on the origin, y down, nose toward -y.
  static final Map<_MarkerShape, Path> _paths = {
    _MarkerShape.unknown: Path()
      ..addPolygon(const [Offset(0, -1), Offset(0.75, 0.85), Offset(0, 0.45), Offset(-0.75, 0.85)], true),
    _MarkerShape.prop: _symmetric(const [
      Offset(0, -1), Offset(0.1, -0.9), Offset(0.1, -0.45), Offset(0.95, -0.4), Offset(0.95, -0.15), //
      Offset(0.1, -0.1), Offset(0.08, 0.6), Offset(0.4, 0.65), Offset(0.4, 0.8), Offset(0.05, 0.85),
    ]),
    _MarkerShape.jet: _symmetric(const [
      Offset(0, -1), Offset(0.1, -0.85), Offset(0.1, -0.3), Offset(0.95, 0.2), Offset(0.95, 0.32), //
      Offset(0.1, 0.1), Offset(0.1, 0.6), Offset(0.4, 0.85), Offset(0.4, 0.95), Offset(0.06, 0.95),
    ]),
    _MarkerShape.fighter: _symmetric(const [
      Offset(0, -1), Offset(0.12, -0.5), Offset(0.8, 0.6), Offset(0.8, 0.75), Offset(0.15, 0.7), Offset(0.12, 0.9),
    ]),
    _MarkerShape.glider: _symmetric(const [
      Offset(0, -1), Offset(0.08, -0.8), Offset(0.08, -0.3), Offset(1, -0.22), Offset(1, -0.12), //
      Offset(0.08, -0.1), Offset(0.05, 0.75), Offset(0.3, 0.8), Offset(0.3, 0.9), Offset(0.03, 0.92),
    ]),
    _MarkerShape.helicopter: _union([
      Path()..addOval(Rect.fromCenter(center: const Offset(0, -0.25), width: 0.55, height: 0.9)),
      Path()..addRect(const Rect.fromLTRB(-0.06, 0, 0.06, 0.95)),
      Path()..addRect(const Rect.fromLTRB(-0.25, 0.78, 0.25, 0.88)),
      // Rotor blades.
      Path()..addRect(const Rect.fromLTRB(-0.95, -0.3, 0.95, -0.22)),
      Path()..addRect(const Rect.fromLTRB(-0.04, -1, 0.04, 0.5)),
    ]),
    _MarkerShape.balloon: _union([
      Path()..addOval(Rect.fromCircle(center: const Offset(0, -0.3), radius: 0.62)),
      Path()..addPolygon(const [Offset(-0.45, 0), Offset(0.45, 0), Offset(0.14, 0.6), Offset(-0.14, 0.6)], true),
      Path()..addRect(const Rect.fromLTRB(-0.14, 0.68, 0.14, 0.92)),
    ]),
    _MarkerShape.drone: _union([
      for (final (x, y) in const [(-1, -1), (1, -1), (-1, 1), (1, 1)])
        Path()..addOval(Rect.fromCircle(center: Offset(0.62 * x, 0.62 * y), radius: 0.32)),
      Path()..addPolygon(const [Offset(-0.7, -0.6), Offset(-0.6, -0.7), Offset(0.7, 0.6), Offset(0.6, 0.7)], true),
      Path()..addPolygon(const [Offset(0.7, -0.6), Offset(0.6, -0.7), Offset(-0.7, 0.6), Offset(-0.6, 0.7)], true),
      Path()..addRect(Rect.fromCircle(center: Offset.zero, radius: 0.22)),
    ]),
    _MarkerShape.vehicle: Path()
      ..addRRect(RRect.fromLTRBR(-0.45, -0.85, 0.45, 0.85, const Radius.circular(0.15))),
    _MarkerShape.obstacle: Path()
      ..addPolygon(const [Offset(0, -0.9), Offset(0.9, 0.75), Offset(-0.9, 0.75)], true),
  };

  /// Shapes whose heading means nothing, so they're drawn upright.
  static bool isUpright(_MarkerShape shape) =>
      shape == _MarkerShape.balloon || shape == _MarkerShape.obstacle;

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
  bool shouldRepaint(_MarkerPainter old) => old.shape != shape || old.color != color;
}

/// Colour for an altitude, on a scale like tar1090's: orange near the
/// ground, through green and blue, to purple at 40,000 ft and above.
Color _altitudeColor(int altFt) {
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
class _SignalBarsPainter extends CustomPainter {
  _SignalBarsPainter(this.filled, this.color, this.emptyColor);

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
  bool shouldRepaint(_SignalBarsPainter old) =>
      old.filled != filled || old.color != color || old.emptyColor != emptyColor;
}

/// An arrow through the centre, tilted with vertical rate: level at 0,
/// steepest (60 degrees) at +-[fullScaleFpm] and beyond.
class _SlopePainter extends CustomPainter {
  _SlopePainter(this.fpm, this.color);

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
  bool shouldRepaint(_SlopePainter old) => old.fpm != fpm || old.color != color;
}

/// A horizontal gauge, `fraction` full.
class _GaugePainter extends CustomPainter {
  _GaugePainter(this.fraction, this.color, this.trackColor);

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
  bool shouldRepaint(_GaugePainter old) =>
      old.fraction != fraction || old.color != color || old.trackColor != trackColor;
}

const _compassPoints = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];

/// A horizontal bar of _altitudeColor from 0 to 40,000 ft, for the map legend.
class _AltitudeScalePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    const steps = 40;
    final w = size.width / steps;
    for (var i = 0; i < steps; i++) {
      canvas.drawRect(
        Rect.fromLTWH(i * w, 0, w + 0.5, size.height),
        Paint()..color = _altitudeColor((i + 0.5) / steps * 40000 ~/ 1),
      );
    }
  }

  @override
  bool shouldRepaint(_AltitudeScalePainter old) => false;
}

/// The scrubber's overview strip: the whole recording [startS, endS] as a
/// histogram of `activity` (aircraft per `activityBucketS` bucket from
/// `activityT0`; each pixel shows the busiest bucket it covers), with ticks
/// (full-height lines at local midnight), the fine slider's window
/// [windowStartS, windowEndS] and the scrub position.
class _TimelinePainter extends CustomPainter {
  _TimelinePainter({
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
    // Aligned to local time, so midnight ticks land on midnight. Ignores a DST change within the span.
    final offsetS = DateTime.fromMillisecondsSinceEpoch((startS * 1000).round()).timeZoneOffset.inSeconds;
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
  bool shouldRepaint(_TimelinePainter old) =>
      old.startS != startS ||
      old.endS != endS ||
      old.windowStartS != windowStartS ||
      old.windowEndS != windowEndS ||
      old.positionS != positionS ||
      !identical(old.activity, activity) ||
      old.color != color ||
      old.trackColor != trackColor;
}

/// A line chart of `values` against `times` over [tMin, tMax]. Gaps of more
/// than [maxGapS] break the line; nulls are skipped. `colorOf` colours each
/// segment by its value; `zeroLine` draws y = 0 when it's in range.
/// `targets`, if given, is a second series on the same axis drawn as a dashed
/// step line in `targetColor`: a setting, such as the selected altitude, that
/// holds until the next point.
class _TimeSeriesPainter extends CustomPainter {
  _TimeSeriesPainter({
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
  bool shouldRepaint(_TimeSeriesPainter old) =>
      old.times != times ||
      old.values != values ||
      old.targets != targets ||
      old.tMin != tMin ||
      old.tMax != tMax;
}

/// What the emergency squawks mean.
const _emergencySquawks = {'7500': 'Hijack', '7600': 'Radio failure', '7700': 'Emergency'};

/// The sky at one time, from the backend's history db (--history-db; see
/// proto/src/output/aircraft_history.h: aircraft_history_snapshot): every
/// aircraft updated in the ten minutes before `timeS`, with its state as of
/// `timeS` and its track over those minutes.
class AircraftSnapshot {
  final double timeS;
  /// Time span the whole db covers; null while it's empty.
  final double? startS;
  final double? endS;
  final Map<String, AircraftRecord> aircraft;
  final Map<String, List<TrailPoint>> trails;

  AircraftSnapshot({
    required this.timeS,
    required this.startS,
    required this.endS,
    required this.aircraft,
    required this.trails,
  });

  factory AircraftSnapshot.fromJson(Map<String, dynamic> j) {
    final aircraft = <String, AircraftRecord>{};
    final trails = <String, List<TrailPoint>>{};
    for (final e in j['aircraft'] as List) {
      final a = AircraftRecord.fromJson(e['state'] as Map<String, dynamic>);
      aircraft[a.icao] = a;
      trails[a.icao] = [
        for (final p in e['track'] as List)
          (pos: LatLng((p[0] as num).toDouble(), (p[1] as num).toDouble()), altFt: (p[2] as num?)?.round()),
      ];
    }
    return AircraftSnapshot(
      timeS: (j['t'] as num).toDouble(),
      startS: (j['t_min'] as num?)?.toDouble(),
      endS: (j['t_max'] as num?)?.toDouble(),
      aircraft: aircraft,
      trails: trails,
    );
  }
}

/// One point of a map trail, with the altitude there to colour it by.
typedef TrailPoint = ({LatLng pos, int? altFt});

/// One update from an aircraft's history, for the detail panel's charts.
typedef HistoryPoint = ({double t, int? altFt, double? gsKt, int? vrFpm, double? signalDb, int? selAltFt});

HistoryPoint _historyPointOf(AircraftRecord a) => (
      t: a.lastSeenUnixS,
      altFt: a.altitudeFt,
      gsKt: a.groundSpeedKt,
      vrFpm: a.verticalRateFpm,
      signalDb: a.signalDb,
      selAltFt: a.selectedAltitudeFt,
    );

/// Local date and time of a Unix time, to the second.
String _formatTime(double unixS) {
  final t = DateTime.fromMillisecondsSinceEpoch((unixS * 1000).round());
  String two(int n) => n.toString().padLeft(2, '0');
  return '${t.year}-${two(t.month)}-${two(t.day)} ${two(t.hour)}:${two(t.minute)}:${two(t.second)}';
}

/// One FFT snapshot of the raw pre-resample IQ, as published by the
/// backend's --ws-port (see proto/src/dsp/spectrum.cpp: to_json). `bins` is
/// already fftshifted -- bin 0 is freqHz - rateHz/2, the last bin is
/// freqHz + rateHz/2 -- so it plots directly against a linear frequency axis.
class SpectrumFrame {
  final double freqHz;
  final double rateHz;
  final List<double> bins;

  SpectrumFrame({required this.freqHz, required this.rateHz, required this.bins});

  factory SpectrumFrame.fromJson(Map<String, dynamic> j) {
    return SpectrumFrame(
      freqHz: (j['freq_hz'] as num).toDouble(),
      rateHz: (j['rate_hz'] as num).toDouble(),
      bins: (j['bins'] as List).map((e) => (e as num).toDouble()).toList(),
    );
  }
}

// Downlink format codes ADS-B/Mode S actually defines. A frame decoded with
// a df outside this set almost certainly came from a false-positive preamble
// detection on noise, not a real transponder reply. df 24-31 all decode to
// Comm-D (ELM) -- the top 3 bits (110) are what's significant, not the full
// 5-bit value -- so the whole range counts as known.
/// DFs whose CRC validates the frame on its own.
const Set<int> _selfCheckDfs = {11, 17, 18};

/// DFs whose parity field is ICAO XOR CRC (DF24 is every DF from 24 up).
const Set<int> _addrParityDfs = {0, 4, 5, 16, 20, 21};

/// Sort order for the crc column: fail, unchecked, ok.
int _crcRank(FrameRecord f) => f.crcFixedBit != null ? 2 : f.crcOk ? 3 : f.crcChecked ? 0 : 1;

final Set<int> _knownDfValues = {0, 4, 5, 11, 16, 17, 18, 19, 20, 21, ...List.generate(8, (i) => 24 + i)};

/// Compares optional numbers, nulls first.
int _compareNullable(num? a, num? b) {
  if (a == null && b == null) return 0;
  if (a == null) return -1;
  if (b == null) return 1;
  return a.compareTo(b);
}

/// A Listenable notified by hand: the page notifies one of these when a piece of its state changes, and only
/// the widgets showing that piece (ListenableBuilders on it) rebuild, not the whole page.
class _Changed extends ChangeNotifier {
  void notify() => notifyListeners();
}

class FrameTablePage extends StatefulWidget {
  const FrameTablePage({super.key});

  @override
  State<FrameTablePage> createState() => _FrameTablePageState();
}

class _FrameTablePageState extends State<FrameTablePage> {
  // 127.0.0.1, not "localhost": the backend's WsListener binds an IPv4
  // socket only, and "localhost" can resolve to ::1 first on Linux, which
  // fails to connect instead of falling back to the v4 address.
  final _urlController = TextEditingController(text: 'ws://127.0.0.1:8765');
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connected = false;
  String? _error;

  // Frames arrive far faster than the table can usefully redraw or the user
  // can read, so incoming messages are buffered here and only merged into
  // state on a timer -- see _flushUpdates. setState is only for the
  // connection controls; data changes notify the _Changed below, so each
  // flush rebuilds just the widgets showing what changed.
  static const int _maxFrames = 5000;
  static const Duration _uiUpdateInterval = Duration(seconds: 1);

  final Queue<FrameRecord> _frames = Queue<FrameRecord>();
  final List<FrameRecord> _pendingFrames = [];
  int _frameSortColumn = 0;
  bool _frameSortAscending = false;
  // _frames sorted for the table; null once stale (new frames or sort change).
  List<FrameRecord>? _sortedFramesCache;
  // While the frame table is paused, what it shows: _frames as of pausing.
  // Frames keep arriving into _frames and the stats meanwhile.
  List<FrameRecord>? _pausedFrames;
  int _framesSincePause = 0;
  // Frame log and stats.
  final _framesChanged = _Changed();

  // Cumulative counts across the whole connection, independent of the
  // _maxFrames display cap -- a frame that ages out of the table still
  // counts here. Rate is computed per flush interval only (no smoothing
  // across intervals), so it reflects exactly what arrived in that window.
  int _statsTotalFrames = 0;
  // Self-checking formats (DF11/17/18): a correct frame always passes, so this is the clean pass rate.
  int _statsSelfCheck = 0;
  int _statsSelfCheckOk = 0;
  // Frames that passed only after single-bit error correction (DF17/18); counted in _statsSelfCheckOk too.
  int _statsCrcFixed = 0;
  // Address/parity formats: pass only if the address was already confirmed, so a failure is either
  // corruption or an aircraft not yet heard on DF11/17/18 -- the two can't be told apart per frame.
  int _statsAddrParity = 0;
  int _statsAddrParityOk = 0;
  int _statsCrcUnchecked = 0;
  int _statsUnknownDf = 0;
  final Map<int, int> _statsByDf = {};
  double _statsFramesPerSec = 0;
  DateTime? _statsWindowStart;

  final Map<String, AircraftRecord> _aircraft = {};
  final Map<String, AircraftRecord> _pendingAircraft = {};

  // Accumulated in-memory position history per aircraft, oldest first, for
  // drawing a route trail on the map -- purely a running log of what's
  // arrived since this client connected (see AircraftRecord's doc comment:
  // each update is the aircraft's full current state, not a delta, so the
  // trail has to be built up here rather than read off any single message).
  // Capped per-aircraft so a long-running session doesn't grow this map
  // unboundedly; the oldest points are dropped first, same trim policy as
  // _frames.
  static const int _maxTrailPoints = 1000;
  final Map<String, List<TrailPoint>> _aircraftTrails = {};
  int _aircraftSortColumn = 0;
  bool _aircraftSortAscending = true;
  List<AircraftRecord>? _sortedAircraftCache;
  // Whether the aircraft table and map show stale aircraft (greyed out) or hide them.
  bool _showStale = true;
  // Whether the table lists only aircraft with a position (the map only ever shows those).
  bool _positionOnly = false;
  // Aircraft search, lowercased; matched against the text columns.
  final _searchController = TextEditingController();
  String _search = '';
  // Aircraft, trails, selection and the table/map split.
  final _aircraftChanged = _Changed();
  // Every flush, for the "last seen" ago-labels, which advance even with no new data.
  final _clockTick = _Changed();

  // History scrubbing, with the backend's --history-db. On connect the
  // backend sends one snapshot of the recent sky, which backfills the live
  // state and turns the scrubber on; after that it sends one per seek.
  bool _backfilled = false;
  // Span the scrubber covers (Unix s): the db's first update to the newest,
  // live ones included. Null without a history db.
  double? _historyStartS;
  double? _historyEndS;
  // Scrubber position, or null when live.
  double? _scrubTimeS;
  // The fine slider covers _scrubWindowS starting at _scrubWindowStartS, or the
  // newest _scrubWindowS when that's null (live, or never moved). The overview
  // strip above it spans the whole history and moves the window.
  static const _scrubWindowChoicesS = [600.0, 1800.0, 3600.0, 3 * 3600.0, 12 * 3600.0];
  double _scrubWindowS = 1800;
  double? _scrubWindowStartS;
  // Distinct aircraft per _activityBucketS bucket, consecutive from
  // _activityT0 (Unix s), for the overview strip's histogram. Replaced, not
  // mutated, on each activity reply, so the painter can tell it changed.
  // Fetched whole after the backfill, then the tail every _activityPollInterval.
  static const _activityPollInterval = Duration(seconds: 30);
  List<int> _activity = const [];
  double? _activityT0;
  double _activityBucketS = 60;
  int _activityPeakCount = 0;
  bool _activityInFlight = false;
  Timer? _activityTimer;
  // The map's heatmap of where aircraft flew (see _requestHeatmap): how far back from the shown time it
  // looks, null when it's off and infinity for all history.
  double? _heatmapRangeS;
  static const _heatmapRangeChoicesS = [3600.0, 6 * 3600.0, 86400.0, 7 * 86400.0, double.infinity];
  // Grid cell size, in logical pixels of the map.
  static const _heatmapCellPx = 4.0;
  OverlayImage? _heatmapOverlay;
  // Flights per cell at the dark end of the heatmap's colours, for its legend.
  double _heatmapTop = 0;
  // One request in flight at a time; a view change meanwhile asks again when it lands.
  bool _heatmapInFlight = false;
  bool _heatmapQueued = false;
  // Waits for panning and zooming to settle before asking.
  Timer? _heatmapDebounce;
  // _mapController.camera throws until the map has been laid out.
  bool _mapReady = false;
  // Latest snapshot for _scrubTimeS, shown instead of the live aircraft while scrubbing.
  AircraftSnapshot? _snapshot;
  // One seek in flight at a time; the newest position asked for meanwhile waits here.
  bool _seekInFlight = false;
  double? _queuedSeekS;
  Timer? _uiUpdateTimer;

  // Aircraft older than this (relative to the newest last_seen_unix_s we've
  // received, or the scrubbed-to time) are shown greyed-out rather than
  // removed outright. Relative to the newest update rather than
  // DateTime.now(), since in file-playback mode the backend's times run
  // faster or slower than real time.
  static const double _staleAfterS = 30;
  double _latestTimeS = 0;

  // Wall-clock time each aircraft's state was last updated, for the "last
  // seen" table column -- shown as seconds-ago from the real current time,
  // not stream position, since that's what the person watching the table
  // actually wants to know.
  final Map<String, DateTime> _aircraftLastSeenWallClock = {};

  // Selecting a row in the aircraft table highlights the matching marker on
  // the map, and vice versa.
  String? _selectedIcao;
  // The selected aircraft's history for the detail panel: fetched from the
  // backend's history db on selection (and on each seek while scrubbing),
  // then extended with live updates. Oldest first.
  List<HistoryPoint> _selectedHistory = [];
  // The aircraft _selectedHistory is for.
  String? _selectedHistoryIcao;

  final _mapController = MapController();
  // Center the map on the first aircraft position we see, then leave the
  // user's pan/zoom alone -- re-centering on every update would fight
  // anyone trying to look around.
  bool _mapAutoCentered = false;

  // Fraction of the aircraft pane's height given to the map, above the
  // full-width table; dragged via the divider in _buildAircraftSplit.
  double _mapFraction = 0.5;
  static const double _minPaneHeight = 120;
  // Dock to the right of the map and table, for the frame log and the
  // spectrum waterfall, each shown on demand from the top bar (stacked when
  // both are). Its width is dragged from its left edge.
  bool _showFrames = false;
  bool _showSpectrum = false;
  double _dockWidth = 560;
  static const double _minDockWidth = 400;
  static const double _minMapWidth = 320;

  // Spectrum snapshots arrive far slower than frames (backend throttles to
  // one every 150ms regardless of sample rate -- see kSpectrumInterval in
  // main.cpp), so unlike frames/aircraft these go straight into their own
  // ValueNotifier on arrival instead of through the once-a-second
  // _flushUpdates batch: only the waterfall widget listening to it repaints,
  // not the whole page, and at ~7Hz that's cheap either way.
  static const int _maxSpectrumRows = 150;
  final ValueNotifier<List<SpectrumFrame>> _spectrumHistory = ValueNotifier<List<SpectrumFrame>>([]);

  static final List<_TableColumn<FrameRecord>> _frameColumns = [
    _TableColumn('idx', 110, numeric: true, compare: (a, b) => a.idx.compareTo(b.idx), cell: (f) => Text('${f.idx}')),
    _TableColumn('df', 50, numeric: true, compare: (a, b) => a.df.compareTo(b.df), cell: (f) => Text('${f.df}')),
    _TableColumn('icao', 80,
        compare: (a, b) => (a.icao ?? '').compareTo(b.icao ?? ''), cell: (f) => Text(f.icao ?? '??????')),
    _TableColumn('confidence', 100,
        numeric: true,
        compare: (a, b) => a.confidence.compareTo(b.confidence),
        cell: (f) => Text(f.confidence.toStringAsFixed(1))),
    _TableColumn('crc', 60, compare: (a, b) => _crcRank(a).compareTo(_crcRank(b)), cell: (f) {
      if (f.crcFixedBit != null) {
        return Tooltip(
          message: 'bit ${f.crcFixedBit} corrected',
          child: Text('fixed', style: TextStyle(color: Colors.amber.shade800)),
        );
      }
      return Text(
        f.crcOk ? 'ok' : f.crcChecked ? 'fail' : 'n/a',
        style: TextStyle(color: f.crcOk ? Colors.green : f.crcChecked ? Colors.red : Colors.grey),
      );
    }),
    _TableColumn('decoded', 300,
        compare: (a, b) => (a.summary ?? '').compareTo(b.summary ?? ''),
        cell: (f) => Tooltip(
              message: f.summary ?? '',
              child: Text(f.summary ?? '', maxLines: 1, overflow: TextOverflow.ellipsis),
            )),
    _TableColumn('bits', 50, numeric: true, cell: (f) => Text('${f.bits}')),
    _TableColumn('payload', 270, cell: (f) => Text(f.payload, style: const TextStyle(fontFamily: 'monospace'))),
  ];

  // Colour for a visual in `a`'s row: `color`, or grey if `a` is stale, to
  // match the row's greyed-out text.
  Color _rowColor(AircraftRecord a, Color color) => _isStale(a) ? Colors.grey.shade400 : color;

  // A numeric cell with a small visual just left of the value. The value is
  // right-aligned in a fixed `textWidth` (fitting the column's widest
  // value), so values and visuals both line up down the column.
  static Widget _visualCell(Widget visual, String text, double textWidth) => Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          visual,
          const SizedBox(width: 6),
          SizedBox(width: textWidth, child: Text(text, textAlign: TextAlign.right)),
        ],
      );

  // The small visuals shown beside values, in the table and the map
  // tooltips. Grey when `a` is stale.

  // Gauge to 45,000 ft, in the altitude's colour.
  Widget _altitudeVisual(AircraftRecord a, int alt) => CustomPaint(
        size: const Size(32, 6),
        painter: _GaugePainter(alt / 45000, _rowColor(a, _altitudeColor(alt)), Colors.grey.shade200),
      );

  // Gauge to 600 kt.
  Widget _speedVisual(AircraftRecord a, double gs) => CustomPaint(
        size: const Size(32, 6),
        painter: _GaugePainter(gs / 600, _rowColor(a, Colors.blueGrey), Colors.grey.shade200),
      );

  // Arrow pointing along the track (north up), and the compass point.
  Widget _trackVisual(AircraftRecord a, double trk) => Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Transform.rotate(
            angle: trk * math.pi / 180,
            child: Icon(Icons.navigation, size: 16, color: _rowColor(a, Colors.blueGrey)),
          ),
          const SizedBox(width: 4),
          SizedBox(width: 22, child: Text(_compassPoints[((trk + 22.5) / 45).floor() % 8])),
        ],
      );

  // Arrow tilted with the climb or descent: green up, orange down, grey
  // within +-100 fpm (level flight).
  Widget _verticalRateVisual(AircraftRecord a, int vr) {
    final color = vr > 100 ? Colors.green.shade600 : (vr < -100 ? Colors.orange.shade800 : Colors.grey);
    return CustomPaint(size: const Size(20, 20), painter: _SlopePainter(vr, _rowColor(a, color)));
  }

  // One bar per 3 dB, from red (1) to green (4-5).
  Widget _signalVisual(AircraftRecord a, double db) {
    final bars = (db / 3).floor().clamp(0, _SignalBarsPainter.count - 1) + 1;
    final color = const [Colors.red, Colors.orange, Colors.amber, Colors.green, Colors.green][bars - 1];
    return CustomPaint(
      size: const Size(18, 14),
      painter: _SignalBarsPainter(bars, _rowColor(a, color), Colors.grey.shade300),
    );
  }

  // Dot fading from green to grey as the aircraft goes quiet.
  Widget _ageVisual(double age) {
    final color = age < 5
        ? Colors.green
        : age < 15
            ? Colors.lightGreen
            : age < _staleAfterS
                ? Colors.orange
                : Colors.grey.shade400;
    return Container(width: 8, height: 8, decoration: BoxDecoration(color: color, shape: BoxShape.circle));
  }

  Widget _typeVisual(AircraftRecord a, String category) {
    final (shape, _) = _categoryMarkers[category] ?? (_MarkerShape.unknown, 0.9);
    return CustomPaint(
      size: const Size.square(16),
      painter: _MarkerPainter(shape, _rowColor(a, Theme.of(context).colorScheme.primary)),
    );
  }

  // Badges for the flags that are set: on the ground, alert (squawk changed or an emergency) and SPI
  // (ident). `tooltips` is off inside the map marker's own tooltip.
  List<Widget> _statusBadges(AircraftRecord a, {bool tooltips = true}) {
    Widget badge(String text, Color color, String meaning) {
      final c = _rowColor(a, color);
      final box = Container(
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
        decoration: BoxDecoration(border: Border.all(color: c), borderRadius: BorderRadius.circular(3)),
        child: Text(text, style: TextStyle(color: c, fontSize: 10, fontWeight: FontWeight.w600)),
      );
      return tooltips ? Tooltip(message: meaning, child: box) : box;
    }

    return [
      if (a.onGround == true) badge('GND', Colors.brown, 'On the ground'),
      if (a.alert) badge('ALERT', Colors.red, 'Alert: squawk changed, or an emergency'),
      if (a.spi) badge('SPI', Colors.purple, 'Ident (special position indicator)'),
    ];
  }

  // Sort order of the status column: alerts first, then ident, then on the ground.
  static int _statusRank(AircraftRecord a) =>
      (a.alert ? 4 : 0) + (a.spi ? 2 : 0) + (a.onGround == true ? 1 : 0);

  late final List<_TableColumn<AircraftRecord>> _aircraftColumns = [
    _TableColumn('icao', 70, compare: (a, b) => a.icao.compareTo(b.icao), cell: (a) => Text(a.icao)),
    _TableColumn('callsign', 85,
        compare: (a, b) => (a.callsign ?? '').compareTo(b.callsign ?? ''), cell: (a) => Text(a.callsign ?? '')),
    _TableColumn('squawk', 70, compare: (a, b) => (a.squawk ?? '').compareTo(b.squawk ?? ''), cell: (a) {
      final emergency = _emergencySquawks[a.squawk];
      if (emergency == null) return Text(a.squawk ?? '');
      return Tooltip(
        message: emergency,
        child: Text(a.squawk!, style: const TextStyle(color: Colors.red, fontWeight: FontWeight.bold)),
      );
    }),
    _TableColumn('type', 135, compare: (a, b) => (a.category ?? '').compareTo(b.category ?? ''), cell: (a) {
      final code = a.category;
      if (code == null) return const Text('');
      return Tooltip(
        message: code,
        child: Row(
          children: [
            _typeVisual(a, code),
            const SizedBox(width: 6),
            Flexible(child: Text(_categoryNames[code] ?? code)),
          ],
        ),
      );
    }),
    _TableColumn('alt (ft)', 120,
        numeric: true,
        compare: (a, b) => _compareNullable(a.altitudeFt, b.altitudeFt),
        cell: (a) {
          final alt = a.altitudeFt;
          if (alt == null) return const Text('');
          return _visualCell(_altitudeVisual(a, alt), '$alt', 42);
        }),
    _TableColumn('lat', 85,
        numeric: true,
        compare: (a, b) => _compareNullable(a.lat, b.lat),
        cell: (a) => Text(a.lat?.toStringAsFixed(4) ?? '')),
    _TableColumn('lon', 95,
        numeric: true,
        compare: (a, b) => _compareNullable(a.lon, b.lon),
        cell: (a) => Text(a.lon?.toStringAsFixed(4) ?? '')),
    _TableColumn('gs (kt)', 105,
        numeric: true,
        compare: (a, b) => _compareNullable(a.groundSpeedKt, b.groundSpeedKt),
        cell: (a) {
          final gs = a.groundSpeedKt;
          if (gs == null) return const Text('');
          return _visualCell(_speedVisual(a, gs), gs.toStringAsFixed(0), 28);
        }),
    _TableColumn('trk', 105,
        numeric: true,
        compare: (a, b) => _compareNullable(a.trackDeg, b.trackDeg),
        cell: (a) {
          final trk = a.trackDeg;
          if (trk == null) return const Text('');
          return _visualCell(_trackVisual(a, trk), '${trk.toStringAsFixed(0)}°', 32);
        }),
    _TableColumn('vr (fpm)', 110,
        numeric: true,
        compare: (a, b) => _compareNullable(a.verticalRateFpm, b.verticalRateFpm),
        cell: (a) {
          final vr = a.verticalRateFpm;
          if (vr == null) return const Text('');
          return _visualCell(_verticalRateVisual(a, vr), '$vr', 42);
        }),
    _TableColumn('sel alt', 80,
        numeric: true,
        compare: (a, b) => _compareNullable(a.selectedAltitudeFt, b.selectedAltitudeFt),
        cell: (a) => Text(a.selectedAltitudeFt?.toString() ?? '')),
    _TableColumn('status', 120,
        compare: (a, b) => _statusRank(a).compareTo(_statusRank(b)),
        cell: (a) => Wrap(spacing: 4, children: _statusBadges(a))),
    _TableColumn('msgs', 70,
        numeric: true, compare: (a, b) => a.messages.compareTo(b.messages), cell: (a) => Text('${a.messages}')),
    _TableColumn('sig (dB)', 100,
        numeric: true,
        compare: (a, b) => _compareNullable(a.signalDb, b.signalDb),
        cell: (a) {
          final db = a.signalDb;
          if (db == null) return const Text('');
          return _visualCell(_signalVisual(a, db), db.toStringAsFixed(1), 32);
        }),
    _TableColumn('last seen', 100,
        numeric: true,
        compare: (a, b) => a.lastSeenUnixS.compareTo(b.lastSeenUnixS),
        cell: (a) => ListenableBuilder(
              listenable: _clockTick,
              builder: (context, _) {
                final age = _ageS(a);
                if (age == null) return const Text('');
                return _visualCell(_ageVisual(age), '${age.toStringAsFixed(0)}s ago', 60);
              },
            )),
  ];

  @override
  void dispose() {
    _uiUpdateTimer?.cancel();
    _activityTimer?.cancel();
    _heatmapDebounce?.cancel();
    _sub?.cancel();
    _channel?.sink.close();
    _urlController.dispose();
    _searchController.dispose();
    _spectrumHistory.dispose();
    _framesChanged.dispose();
    _aircraftChanged.dispose();
    _clockTick.dispose();
    super.dispose();
  }

  Future<void> _connect() async {
    final url = _urlController.text.trim();
    if (url.isEmpty) return;
    _uiUpdateTimer?.cancel();
    _frames.clear();
    _pendingFrames.clear();
    _sortedFramesCache = null;
    _pausedFrames = null;
    _aircraft.clear();
    _pendingAircraft.clear();
    _sortedAircraftCache = null;
    _aircraftTrails.clear();
    _backfilled = false;
    _historyStartS = null;
    _historyEndS = null;
    _scrubTimeS = null;
    _scrubWindowStartS = null;
    _activityTimer?.cancel();
    _activity = const [];
    _activityT0 = null;
    _activityPeakCount = 0;
    _activityInFlight = false;
    _heatmapOverlay = null;
    _heatmapInFlight = false;
    _heatmapQueued = false;
    _snapshot = null;
    _seekInFlight = false;
    _queuedSeekS = null;
    _mapAutoCentered = false;
    _latestTimeS = 0;
    _aircraftLastSeenWallClock.clear();
    _selectedIcao = null;
    _selectedHistory = [];
    _selectedHistoryIcao = null;
    _statsTotalFrames = 0;
    _statsSelfCheck = 0;
    _statsSelfCheckOk = 0;
    _statsCrcFixed = 0;
    _statsAddrParity = 0;
    _statsAddrParityOk = 0;
    _statsCrcUnchecked = 0;
    _statsUnknownDf = 0;
    _statsByDf.clear();
    _statsFramesPerSec = 0;
    _statsWindowStart = DateTime.now();
    _spectrumHistory.value = [];
    _framesChanged.notify();
    _aircraftChanged.notify();
    setState(() => _error = null);
    final channel = WebSocketChannel.connect(Uri.parse(url));
    _channel = channel;
    try {
      await channel.ready;
    } catch (e) {
      setState(() {
        _error = '$e';
        _connected = false;
      });
      return;
    }
    _sub = channel.stream.listen(
      (event) {
        // Frames/aircraft can arrive at rates far higher than the UI can
        // (or should) redraw at, so just buffer them here -- _flushUpdates,
        // on a timer, is what actually triggers a rebuild.
        try {
          final json = jsonDecode(event as String) as Map<String, dynamic>;
          switch (json['type']) {
            case 'frame':
              _pendingFrames.add(FrameRecord.fromJson(json));
            case 'aircraft':
              final ac = AircraftRecord.fromJson(json);
              _pendingAircraft[ac.icao] = ac;
            case 'snapshot':
              _onSnapshot(AircraftSnapshot.fromJson(json));
            case 'aircraft_history':
              _onAircraftHistory(json);
            case 'heatmap':
              _onHeatmap(json);
            case 'activity':
              _onActivity(json);
            case 'spectrum':
              final rows = [..._spectrumHistory.value, SpectrumFrame.fromJson(json)];
              if (rows.length > _maxSpectrumRows) {
                rows.removeRange(0, rows.length - _maxSpectrumRows);
              }
              _spectrumHistory.value = rows;
          }
        } catch (e, st) {
          // Skip the message rather than tearing down the connection, but say
          // why: a handler that throws here fails silently otherwise.
          debugPrint('dropped ws message: $e\n$st');
        }
      },
      onError: (e) => _disconnect(error: '$e'),
      onDone: _disconnect,
      cancelOnError: true,
    );
    _uiUpdateTimer = Timer.periodic(_uiUpdateInterval, (_) => _flushUpdates());
    _activityTimer = Timer.periodic(_activityPollInterval, (_) {
      _requestActivity();
      // A live heatmap grows with the history; a scrubbed one doesn't change.
      if (_snapshot == null) _requestHeatmap();
    });
    setState(() => _connected = true);
  }

  /// Merges buffered frames/aircraft into the displayed state, trims the
  /// frame log to _maxFrames (dropping the oldest), runs once-only map
  /// auto-centering off the batch's first new position, and notifies whatever
  /// changed.
  void _flushUpdates() {
    final now = DateTime.now();
    final elapsedS = now.difference(_statsWindowStart ?? now).inMicroseconds / 1e6;
    final framesPerSec = elapsedS > 0 ? _pendingFrames.length / elapsedS : 0.0;
    _statsWindowStart = now;

    if (_pendingFrames.isNotEmpty || framesPerSec != _statsFramesPerSec) {
      _statsFramesPerSec = framesPerSec;
      for (final f in _pendingFrames) {
        _statsTotalFrames++;
        if (_selfCheckDfs.contains(f.df)) {
          _statsSelfCheck++;
          if (f.crcOk) _statsSelfCheckOk++;
          if (f.crcFixedBit != null) _statsCrcFixed++;
        } else if (_addrParityDfs.contains(f.df) || f.df >= 24) {
          _statsAddrParity++;
          if (f.crcOk) _statsAddrParityOk++;
        } else if (!f.crcChecked) {
          _statsCrcUnchecked++;
        }
        if (!_knownDfValues.contains(f.df)) _statsUnknownDf++;
        _statsByDf[f.df] = (_statsByDf[f.df] ?? 0) + 1;
      }

      _frames.addAll(_pendingFrames);
      while (_frames.length > _maxFrames) {
        _frames.removeFirst();
      }
      if (_pausedFrames == null) {
        _sortedFramesCache = null;
      } else {
        _framesSincePause += _pendingFrames.length;
      }
      _pendingFrames.clear();
      _framesChanged.notify();
    }

    if (_pendingAircraft.isNotEmpty) {
      _aircraft.addAll(_pendingAircraft);
      LatLng? firstNewPosition;
      if (!_mapAutoCentered) {
        for (final ac in _pendingAircraft.values) {
          if (ac.lat != null && ac.lon != null) {
            firstNewPosition = LatLng(ac.lat!, ac.lon!);
            break;
          }
        }
      }
      for (final ac in _pendingAircraft.values) {
        if (ac.lastSeenUnixS > _latestTimeS) _latestTimeS = ac.lastSeenUnixS;
        _aircraftLastSeenWallClock[ac.icao] = now;
        if (ac.icao == _selectedIcao && _scrubTimeS == null) _appendHistory(_historyPointOf(ac));
        if (ac.lat != null && ac.lon != null) {
          final trail = _aircraftTrails.putIfAbsent(ac.icao, () => []);
          trail.add((pos: LatLng(ac.lat!, ac.lon!), altFt: ac.altitudeFt));
          if (trail.length > _maxTrailPoints) {
            trail.removeRange(0, trail.length - _maxTrailPoints);
          }
        }
      }
      _pendingAircraft.clear();
      _sortedAircraftCache = null;
      if (_backfilled) {
        _historyStartS ??= _latestTimeS;
        _historyEndS = math.max(_historyEndS ?? _latestTimeS, _latestTimeS);
      }

      if (firstNewPosition != null) {
        _mapAutoCentered = true;
        final pos = firstNewPosition;
        WidgetsBinding.instance.addPostFrameCallback((_) {
          try {
            _mapController.move(pos, 9);
          } catch (_) {
            // Map isn't mounted yet -- it'll just show the default view.
          }
        });
      }
      _aircraftChanged.notify();
    }

    _clockTick.notify();
  }

  // What the map and aircraft table show: the scrubbed-to snapshot, or live.
  Map<String, AircraftRecord> get _shownAircraft => _snapshot?.aircraft ?? _aircraft;
  Map<String, List<TrailPoint>> get _shownTrails => _snapshot?.trails ?? _aircraftTrails;
  double get _shownTimeS => _snapshot?.timeS ?? _latestTimeS;

  bool _isStale(AircraftRecord a) => _shownTimeS - a.lastSeenUnixS > _staleAfterS;

  // _shownAircraft through the bottom bar's filters, for the table and map.
  Iterable<AircraftRecord> get _visibleAircraft => _shownAircraft.values.where(
        (a) => (_showStale || !_isStale(a)) && (!_positionOnly || a.lat != null) && _matchesSearch(a),
      );

  bool _matchesSearch(AircraftRecord a) =>
      _search.isEmpty ||
      [a.icao, a.callsign, a.squawk, a.category, _categoryNames[a.category]]
          .any((field) => field != null && field.toLowerCase().contains(_search));

  void _onAircraftFilterChanged() {
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  /// Live, time since the update arrived; scrubbing, time before the scrubbed-to time.
  double? _ageS(AircraftRecord a) {
    final snapshot = _snapshot;
    if (snapshot != null) return snapshot.timeS - a.lastSeenUnixS;
    final t = _aircraftLastSeenWallClock[a.icao];
    if (t == null) return null;
    return DateTime.now().difference(t).inMilliseconds / 1000;
  }

  /// Applies a snapshot from the backend. The first, sent on connect before
  /// any live message, backfills the live aircraft and trails; the rest
  /// answer seeks and are shown while scrubbing.
  void _onSnapshot(AircraftSnapshot s) {
    if (s.startS != null) {
      _historyStartS = math.min(_historyStartS ?? s.startS!, s.startS!);
      _historyEndS = math.max(_historyEndS ?? s.endS!, s.endS!);
    }
    if (!_backfilled) {
      _backfilled = true;
      _requestActivity();
      _scheduleHeatmap();
      final now = DateTime.now();
      _aircraft.addAll(s.aircraft);
      _aircraftTrails.addAll(s.trails);
      for (final a in s.aircraft.values) {
        _aircraftLastSeenWallClock[a.icao] =
            now.subtract(Duration(milliseconds: ((s.timeS - a.lastSeenUnixS) * 1000).round()));
        _latestTimeS = math.max(_latestTimeS, a.lastSeenUnixS);
      }
    } else {
      _seekInFlight = false;
      // Ignore a reply that lands after going back to live.
      if (_scrubTimeS != null) {
        _snapshot = s;
        _requestSelectedHistory();
        _scheduleHeatmap();
      }
      final queued = _queuedSeekS;
      _queuedSeekS = null;
      if (queued != null) _seek(queued);
    }
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  /// Asks for the activity histogram: all of it at first, then from the
  /// last bucket (which may have been still filling) on. Needs a history db.
  void _requestActivity() {
    if (!_backfilled || _historyStartS == null || _activityInFlight) return;
    _activityInFlight = true;
    final t0 = _activityT0;
    final from = t0 == null ? null : t0 + math.max(0, _activity.length - 1) * _activityBucketS;
    _channel?.sink.add(jsonEncode({'type': 'activity', 'from': ?from}));
  }

  /// Merges an activity reply: its counts replace ours from its t0 on.
  void _onActivity(Map<String, dynamic> j) {
    _activityInFlight = false;
    final t0 = (j['t0'] as num?)?.toDouble();
    if (t0 == null) return;
    final bucketS = (j['bucket_s'] as num).toDouble();
    final counts = [for (final c in j['counts'] as List) (c as num).toInt()];
    final oldT0 = _activityT0;
    final List<int> merged;
    if (oldT0 == null || bucketS != _activityBucketS || t0 < oldT0) {
      merged = counts;
      _activityT0 = t0;
    } else {
      final keep = ((t0 - oldT0) / bucketS).round();
      merged = [
        ..._activity.take(keep),
        ...List.filled(math.max(0, keep - _activity.length), 0),
        ...counts,
      ];
    }
    _activityBucketS = bucketS;
    _activity = merged;
    _activityPeakCount = merged.fold(0, math.max);
    _aircraftChanged.notify();
  }

  /// Asks for the heatmap of the map's current view over _heatmapRangeS up to
  /// the shown time. Needs a history db; does nothing while it's off.
  void _requestHeatmap() {
    final range = _heatmapRangeS;
    if (range == null || !_mapReady || _channel == null || _historyStartS == null) return;
    if (_heatmapInFlight) {
      _heatmapQueued = true;
      return;
    }
    final camera = _mapController.camera;
    final bounds = camera.visibleBounds;
    final t1 = _snapshot?.timeS; // null: up to the latest
    _heatmapInFlight = true;
    _channel!.sink.add(jsonEncode({
      'type': 'heatmap',
      'west': bounds.west,
      'south': bounds.south,
      'east': bounds.east,
      'north': bounds.north,
      'width': (camera.size.width / _heatmapCellPx).ceil(),
      'height': (camera.size.height / _heatmapCellPx).ceil(),
      't0': ?(range.isFinite ? (t1 ?? _latestTimeS) - range : null),
      't1': ?t1,
    }));
  }

  void _scheduleHeatmap() {
    if (_heatmapRangeS == null) return;
    _heatmapDebounce?.cancel();
    _heatmapDebounce = Timer(const Duration(milliseconds: 300), _requestHeatmap);
  }

  /// Draws a heatmap reply as an image over the area it covers.
  Future<void> _onHeatmap(Map<String, dynamic> j) async {
    _heatmapInFlight = false;
    if (_heatmapQueued) {
      _heatmapQueued = false;
      _requestHeatmap();
    }
    if (_heatmapRangeS == null) return;
    final width = j['width'] as int;
    final height = j['height'] as int;
    final counts = Float64List(width * height);
    final cells = j['cells'] as List;
    for (var i = 0; i + 1 < cells.length; i += 2) {
      counts[cells[i] as int] = (cells[i + 1] as num).toDouble();
    }
    final bounds = LatLngBounds(
      LatLng((j['south'] as num).toDouble(), (j['west'] as num).toDouble()),
      LatLng((j['north'] as num).toDouble(), (j['east'] as num).toDouble()),
    );
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

  // Colours of the heatmap, from few flights to many: light to dark so busy
  // areas stand out on the light map (ColorBrewer's YlOrRd, run on to near black).
  static const _heatmapStops = [
    Color(0xffffeda0), Color(0xfffeb24c), Color(0xfffc4e2a), Color(0xffbd0026), Color(0xff4d0026),
  ];

  static final _heatmapLut = [
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
  static (Uint8List, double) _heatmapPixels(Float64List counts, int width, int height) {
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

  static String _heatmapRangeLabel(double s) =>
      s.isInfinite ? 'all' : s >= 86400 ? '${(s / 86400).round()} d' : _windowLabel(s);

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
        for (final s in _heatmapRangeChoicesS)
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

  void _seek(double t) {
    if (_seekInFlight) {
      _queuedSeekS = t;
      return;
    }
    _seekInFlight = true;
    _channel?.sink.add(jsonEncode({'type': 'seek', 't': t}));
  }

  void _onScrub(double t) {
    _scrubTimeS = t;
    _seek(t);
    _aircraftChanged.notify();
  }

  /// The fine slider's range within [start, end]: _scrubWindowS long (or all
  /// of the history when shorter), from _scrubWindowStartS or ending at end.
  (double, double) _scrubWindow(double start, double end) {
    final length = math.min(_scrubWindowS, end - start);
    final lo = (_scrubWindowStartS ?? end - length).clamp(start, end - length);
    return (lo, lo + length);
  }

  /// Jumps to `t` from the overview strip, ◀/▶ or the time picker: centres
  /// the window on it and scrubs there. At or past the end, goes live.
  void _jumpTo(double t) {
    final start = _historyStartS;
    final end = _historyEndS;
    if (start == null || end == null) return;
    if (t >= end) {
      _goLive();
      return;
    }
    t = math.max(t, start);
    _scrubWindowStartS = t - _scrubWindowS / 2;
    _onScrub(t);
  }

  Future<void> _pickScrubTime() async {
    final start = _historyStartS;
    final end = _historyEndS;
    if (start == null || end == null) return;
    DateTime local(double s) => DateTime.fromMillisecondsSinceEpoch((s * 1000).round());
    final current = local(_scrubTimeS ?? end);
    final first = local(start);
    final last = local(end);
    final date = await showDatePicker(
      context: context,
      initialDate: current,
      firstDate: DateTime(first.year, first.month, first.day),
      lastDate: last,
    );
    if (date == null || !mounted) return;
    final time = await showTimePicker(context: context, initialTime: TimeOfDay.fromDateTime(current));
    if (time == null) return;
    _jumpTo(DateTime(date.year, date.month, date.day, time.hour, time.minute).millisecondsSinceEpoch / 1000);
  }

  void _goLive() {
    _scrubWindowStartS = null;
    _scrubTimeS = null;
    _snapshot = null;
    _requestSelectedHistory();
    _scheduleHeatmap();
    _queuedSeekS = null;
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  void _toggleSelected(String icao) {
    _selectedIcao = _selectedIcao == icao ? null : icao;
    _requestSelectedHistory();
    _aircraftChanged.notify();
  }

  /// Asks the backend for the selected aircraft's history up to the shown
  /// time. A newly selected aircraft's history restarts from what's shown
  /// now; otherwise (scrubbing, going live) the current charts stay up until
  /// the reply replaces them, so they don't flash empty while dragging.
  /// Without a history db there's no reply and it builds up from live
  /// updates alone.
  void _requestSelectedHistory() {
    final icao = _selectedIcao;
    if (icao != _selectedHistoryIcao) {
      final a = icao == null ? null : _shownAircraft[icao];
      _selectedHistory = a == null ? [] : [_historyPointOf(a)];
      _selectedHistoryIcao = icao;
    }
    if (icao == null || !_backfilled) return;
    _channel?.sink.add(jsonEncode({'type': 'aircraft_history', 'icao': icao, 't': ?_snapshot?.timeS}));
  }

  /// A reply to _requestSelectedHistory: its points, then (when live) any
  /// live ones that arrived after it. Replies for an earlier selection are
  /// dropped.
  void _onAircraftHistory(Map<String, dynamic> j) {
    if (j['icao'] != _selectedIcao) return;
    final points = <HistoryPoint>[
      for (final p in j['points'] as List)
        (
          t: (p[0] as num).toDouble(),
          altFt: (p[1] as num?)?.round(),
          gsKt: (p[2] as num?)?.toDouble(),
          vrFpm: (p[3] as num?)?.round(),
          signalDb: (p[4] as num?)?.toDouble(),
          selAltFt: (p[5] as num?)?.round(),
        ),
    ];
    final last = points.isEmpty ? double.negativeInfinity : points.last.t;
    _selectedHistory = [
      ...points,
      // While scrubbing, anything after the reply is from the previous position.
      if (_scrubTimeS == null) ..._selectedHistory.where((p) => p.t > last),
    ];
    _aircraftChanged.notify();
  }

  void _appendHistory(HistoryPoint p) {
    if (_selectedHistory.isNotEmpty && p.t <= _selectedHistory.last.t) return;
    _selectedHistory.add(p);
    // Same window as the backend's kHistoryAircraftWindowS.
    final cutoff = p.t - _historyWindowS;
    final firstKept = _selectedHistory.indexWhere((q) => q.t > cutoff);
    if (firstKept > 0) _selectedHistory.removeRange(0, firstKept);
  }

  static const double _historyWindowS = 1800;

  /// Closes the connection and stops the flush timer, after flushing what's
  /// still buffered. Also where a dropped connection ends up, so the timer
  /// never outlives it.
  void _disconnect({String? error}) {
    if (_uiUpdateTimer != null) _flushUpdates();
    _uiUpdateTimer?.cancel();
    _uiUpdateTimer = null;
    _activityTimer?.cancel();
    _activityTimer = null;
    _sub?.cancel();
    _channel?.sink.close();
    _sub = null;
    _channel = null;
    _seekInFlight = false;
    _queuedSeekS = null;
    _heatmapInFlight = false;
    _heatmapQueued = false;
    setState(() {
      _connected = false;
      if (error != null) _error = error;
    });
  }

  void _onFrameSort(int column, bool ascending) {
    _frameSortColumn = column;
    _frameSortAscending = ascending;
    _sortedFramesCache = null;
    _framesChanged.notify();
  }

  void _onAircraftSort(int column, bool ascending) {
    _aircraftSortColumn = column;
    _aircraftSortAscending = ascending;
    _sortedAircraftCache = null;
    _aircraftChanged.notify();
  }

  static List<T> _sortedBy<T>(Iterable<T> rows, _TableColumn<T> column, bool ascending) {
    final cmp = column.compare!;
    return rows.toList()..sort(ascending ? cmp : (a, b) => cmp(b, a));
  }

  List<FrameRecord> get _sortedFrames => _sortedFramesCache ??=
      _sortedBy(_pausedFrames ?? _frames, _frameColumns[_frameSortColumn], _frameSortAscending);

  void _toggleFramesPaused() {
    _pausedFrames = _pausedFrames == null ? _frames.toList() : null;
    _framesSincePause = 0;
    _sortedFramesCache = null;
    _framesChanged.notify();
  }

  // Staleness is relative to the newest update, so it only changes when the
  // aircraft do, which already invalidates this cache.
  List<AircraftRecord> get _sortedAircraft => _sortedAircraftCache ??= _sortedBy(
    _visibleAircraft,
    _aircraftColumns[_aircraftSortColumn],
    _aircraftSortAscending,
  );

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
        children: [
          _buildTopBar(),
          const Divider(height: 1),
          Expanded(
            child: ListenableBuilder(listenable: _aircraftChanged, builder: (context, _) => _buildAircraftPane()),
          ),
        ],
      ),
    );
  }

  /// One compact bar in place of an app bar: title, aircraft count, toggles
  /// for the frames and spectrum dock, a one-line stats summary (click for
  /// the details), and the connection controls.
  Widget _buildTopBar() {
    final theme = Theme.of(context);
    return Material(
      color: theme.colorScheme.surfaceContainer,
      child: SizedBox(
        height: 48,
        child: Row(
          children: [
            const SizedBox(width: 16),
            Text('ADS-B', style: theme.textTheme.titleMedium),
            const SizedBox(width: 8),
            const SizedBox(width: 8),
            ListenableBuilder(
              listenable: _aircraftChanged,
              builder: (context, _) => Text('${_shownAircraft.length} aircraft'),
            ),
            const SizedBox(width: 16),
            ListenableBuilder(
              listenable: _framesChanged,
              builder: (context, _) => _dockToggle(
                icon: Icons.list_alt,
                label: 'Frames (${_frames.length})',
                selected: _showFrames,
                onSelected: (v) => setState(() => _showFrames = v),
              ),
            ),
            const SizedBox(width: 6),
            _dockToggle(
              icon: Icons.waterfall_chart,
              label: 'Spectrum',
              selected: _showSpectrum,
              onSelected: (v) => setState(() => _showSpectrum = v),
            ),
            Expanded(
              child: ListenableBuilder(
                listenable: _framesChanged,
                builder: (context, _) => _statsTotalFrames > 0 ? _buildStatsSummary() : const SizedBox.shrink(),
              ),
            ),
            const SizedBox(width: 8),
            _buildConnectionControls(),
            const SizedBox(width: 12),
          ],
        ),
      ),
    );
  }

  /// Summary of incoming-frame health: throughput and how much of what's
  /// being demodulated looks like a real transponder reply vs. noise. In the
  /// top bar since it's about the link, not either table.
  /// Clicking it opens _showStatsDetails.
  Widget _buildStatsSummary() {
    final total = _statsTotalFrames;
    String pct(int n, int d) => d > 0 ? '${(n / d * 100).toStringAsFixed(1)}%' : '–';
    final unknownDfPct = _statsUnknownDf / total * 100;
    return Align(
      alignment: Alignment.centerRight,
      child: TextButton(
        onPressed: _showStatsDetails,
        child: Text.rich(
          TextSpan(
            children: [
              TextSpan(text: '${_statsFramesPerSec.toStringAsFixed(1)} frames/s  ·  '),
              TextSpan(text: 'crc ok ${pct(_statsSelfCheckOk, _statsSelfCheck)}  ·  '),
              TextSpan(text: 'fixed $_statsCrcFixed  ·  '),
              TextSpan(text: 'addr ${pct(_statsAddrParityOk, _statsAddrParity)}  ·  '),
              TextSpan(
                text: 'unknown df ${unknownDfPct.toStringAsFixed(1)}%',
                style: _statsUnknownDf > 0 ? TextStyle(color: Colors.orange[800]) : null,
              ),
            ],
          ),
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
      ),
    );
  }

  void _showStatsDetails() {
    showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Frame statistics'),
        content: ListenableBuilder(
          listenable: _framesChanged,
          builder: (context, _) {
            final total = _statsTotalFrames;
            String pct(int n, int d) => d > 0 ? '${(n / d * 100).toStringAsFixed(1)}%' : '–';
            final unknownDfPct = total > 0 ? _statsUnknownDf / total * 100 : 0.0;
            return SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Wrap(
                    spacing: 20,
                    runSpacing: 8,
                    children: [
                      _statChip('$total', 'frames'),
                      _statChip(_statsFramesPerSec.toStringAsFixed(1), 'frames/s'),
                      _statChip(pct(_statsSelfCheckOk, _statsSelfCheck), 'crc ok (df 11/17/18)'),
                      _statChip(
                        '$_statsCrcFixed (${pct(_statsCrcFixed, _statsSelfCheckOk)})',
                        'crc ok after 1-bit fix (df 17/18)',
                      ),
                      _statChip(pct(_statsAddrParityOk, _statsAddrParity), 'address match (df 0/4/5/16/20/21/24)'),
                      _statChip('$_statsCrcUnchecked', 'crc unchecked (df 19/22)'),
                      _statChip(
                        '${unknownDfPct.toStringAsFixed(1)}%',
                        'unrecognized df',
                        warn: _statsUnknownDf > 0,
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  _buildDfBreakdown(),
                ],
              ),
            );
          },
        ),
        actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Close'))],
      ),
    );
  }

  /// Status dot, URL field and connect button; a connection error shows as
  /// an icon whose tooltip has the message.
  Widget _buildConnectionControls() {
    final theme = Theme.of(context);
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (_error != null) ...[
          Tooltip(message: _error!, child: Icon(Icons.error_outline, color: theme.colorScheme.error, size: 20)),
          const SizedBox(width: 8),
        ],
        Tooltip(
          message: _connected ? 'Connected' : 'Disconnected',
          child: Icon(Icons.circle, size: 10, color: _connected ? Colors.green : Colors.grey),
        ),
        const SizedBox(width: 8),
        SizedBox(
          width: 240,
          child: TextField(
            controller: _urlController,
            enabled: !_connected,
            style: theme.textTheme.bodyMedium,
            decoration: const InputDecoration(
              hintText: 'WebSocket URL',
              isDense: true,
              contentPadding: EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              border: OutlineInputBorder(),
            ),
          ),
        ),
        const SizedBox(width: 8),
        FilledButton.tonal(
          onPressed: _connected ? _disconnect : _connect,
          style: FilledButton.styleFrom(visualDensity: VisualDensity.compact),
          child: Text(_connected ? 'Disconnect' : 'Connect'),
        ),
      ],
    );
  }

  Widget _statChip(String value, String label, {bool warn = false}) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          value,
          style: TextStyle(
            fontWeight: FontWeight.bold,
            color: warn ? Colors.orange[800] : null,
          ),
        ),
        Text(label, style: TextStyle(fontSize: 11, color: Colors.grey[600])),
      ],
    );
  }

  /// Per-downlink-format counts, sorted by frequency. DFs outside
  /// _knownDfValues are flagged -- they're the frames most likely to be
  /// false-positive preamble detections on noise rather than real replies.
  Widget _buildDfBreakdown() {
    final total = _statsTotalFrames;
    final entries = _statsByDf.entries.toList()..sort((a, b) => b.value.compareTo(a.value));
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        for (final e in entries)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 1),
            child: Text(
              'df ${e.key.toString().padLeft(2)}: ${e.value.toString().padLeft(6)} '
              '(${(e.value / total * 100).toStringAsFixed(1)}%)'
              '${_knownDfValues.contains(e.key) ? '' : '  — unrecognized, likely noise'}',
              style: TextStyle(
                fontFamily: 'monospace',
                color: _knownDfValues.contains(e.key) ? null : Colors.orange[800],
              ),
            ),
          ),
      ],
    );
  }

  Widget _dockToggle({
    required IconData icon,
    required String label,
    required bool selected,
    required ValueChanged<bool> onSelected,
  }) {
    return FilterChip(
      avatar: Icon(icon, size: 16),
      label: Text(label),
      selected: selected,
      showCheckmark: false,
      visualDensity: VisualDensity.compact,
      tooltip: selected ? 'Hide $label' : 'Show $label',
      onSelected: onSelected,
    );
  }

  /// The dock beside the map and table: whichever of the frame log and
  /// spectrum are shown, stacked, behind a drag handle that sets its width.
  Widget _buildDock(double totalWidth) {
    final maxWidth = math.max(_minDockWidth, totalWidth - _minMapWidth);
    final width = _dockWidth.clamp(_minDockWidth, maxWidth);
    return SizedBox(
      key: const ValueKey('dock'),
      width: width,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          MouseRegion(
            cursor: SystemMouseCursors.resizeColumn,
            child: GestureDetector(
              behavior: HitTestBehavior.translucent,
              onHorizontalDragUpdate: (details) {
                // From the state, not the captured width, as in _buildAircraftSplit.
                final current = _dockWidth.clamp(_minDockWidth, maxWidth);
                _dockWidth = (current - details.delta.dx).clamp(_minDockWidth, maxWidth);
                _aircraftChanged.notify();
              },
              child: const SizedBox(width: 8, child: Center(child: VerticalDivider(width: 1))),
            ),
          ),
          Expanded(
            child: Column(
              children: [
                if (_showFrames)
                  Expanded(
                    flex: 3,
                    child: _buildDockSection(
                      title: ListenableBuilder(
                        listenable: _framesChanged,
                        builder: (context, _) => Text('Frames (${_frames.length})'),
                      ),
                      onClose: () => setState(() => _showFrames = false),
                      child: ListenableBuilder(listenable: _framesChanged, builder: (context, _) => _buildFramesPane()),
                    ),
                  ),
                if (_showFrames && _showSpectrum) const Divider(height: 1),
                if (_showSpectrum)
                  Expanded(
                    flex: 2,
                    child: _buildDockSection(
                      title: const Text('Spectrum'),
                      onClose: () => setState(() => _showSpectrum = false),
                      child: _buildSpectrumPane(),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDockSection({required Widget title, required VoidCallback onClose, required Widget child}) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Material(
          color: theme.colorScheme.surfaceContainerLow,
          child: Padding(
            padding: const EdgeInsets.only(left: 12),
            child: Row(
              children: [
                DefaultTextStyle(style: theme.textTheme.titleSmall!, child: title),
                const Spacer(),
                IconButton(
                  icon: const Icon(Icons.close, size: 18),
                  tooltip: 'Close',
                  visualDensity: VisualDensity.compact,
                  onPressed: onClose,
                ),
              ],
            ),
          ),
        ),
        Expanded(child: child),
      ],
    );
  }

  Widget _buildFramesPane() {
    final paused = _pausedFrames != null;
    return Column(
      children: [
        Expanded(child: _buildFrameTable()),
        const Divider(height: 1),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          child: Row(
            children: [
              FilledButton.tonalIcon(
                onPressed: _toggleFramesPaused,
                icon: Icon(paused ? Icons.play_arrow : Icons.pause),
                label: Text(paused ? 'Resume' : 'Pause'),
              ),
              const SizedBox(width: 12),
              if (paused)
                Expanded(
                  child: Text(
                    'Paused, $_framesSincePause new frames since'
                    '${_framesSincePause > _maxFrames ? ' (only the newest $_maxFrames kept)' : ''}',
                    style: TextStyle(color: Colors.grey[600]),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildFrameTable() {
    return _LazyTable<FrameRecord>(
      columns: _frameColumns,
      rows: _sortedFrames,
      sortColumn: _frameSortColumn,
      sortAscending: _frameSortAscending,
      onSort: _onFrameSort,
    );
  }

  Widget _buildAircraftTable() {
    return _LazyTable<AircraftRecord>(
      columns: _aircraftColumns,
      rows: _sortedAircraft,
      sortColumn: _aircraftSortColumn,
      sortAscending: _aircraftSortAscending,
      onSort: _onAircraftSort,
      onTapRow: (a) => _toggleSelected(a.icao),
      isSelected: (a) => a.icao == _selectedIcao,
      rowStyle: (a) => _isStale(a) ? TextStyle(color: Colors.grey[500]) : null,
    );
  }

  /// The map over the aircraft table, the dock beside them when anything's
  /// in it (sliding in and out), and the scrubber bar under both.
  Widget _buildAircraftPane() {
    final docked = _showFrames || _showSpectrum;
    return Column(
      children: [
        Expanded(
          child: LayoutBuilder(
            builder: (context, constraints) => Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(child: _buildAircraftSplit()),
                AnimatedSwitcher(
                  duration: const Duration(milliseconds: 200),
                  transitionBuilder: (child, animation) => SizeTransition(
                    sizeFactor: animation,
                    axis: Axis.horizontal,
                    axisAlignment: 1,
                    child: child,
                  ),
                  child: docked ? _buildDock(constraints.maxWidth) : const SizedBox.shrink(key: ValueKey('no dock')),
                ),
              ],
            ),
          ),
        ),
        const Divider(height: 1),
        _buildAircraftControls(),
      ],
    );
  }

  /// Bar under the table and map, for what both show: whether stale aircraft
  /// are shown, and, with a history db, the time scrubber. Dragging the
  /// scrubber shows the aircraft and trails as of that time; Live, or letting
  /// go at the right end, goes back to live.
  Widget _buildAircraftControls() {
    final staleCount = _shownAircraft.values.where(_isStale).length;
    final start = _historyStartS;
    final end = math.max(_historyEndS ?? start ?? 0, start ?? 0);
    final live = _scrubTimeS == null;
    final value = start == null ? 0.0 : (_scrubTimeS ?? end).clamp(start, end);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: Row(
        children: [
          SizedBox(
            width: 200,
            child: TextField(
              controller: _searchController,
              decoration: InputDecoration(
                isDense: true,
                border: const OutlineInputBorder(),
                prefixIcon: const Icon(Icons.search, size: 18),
                hintText: 'icao, callsign, squawk, type',
                suffixIcon: _search.isEmpty
                    ? null
                    : IconButton(
                        icon: const Icon(Icons.clear, size: 18),
                        tooltip: 'Clear',
                        onPressed: () {
                          _searchController.clear();
                          _search = '';
                          _onAircraftFilterChanged();
                        },
                      ),
              ),
              onChanged: (text) {
                _search = text.trim().toLowerCase();
                _onAircraftFilterChanged();
              },
            ),
          ),
          const SizedBox(width: 8),
          Tooltip(
            message: 'Aircraft not seen for ${_staleAfterS.toStringAsFixed(0)}s',
            child: FilterChip(
              label: Text('Stale ($staleCount)'),
              selected: _showStale,
              onSelected: (v) {
                _showStale = v;
                _onAircraftFilterChanged();
              },
            ),
          ),
          const SizedBox(width: 8),
          Tooltip(
            message: 'Only aircraft with a position',
            child: FilterChip(
              label: const Text('Has position'),
              selected: _positionOnly,
              onSelected: (v) {
                _positionOnly = v;
                _onAircraftFilterChanged();
              },
            ),
          ),
          if (start != null) ...[
            const SizedBox(width: 16),
            SizedBox(
              width: 180,
              child: Tooltip(
                message: 'Jump to a date and time',
                child: TextButton(
                  onPressed: _connected && end > start ? _pickScrubTime : null,
                  child: Text(live ? 'live' : _formatTime(value), style: const TextStyle(fontFamily: 'monospace')),
                ),
              ),
            ),
            _buildScrubWindowMenu(),
            IconButton(
              icon: const Icon(Icons.chevron_left),
              tooltip: 'Back ${_windowLabel(_scrubWindowS)}',
              onPressed: _connected && value > start ? () => _jumpTo(value - _scrubWindowS) : null,
            ),
            Expanded(child: _buildScrubber(start, end, value)),
            IconButton(
              icon: const Icon(Icons.chevron_right),
              tooltip: 'Forward ${_windowLabel(_scrubWindowS)}',
              onPressed: _connected && !live ? () => _jumpTo(value + _scrubWindowS) : null,
            ),
            FilledButton.tonal(onPressed: live ? null : _goLive, child: const Text('Live')),
          ],
        ],
      ),
    );
  }

  static String _windowLabel(double s) => s >= 3600 ? '${(s / 3600).round()} h' : '${(s / 60).round()} min';

  Widget _buildScrubWindowMenu() {
    return PopupMenuButton<double>(
      tooltip: 'Slider span',
      initialValue: _scrubWindowS,
      onSelected: (s) {
        final t = _scrubTimeS;
        _scrubWindowS = s;
        // Recentre on the scrub position, so it stays within the new window.
        if (t != null) _scrubWindowStartS = t - s / 2;
        _aircraftChanged.notify();
      },
      itemBuilder: (context) => [
        for (final s in _scrubWindowChoicesS) PopupMenuItem(value: s, child: Text(_windowLabel(s))),
      ],
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [Text(_windowLabel(_scrubWindowS)), const Icon(Icons.arrow_drop_down, size: 18)],
        ),
      ),
    );
  }

  /// Two-level scrubber: an overview strip of the whole history, where a
  /// click or drag jumps (moving the window there), over a fine slider that
  /// covers just the window.
  Widget _buildScrubber(double start, double end, double value) {
    final (lo, hi) = _scrubWindow(start, end);
    final enabled = _connected && end > start;
    final color = Theme.of(context).colorScheme.primary;
    final trackColor = Theme.of(context).colorScheme.outlineVariant;
    return LayoutBuilder(
      builder: (context, constraints) {
        // Inset to line up with the slider's track.
        const inset = 24.0;
        final width = math.max(1.0, constraints.maxWidth - 2 * inset);
        void jump(Offset local) {
          if (enabled) _jumpTo(start + ((local.dx - inset) / width).clamp(0.0, 1.0) * (end - start));
        }

        return Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Tooltip(
              message: '${_formatTime(start)} – ${_formatTime(end)}\n'
                  'Shading: aircraft seen per minute (peak $_activityPeakCount)\nClick or drag to jump',
              waitDuration: const Duration(milliseconds: 600),
              child: GestureDetector(
                onTapDown: (d) => jump(d.localPosition),
                onHorizontalDragUpdate: (d) => jump(d.localPosition),
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: inset),
                  child: CustomPaint(
                    size: const Size(double.infinity, 20),
                    painter: _TimelinePainter(
                      startS: start,
                      endS: end,
                      windowStartS: lo,
                      windowEndS: hi,
                      positionS: value,
                      activity: _activity,
                      activityT0: _activityT0,
                      activityBucketS: _activityBucketS,
                      color: color,
                      trackColor: trackColor,
                    ),
                  ),
                ),
              ),
            ),
            SizedBox(
              height: 32,
              child: Slider(
                min: lo,
                max: hi,
                value: value.clamp(lo, hi),
                onChanged: enabled && hi > lo
                    ? (t) {
                        // Pin the window, so it stops following the live end while dragging.
                        _scrubWindowStartS ??= lo;
                        _onScrub(t);
                      }
                    : null,
                onChangeEnd: (t) {
                  if (t >= end) _goLive();
                },
              ),
            ),
          ],
        );
      },
    );
  }

  Widget _buildAircraftSplit() {
    return LayoutBuilder(
      builder: (context, constraints) {
        final height = constraints.maxHeight;
        final maxMapHeight = math.max(_minPaneHeight, height - _minPaneHeight - 8);
        final mapHeight = (_mapFraction * height).clamp(_minPaneHeight, maxMapHeight);
        return Column(
          children: [
            SizedBox(height: mapHeight, child: _buildAircraftMap()),
            MouseRegion(
              cursor: SystemMouseCursors.resizeRow,
              child: GestureDetector(
                behavior: HitTestBehavior.translucent,
                onVerticalDragUpdate: (details) {
                  // Several drag events can arrive per frame, before the rebuild, so start from the state
                  // (already moved by the earlier ones), not the mapHeight this closure captured.
                  final current = (_mapFraction * height).clamp(_minPaneHeight, maxMapHeight);
                  _mapFraction = (current + details.delta.dy).clamp(_minPaneHeight, maxMapHeight) / height;
                  _aircraftChanged.notify();
                },
                child: const SizedBox(
                  height: 8,
                  child: Center(child: Divider(height: 1)),
                ),
              ),
            ),
            Expanded(child: _buildAircraftTable()),
          ],
        );
      },
    );
  }

  Widget _buildAircraftMap() {
    final positioned = _visibleAircraft.where((a) => a.lat != null && a.lon != null).toList();
    return Stack(
      children: [
        FlutterMap(
          mapController: _mapController,
          options: MapOptions(
            initialCenter: const LatLng(20, 0),
            initialZoom: 2,
            onMapReady: () {
              _mapReady = true;
              _scheduleHeatmap();
            },
            onPositionChanged: (camera, hasGesture) => _scheduleHeatmap(),
          ),
          children: [
            TileLayer(
              urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
              userAgentPackageName: 'com.example.adsb_ui',
            ),
            if (_heatmapOverlay != null) OverlayImageLayer(overlayImages: [_heatmapOverlay!]),
            PolylineLayer(
              polylines: [for (final a in positioned) ..._trailPolylines(a)],
            ),
            MarkerLayer(
              markers: [
                for (final a in positioned)
                  Marker(
                    point: LatLng(a.lat!, a.lon!),
                    width: 44,
                    height: 44,
                    child: _buildAircraftMarker(a),
                  ),
              ],
            ),
          ],
        ),
        Positioned(left: 12, bottom: 12, child: _buildAltitudeLegend()),
        Positioned(right: 12, top: 12, child: _buildHeatmapMenu()),
        if (_selectedIcao != null && _shownAircraft.containsKey(_selectedIcao))
          Positioned(
            left: 12,
            top: 12,
            bottom: 56,
            width: 300,
            child: _buildDetailPanel(_shownAircraft[_selectedIcao]!),
          ),
        Positioned(
          right: 12,
          bottom: 12,
          child: FloatingActionButton.small(
            tooltip: 'Center on aircraft',
            onPressed: positioned.isEmpty ? null : _recenterMap,
            child: const Icon(Icons.my_location),
          ),
        ),
      ],
    );
  }

  /// A track-oriented arrow for one aircraft, dimmed if stale and ringed if
  /// selected. Tapping it selects/deselects, mirroring the table row.
  /// `a`'s trail as polylines, one per run of points in the same 1,000 ft
  /// band, coloured by that band's altitude (flutter_map's gradients run
  /// straight across the screen, not along the line). Grey when stale.
  List<Polyline> _trailPolylines(AircraftRecord a) {
    final trail = _shownTrails[a.icao];
    if (trail == null || trail.length < 2) return const [];
    final selected = a.icao == _selectedIcao;
    final stale = _isStale(a);
    int? band(TrailPoint p) => p.altFt == null ? null : p.altFt! ~/ 1000;
    Color colorOf(int? band) => stale
        ? Colors.grey
        : band == null
            ? Theme.of(context).colorScheme.primary
            : _altitudeColor(band * 1000 + 500);
    final lines = <Polyline>[];
    var start = 0;
    for (var i = 1; i <= trail.length; i++) {
      if (i < trail.length && band(trail[i]) == band(trail[start])) continue;
      // Each run ends on the next run's first point, so the line is unbroken.
      final end = math.min(i, trail.length - 1);
      if (end > start) {
        lines.add(Polyline(
          points: [for (final p in trail.sublist(start, end + 1)) p.pos],
          strokeWidth: selected ? 3 : 2,
          color: colorOf(band(trail[start])).withValues(alpha: selected ? 0.9 : 0.6),
        ));
      }
      start = i;
    }
    return lines;
  }

  Widget _buildAltitudeLegend() {
    final theme = Theme.of(context);
    return Card(
      margin: EdgeInsets.zero,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(8, 6, 8, 4),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            CustomPaint(size: const Size(160, 8), painter: _AltitudeScalePainter()),
            const SizedBox(height: 2),
            SizedBox(
              width: 160,
              child: DefaultTextStyle.merge(
                style: theme.textTheme.labelSmall,
                child: const Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [Text('0'), Text('10k'), Text('20k'), Text('30k'), Text('40k ft')],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  // " MCP" or " FMS", for after a selected altitude.
  static String _sourceSuffix(AircraftRecord a) =>
      a.selectedAltitudeSource == null ? '' : ' ${a.selectedAltitudeSource}';

  static String _modesText(List<String> modes) => modes.isEmpty ? 'none engaged' : modes.join(' ');

  // IAS, TAS and Mach, those known.
  static List<String> _airspeeds(AircraftRecord a) => [
        if (a.iasKt != null) '${a.iasKt} kt IAS',
        if (a.tasKt != null) '${a.tasKt} kt TAS',
        if (a.mach != null) 'M${a.mach!.toStringAsFixed(3)}',
      ];

  /// Horizontal position accuracy of each NACp (95% bound), DO-260B table 2-14.
  static const _nacPBounds = [
    'unknown', '< 10 NM', '< 4 NM', '< 2 NM', '< 1 NM', '< 0.5 NM', '< 0.3 NM', '< 0.1 NM', //
    '< 93 m', '< 30 m', '< 10 m', '< 3 m',
  ];

  /// The detail panel's values beyond those charted: air data, autopilot
  /// settings and ADS-B quality, as label-over-value pairs. Empty if none
  /// are known.
  List<Widget> _buildDetailFacts(AircraftRecord a) {
    final theme = Theme.of(context);
    final facts = <(String, String)>[
      if (a.headingDeg != null) ('heading', '${a.headingDeg!.toStringAsFixed(0)}° mag'),
      if (a.trackDeg != null) ('track', '${a.trackDeg!.toStringAsFixed(0)}°'),
      if (a.iasKt != null) ('IAS', '${a.iasKt} kt'),
      if (a.tasKt != null) ('TAS', '${a.tasKt} kt'),
      if (a.mach != null) ('Mach', a.mach!.toStringAsFixed(3)),
      if (a.rollDeg != null) ('roll', '${a.rollDeg!.toStringAsFixed(1)}°'),
      if (a.selectedAltitudeFt != null) ('selected alt', '${a.selectedAltitudeFt} ft${_sourceSuffix(a)}'),
      if (a.selectedHeadingDeg != null) ('selected hdg', '${a.selectedHeadingDeg!.toStringAsFixed(0)}°'),
      if (a.baroSettingHpa != null) ('baro', '${a.baroSettingHpa!.toStringAsFixed(1)} hPa'),
      if (a.autopilotModes != null) ('autopilot', _modesText(a.autopilotModes!)),
      if (a.adsbVersion != null) ('ADS-B', 'v${a.adsbVersion}'),
      if (a.nacP != null && a.nacP! < _nacPBounds.length) ('NACp', '${a.nacP} (${_nacPBounds[a.nacP!]})'),
      if (a.sil != null) ('SIL', '${a.sil}'),
    ];
    if (facts.isEmpty) return [];
    return [
      Wrap(
        spacing: 16,
        runSpacing: 6,
        children: [
          for (final (label, value) in facts)
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(label, style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline)),
                Text(value, style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold)),
              ],
            ),
        ],
      ),
      const SizedBox(height: 12),
    ];
  }

  /// The selected aircraft's details: header with status badges, the
  /// values from _buildDetailFacts, then charts of altitude (with the
  /// selected altitude dashed), ground speed, vertical rate and signal over
  /// _selectedHistory.
  Widget _buildDetailPanel(AircraftRecord a) {
    final theme = Theme.of(context);
    final history = _selectedHistory;
    final times = [for (final p in history) p.t];
    final tMax = times.isEmpty ? a.lastSeenUnixS : times.last;
    final tMin = times.isEmpty ? tMax : times.first;
    final grid = theme.dividerColor;

    Widget chart(String label, String? current, List<double?> values, Color Function(double) colorOf,
        {bool zeroLine = false, String Function(double)? format, List<double?>? targets}) {
      final present = values.whereType<double>();
      final fmt = format ?? (double v) => v.toStringAsFixed(0);
      return Padding(
        padding: const EdgeInsets.only(bottom: 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(label, style: theme.textTheme.labelMedium),
                const Spacer(),
                if (present.isNotEmpty)
                  Text(
                    '${fmt(present.reduce(math.min))} – ${fmt(present.reduce(math.max))}   ',
                    style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline),
                  ),
                Text(current ?? '–', style: theme.textTheme.labelMedium?.copyWith(fontWeight: FontWeight.bold)),
              ],
            ),
            const SizedBox(height: 4),
            SizedBox(
              height: 44,
              width: double.infinity,
              child: CustomPaint(
                painter: _TimeSeriesPainter(
                  times: times,
                  values: values,
                  tMin: tMin,
                  tMax: tMax,
                  colorOf: colorOf,
                  gridColor: grid,
                  zeroLine: zeroLine,
                  targets: targets,
                  targetColor: theme.colorScheme.outline,
                ),
              ),
            ),
          ],
        ),
      );
    }

    final spanMin = (tMax - tMin) / 60;
    return Card(
      margin: EdgeInsets.zero,
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 8, 4, 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                if (a.category != null) ...[_typeVisual(a, a.category!), const SizedBox(width: 8)],
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(a.callsign ?? a.icao, style: theme.textTheme.titleSmall),
                      Text(
                        [a.icao, if (a.category != null) _categoryNames[a.category] ?? a.category!, if (a.squawk != null) 'squawk ${a.squawk}']
                            .join(' · '),
                        style: theme.textTheme.labelSmall,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                      if (_statusBadges(a).isNotEmpty)
                        Padding(
                          padding: const EdgeInsets.only(top: 2),
                          child: Wrap(spacing: 4, children: _statusBadges(a)),
                        ),
                    ],
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close, size: 18),
                  visualDensity: VisualDensity.compact,
                  tooltip: 'Close',
                  onPressed: () => _toggleSelected(a.icao),
                ),
              ],
            ),
            const Divider(),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.only(right: 8),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    ..._buildDetailFacts(a),
                    chart(
                      history.any((p) => p.selAltFt != null) ? 'Altitude (ft), - - selected' : 'Altitude (ft)',
                      a.altitudeFt?.toString(),
                      [for (final p in history) p.altFt?.toDouble()],
                      (v) => _altitudeColor(v.round()),
                      targets: history.any((p) => p.selAltFt != null)
                          ? [for (final p in history) p.selAltFt?.toDouble()]
                          : null,
                    ),
                    chart(
                      'Ground speed (kt)',
                      a.groundSpeedKt?.toStringAsFixed(0),
                      [for (final p in history) p.gsKt],
                      (_) => Colors.blueGrey,
                    ),
                    chart(
                      'Vertical rate (fpm)',
                      a.verticalRateFpm?.toString(),
                      [for (final p in history) p.vrFpm?.toDouble()],
                      (v) => v > 100 ? Colors.green.shade600 : (v < -100 ? Colors.orange.shade800 : Colors.grey),
                      zeroLine: true,
                    ),
                    chart(
                      'Signal (dB)',
                      a.signalDb?.toStringAsFixed(1),
                      [for (final p in history) p.signalDb],
                      (_) => Colors.teal,
                      format: (v) => v.toStringAsFixed(1),
                    ),
                    Text(
                      times.length < 2
                          ? 'Collecting history…'
                          : 'Last ${spanMin < 1 ? '${(spanMin * 60).round()} s' : '${spanMin.toStringAsFixed(0)} min'}'
                              ', ${history.length} updates',
                      style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.outline),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// A map marker's tooltip: the aircraft's values with the same visuals as
  /// the table, in label / visual / value rows.
  Widget _buildAircraftTooltip(AircraftRecord a) {
    const labelStyle = TextStyle(color: Colors.white70, fontSize: 12);
    const valueStyle = TextStyle(color: Colors.white, fontSize: 12);
    TableRow row(String label, Widget? visual, String value, {TextStyle style = valueStyle}) => TableRow(
          children: [
            Padding(padding: const EdgeInsets.only(right: 12, bottom: 2), child: Text(label, style: labelStyle)),
            Padding(
              padding: const EdgeInsets.only(right: 6, bottom: 2),
              child: Align(alignment: Alignment.centerRight, child: visual ?? const SizedBox.shrink()),
            ),
            Padding(
              padding: const EdgeInsets.only(bottom: 2),
              child: Text(value, style: style, textAlign: TextAlign.right),
            ),
          ],
        );
    final emergency = _emergencySquawks[a.squawk];
    final age = _ageS(a);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          a.callsign == null ? a.icao : '${a.callsign}  (${a.icao})',
          style: valueStyle.copyWith(fontWeight: FontWeight.bold, fontSize: 13),
        ),
        if (a.category != null)
          Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                _typeVisual(a, a.category!),
                const SizedBox(width: 6),
                Text(_categoryNames[a.category] ?? a.category!, style: valueStyle),
              ],
            ),
          ),
        const SizedBox(height: 6),
        DefaultTextStyle.merge(
          style: valueStyle,
          child: Table(
            defaultColumnWidth: const IntrinsicColumnWidth(),
            defaultVerticalAlignment: TableCellVerticalAlignment.middle,
            children: [
              if (a.squawk != null)
                row(
                  'squawk',
                  null,
                  emergency == null ? a.squawk! : '${a.squawk} $emergency',
                  style: emergency == null
                      ? valueStyle
                      : valueStyle.copyWith(color: Colors.red.shade300, fontWeight: FontWeight.bold),
                ),
              if (a.altitudeFt != null) row('alt', _altitudeVisual(a, a.altitudeFt!), '${a.altitudeFt} ft'),
              if (a.groundSpeedKt != null)
                row('gs', _speedVisual(a, a.groundSpeedKt!), '${a.groundSpeedKt!.toStringAsFixed(0)} kt'),
              if (a.trackDeg != null)
                row('trk', _trackVisual(a, a.trackDeg!), '${a.trackDeg!.toStringAsFixed(0)}°'),
              if (a.verticalRateFpm != null)
                row('vr', _verticalRateVisual(a, a.verticalRateFpm!), '${a.verticalRateFpm} fpm'),
              if (a.headingDeg != null) row('hdg', null, '${a.headingDeg!.toStringAsFixed(0)}° mag'),
              if (_airspeeds(a).isNotEmpty) row('air', null, _airspeeds(a).join(' · ')),
              if (a.selectedAltitudeFt != null)
                row('sel alt', null, '${a.selectedAltitudeFt} ft${_sourceSuffix(a)}'),
              if (a.autopilotModes != null) row('ap', null, _modesText(a.autopilotModes!)),
              if (_statusBadges(a).isNotEmpty)
                row('status', Wrap(spacing: 4, children: _statusBadges(a, tooltips: false)), ''),
              if (a.signalDb != null) row('sig', _signalVisual(a, a.signalDb!), '${a.signalDb!.toStringAsFixed(1)} dB'),
              row('msgs', null, '${a.messages}'),
              if (age != null) row('seen', _ageVisual(age), '${age.toStringAsFixed(0)}s ago'),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildAircraftMarker(AircraftRecord a) {
    final stale = _isStale(a);
    final selected = a.icao == _selectedIcao;
    final color = stale
        ? Colors.grey
        : _emergencySquawks.containsKey(a.squawk)
            ? Colors.red
            : a.altitudeFt != null
                ? _altitudeColor(a.altitudeFt!)
                : Theme.of(context).colorScheme.primary;
    final (shape, scale) = _categoryMarkers[a.category] ?? (_MarkerShape.unknown, 0.9);
    final icon = CustomPaint(size: Size.square(26 * scale), painter: _MarkerPainter(shape, color));
    return GestureDetector(
      onTap: () => _toggleSelected(a.icao),
      child: Tooltip(
        richMessage: WidgetSpan(child: _buildAircraftTooltip(a)),
        child: Container(
          alignment: Alignment.center,
          decoration: selected
              ? BoxDecoration(
                  shape: BoxShape.circle,
                  color: color.withValues(alpha: 0.15),
                  border: Border.all(color: color, width: 2),
                )
              : null,
          child: Opacity(
            opacity: stale ? 0.5 : 1.0,
            child: _MarkerPainter.isUpright(shape)
                ? icon
                : Transform.rotate(angle: (a.trackDeg ?? 0) * math.pi / 180, child: icon),
          ),
        ),
      ),
    );
  }

  void _recenterMap() {
    final positioned = _visibleAircraft.where((a) => a.lat != null && a.lon != null).toList();
    if (positioned.isEmpty) return;
    final lat = positioned.map((a) => a.lat!).reduce((a, b) => a + b) / positioned.length;
    final lon = positioned.map((a) => a.lon!).reduce((a, b) => a + b) / positioned.length;
    _mapController.move(LatLng(lat, lon), _mapController.camera.zoom);
  }

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

/// One column of a _LazyTable.
class _TableColumn<T> {
  final String label;
  final double width;
  final bool numeric;
  /// Sort order, or null if the column isn't sortable.
  final Comparator<T>? compare;
  final Widget Function(T row) cell;

  const _TableColumn(this.label, this.width, {this.numeric = false, this.compare, required this.cell});
}

/// A sortable table that builds only the rows on screen. DataTable builds
/// every row and measures every cell to size its columns on each rebuild,
/// which froze the UI once the frame log filled up; here columns are
/// fixed-width and rows fixed-height, so the ListView lays out only what's
/// visible.
class _LazyTable<T> extends StatelessWidget {
  const _LazyTable({
    super.key,
    required this.columns,
    required this.rows,
    required this.sortColumn,
    required this.sortAscending,
    required this.onSort,
    this.onTapRow,
    this.isSelected,
    this.rowStyle,
  });

  final List<_TableColumn<T>> columns;
  final List<T> rows;
  final int sortColumn;
  final bool sortAscending;
  final void Function(int column, bool ascending) onSort;
  final void Function(T row)? onTapRow;
  final bool Function(T row)? isSelected;
  final TextStyle? Function(T row)? rowStyle;

  static const double _headerHeight = 40;
  static const double _rowHeight = 32;

  Widget _cell(_TableColumn<T> c, double scale, Widget child) {
    return SizedBox(
      width: c.width * scale,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Align(alignment: c.numeric ? Alignment.centerRight : Alignment.centerLeft, child: child),
      ),
    );
  }

  Widget _header(int i, double scale) {
    final c = columns[i];
    final sorted = i == sortColumn;
    final label = _cell(
      c,
      scale,
      Row(
        mainAxisAlignment: c.numeric ? MainAxisAlignment.end : MainAxisAlignment.start,
        children: [
          Flexible(
            child: Text(
              c.label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontWeight: FontWeight.bold),
            ),
          ),
          if (sorted) Icon(sortAscending ? Icons.arrow_upward : Icons.arrow_downward, size: 14),
        ],
      ),
    );
    // Like DataTable: a new column sorts ascending, the current one toggles.
    return c.compare == null ? label : InkWell(onTap: () => onSort(i, sorted ? !sortAscending : true), child: label);
  }

  Widget _row(BuildContext context, T row, double scale) {
    final theme = Theme.of(context);
    final selected = isSelected?.call(row) ?? false;
    return DecoratedBox(
      position: DecorationPosition.foreground,
      decoration: BoxDecoration(border: Border(bottom: BorderSide(color: theme.dividerColor, width: 0.5))),
      child: Material(
        color: selected ? theme.colorScheme.primaryContainer : Colors.transparent,
        child: InkWell(
          onTap: onTapRow == null ? null : () => onTapRow!(row),
          child: DefaultTextStyle.merge(
            style: rowStyle?.call(row),
            maxLines: 1,
            softWrap: false,
            overflow: TextOverflow.ellipsis,
            child: Row(children: [for (final c in columns) _cell(c, scale, c.cell(row))]),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final width = columns.fold<double>(0, (w, c) => w + c.width);
    return LayoutBuilder(
      builder: (context, constraints) {
        // Columns stretch in proportion to fill spare width; narrower than
        // their total, the table scrolls horizontally instead.
        final scale = math.max(1.0, constraints.maxWidth / width);
        return SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: SizedBox(
            width: width * scale,
            child: Column(
              children: [
                SizedBox(
                  height: _headerHeight,
                  child: Row(children: [for (var i = 0; i < columns.length; i++) _header(i, scale)]),
                ),
                const Divider(height: 1),
                Expanded(
                  child: ListView.builder(
                    itemCount: rows.length,
                    itemExtent: _rowHeight,
                    itemBuilder: (context, i) => _row(context, rows[i], scale),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}
