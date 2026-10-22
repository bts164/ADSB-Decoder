import 'dart:ui' show Color;

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum SpeedUnit {
  kt('kt', 1),
  mph('mph', 1.150779),
  kmh('km/h', 1.852);

  const SpeedUnit(this.label, this.perKt);
  final String label;
  final double perKt;
}

enum AltitudeUnit {
  ft('ft', 1),
  m('m', 0.3048);

  const AltitudeUnit(this.label, this.perFt);
  final String label;
  final double perFt;
}

enum VerticalRateUnit {
  fpm('fpm', 1),
  mps('m/s', 0.00508);

  const VerticalRateUnit(this.label, this.perFpm);
  final String label;
  final double perFpm;
}

enum DistanceUnit {
  nm('NM', 1),
  mi('mi', 1.150779),
  km('km', 1.852);

  const DistanceUnit(this.label, this.perNm);
  final String label;
  final double perNm;
}

enum PressureUnit {
  hpa('hPa', 1),
  inHg('inHg', 0.0295300);

  const PressureUnit(this.label, this.perHpa);
  final String label;
  final double perHpa;
}

enum ClockFormat {
  h24('24 h'),
  h12('12 h');

  const ClockFormat(this.label);
  final String label;
}

enum TimeZoneMode {
  local('Local'),
  utc('UTC');

  const TimeZoneMode(this.label);
  final String label;
}

/// The spectrum waterfall's colors, from the bottom of its range to the top: stops spaced evenly and
/// interpolated between.
enum SpectrumColormap {
  classic('Classic', [
    Color(0xFF000030), Color(0xFF0000C0), Color(0xFF00C0C0), Color(0xFF00C000), Color(0xFFE0E000),
    Color(0xFFE00000),
  ]),
  viridis('Viridis', [
    Color(0xFF440154), Color(0xFF482878), Color(0xFF3E4A89), Color(0xFF31688E), Color(0xFF26828E),
    Color(0xFF1F9E89), Color(0xFF35B779), Color(0xFF6DCD59), Color(0xFFB4DE2C), Color(0xFFFDE725),
  ]),
  inferno('Inferno', [
    Color(0xFF000004), Color(0xFF1B0C41), Color(0xFF4A0C6B), Color(0xFF781C6D), Color(0xFFA52C60),
    Color(0xFFCF4446), Color(0xFFED6925), Color(0xFFFB9B06), Color(0xFFF7D13D), Color(0xFFFCFFA4),
  ]),
  turbo('Turbo', [
    Color(0xFF30123B), Color(0xFF4662D7), Color(0xFF36AAF9), Color(0xFF1AE4B6), Color(0xFF72FE5E),
    Color(0xFFC8EF34), Color(0xFFFABA39), Color(0xFFF66B19), Color(0xFFCA2A04), Color(0xFF7A0403),
  ]),
  grayscale('Grayscale', [Color(0xFF000000), Color(0xFFFFFFFF)]);

  const SpectrumColormap(this.label, this.stops);
  final String label;
  final List<Color> stops;
}

/// A set of units to switch to in one go. Leaves the clock and time zone alone.
enum UnitPreset {
  aviation('Aviation', SpeedUnit.kt, AltitudeUnit.ft, VerticalRateUnit.fpm, DistanceUnit.nm, PressureUnit.hpa),
  imperial('Imperial', SpeedUnit.mph, AltitudeUnit.ft, VerticalRateUnit.fpm, DistanceUnit.mi, PressureUnit.inHg),
  metric('Metric', SpeedUnit.kmh, AltitudeUnit.m, VerticalRateUnit.mps, DistanceUnit.km, PressureUnit.hpa);

  const UnitPreset(this.label, this.speed, this.altitude, this.verticalRate, this.distance, this.pressure);
  final String label;
  final SpeedUnit speed;
  final AltitudeUnit altitude;
  final VerticalRateUnit verticalRate;
  final DistanceUnit distance;
  final PressureUnit pressure;
}

/// How values are shown: units, clock, time zone and the spectrum's colors. The backend sends
/// knots, feet, ft/min, NM and hPa, and the app keeps them that way (gauges
/// and colours work on those); these settings only change the text, through
/// the formatting methods below. Saved with shared_preferences when loaded
/// with [load].
class DisplaySettings extends ChangeNotifier {
  /// Defaults (aviation units, 24 h, local time), not saved.
  DisplaySettings() : _prefs = null;

  DisplaySettings._(this._prefs);

  final SharedPreferencesAsync? _prefs;

  SpeedUnit _speed = SpeedUnit.kt;
  AltitudeUnit _altitude = AltitudeUnit.ft;
  VerticalRateUnit _verticalRate = VerticalRateUnit.fpm;
  DistanceUnit _distance = DistanceUnit.nm;
  PressureUnit _pressure = PressureUnit.hpa;
  ClockFormat _clock = ClockFormat.h24;
  TimeZoneMode _timeZone = TimeZoneMode.local;
  SpectrumColormap _spectrumColormap = SpectrumColormap.classic;
  double _spectrumLowDb = defaultSpectrumLowDb;
  double _spectrumHighDb = defaultSpectrumHighDb;

  // The spectrum's colors span its range in dB above the noise floor, rather than absolute dB: the bins aren't
  // calibrated, so the floor moves with the radio and its gain, and a range relative to it stays right.
  // Rather than scaled to the strongest bin either, so a row of noise stays dark and a burst stands out by its
  // strength above the noise.
  static const double spectrumMinDb = -20;
  static const double spectrumMaxDb = 80;
  static const double defaultSpectrumLowDb = -3;
  static const double defaultSpectrumHighDb = 27;

  SpeedUnit get speed => _speed;
  AltitudeUnit get altitude => _altitude;
  VerticalRateUnit get verticalRate => _verticalRate;
  DistanceUnit get distance => _distance;
  PressureUnit get pressure => _pressure;
  ClockFormat get clock => _clock;
  TimeZoneMode get timeZone => _timeZone;
  SpectrumColormap get spectrumColormap => _spectrumColormap;
  /// Bottom of the spectrum's color range, in dB above the noise floor.
  double get spectrumLowDb => _spectrumLowDb;
  /// Top of the spectrum's color range, in dB above the noise floor.
  double get spectrumHighDb => _spectrumHighDb;

  set speed(SpeedUnit v) => _set(_speedKey, () => _speed = v, v);
  set altitude(AltitudeUnit v) => _set(_altitudeKey, () => _altitude = v, v);
  set verticalRate(VerticalRateUnit v) => _set(_verticalRateKey, () => _verticalRate = v, v);
  set distance(DistanceUnit v) => _set(_distanceKey, () => _distance = v, v);
  set pressure(PressureUnit v) => _set(_pressureKey, () => _pressure = v, v);
  set clock(ClockFormat v) => _set(_clockKey, () => _clock = v, v);
  set timeZone(TimeZoneMode v) => _set(_timeZoneKey, () => _timeZone = v, v);
  set spectrumColormap(SpectrumColormap v) => _set(_spectrumColormapKey, () => _spectrumColormap = v, v);

  /// Sets the spectrum's color range, clamped to [spectrumMinDb, spectrumMaxDb].
  void setSpectrumRange(double lowDb, double highDb) {
    _spectrumLowDb = lowDb.clamp(spectrumMinDb, spectrumMaxDb);
    _spectrumHighDb = highDb.clamp(_spectrumLowDb, spectrumMaxDb);
    notifyListeners();
    for (final (key, value) in [(_spectrumLowKey, _spectrumLowDb), (_spectrumHighKey, _spectrumHighDb)]) {
      _prefs?.setDouble(key, value).catchError((Object e) => debugPrint('display settings: not saved: $e'));
    }
  }

  static const _speedKey = 'units.speed';
  static const _altitudeKey = 'units.altitude';
  static const _verticalRateKey = 'units.verticalRate';
  static const _distanceKey = 'units.distance';
  static const _pressureKey = 'units.pressure';
  static const _clockKey = 'time.clock';
  static const _timeZoneKey = 'time.zone';
  static const _spectrumColormapKey = 'spectrum.colormap';
  static const _spectrumLowKey = 'spectrum.lowDb';
  static const _spectrumHighKey = 'spectrum.highDb';

  /// The saved settings, or the defaults for any not saved (or if they
  /// can't be read).
  static Future<DisplaySettings> load() async {
    final prefs = SharedPreferencesAsync();
    final s = DisplaySettings._(prefs);
    try {
      T pick<T extends Enum>(List<T> values, String? name, T fallback) =>
          values.where((v) => v.name == name).firstOrNull ?? fallback;
      s._speed = pick(SpeedUnit.values, await prefs.getString(_speedKey), s._speed);
      s._altitude = pick(AltitudeUnit.values, await prefs.getString(_altitudeKey), s._altitude);
      s._verticalRate = pick(VerticalRateUnit.values, await prefs.getString(_verticalRateKey), s._verticalRate);
      s._distance = pick(DistanceUnit.values, await prefs.getString(_distanceKey), s._distance);
      s._pressure = pick(PressureUnit.values, await prefs.getString(_pressureKey), s._pressure);
      s._clock = pick(ClockFormat.values, await prefs.getString(_clockKey), s._clock);
      s._timeZone = pick(TimeZoneMode.values, await prefs.getString(_timeZoneKey), s._timeZone);
      s._spectrumColormap =
          pick(SpectrumColormap.values, await prefs.getString(_spectrumColormapKey), s._spectrumColormap);
      final low = await prefs.getDouble(_spectrumLowKey);
      final high = await prefs.getDouble(_spectrumHighKey);
      // Both or neither, and in range, so a bad pair can't leave the waterfall one flat color.
      if (low != null && high != null && spectrumMinDb <= low && low < high && high <= spectrumMaxDb) {
        s._spectrumLowDb = low;
        s._spectrumHighDb = high;
      }
    } catch (e) {
      debugPrint('display settings: using defaults, could not read saved ones: $e');
    }
    return s;
  }

  void _set(String key, void Function() assign, Enum value) {
    assign();
    notifyListeners();
    // Fire and forget: a failed save only loses the setting next launch.
    _prefs?.setString(key, value.name).catchError((Object e) => debugPrint('display settings: not saved: $e'));
  }

  /// The preset the current units match, or null if they're a mix.
  UnitPreset? get preset => UnitPreset.values
      .where((p) =>
          p.speed == _speed &&
          p.altitude == _altitude &&
          p.verticalRate == _verticalRate &&
          p.distance == _distance &&
          p.pressure == _pressure)
      .firstOrNull;

  void applyPreset(UnitPreset p) {
    speed = p.speed;
    altitude = p.altitude;
    verticalRate = p.verticalRate;
    distance = p.distance;
    pressure = p.pressure;
  }

  // ---- Formatting. Without `unit`, just the number, for under a column
  // header that names the unit.

  String formatSpeed(num kt, {bool unit = true}) =>
      _withUnit((kt * _speed.perKt).toStringAsFixed(0), _speed.label, unit);

  String formatAltitude(int ft, {bool unit = true}) =>
      _withUnit((ft * _altitude.perFt).round().toString(), _altitude.label, unit);

  String formatVerticalRate(int fpm, {bool unit = true}) => _withUnit(
      _verticalRate == VerticalRateUnit.fpm ? '$fpm' : (fpm * _verticalRate.perFpm).toStringAsFixed(1),
      _verticalRate.label,
      unit);

  String formatPressure(double hpa) => _pressure == PressureUnit.hpa
      ? '${hpa.toStringAsFixed(1)} hPa'
      : '${(hpa * _pressure.perHpa).toStringAsFixed(2)} inHg';

  /// A distance given in metres: in the distance unit, or, under a tenth of
  /// a NM, in feet (with miles) or metres (otherwise).
  String formatDistanceM(double m) {
    final nm = m / 1852;
    if (nm >= 0.1 - 1e-9) {
      final v = nm * _distance.perNm;
      final text = v.toStringAsFixed(v >= 10 ? 0 : v >= 1 ? 1 : 2);
      // 0.50 -> 0.5, 2.0 -> 2
      final trimmed = text.contains('.') ? text.replaceFirst(RegExp(r'\.?0+$'), '') : text;
      return '$trimmed ${_distance.label}';
    }
    return _distance == DistanceUnit.mi ? '${(m / 0.3048).round()} ft' : '${m.round()} m';
  }

  static String _withUnit(String value, String label, bool unit) => unit ? '$value $label' : value;

  /// A Unix time as a DateTime in the display time zone.
  DateTime toDisplayTime(double unixS) =>
      DateTime.fromMillisecondsSinceEpoch((unixS * 1000).round(), isUtc: _timeZone == TimeZoneMode.utc);

  /// The Unix time of a wall-clock time in the display time zone.
  double fromDisplayTime(int year, int month, int day, int hour, int minute) =>
      (_timeZone == TimeZoneMode.utc
              ? DateTime.utc(year, month, day, hour, minute)
              : DateTime(year, month, day, hour, minute))
          .millisecondsSinceEpoch /
      1000;

  /// Seconds the display time zone is ahead of UTC at `unixS`.
  int utcOffsetS(double unixS) => toDisplayTime(unixS).timeZoneOffset.inSeconds;

  /// Date and time of a Unix time, to the second, e.g. 2026-10-02 14:05:09 or
  /// 2026-10-02 2:05:09 PM.
  String formatDateTime(double unixS) {
    final t = toDisplayTime(unixS);
    String two(int n) => n.toString().padLeft(2, '0');
    final date = '${t.year}-${two(t.month)}-${two(t.day)}';
    if (_clock == ClockFormat.h24) return '$date ${two(t.hour)}:${two(t.minute)}:${two(t.second)}';
    final h = t.hour % 12 == 0 ? 12 : t.hour % 12;
    return '$date $h:${two(t.minute)}:${two(t.second)} ${t.hour < 12 ? 'AM' : 'PM'}';
  }
}
