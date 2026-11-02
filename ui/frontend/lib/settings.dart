import 'dart:convert';
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
  int _spectrumPersistRows = defaultSpectrumPersistRows;
  bool _spectrumPsdColored = true;
  String _serverUrl = defaultServerUrl;
  bool _framesShown = false;
  bool _spectrumShown = false;
  bool _receptionShown = false;
  double _mapFraction = 0.5;
  double _dockWidth = 560;
  Map<String, double> _dockWeights = const {};
  int _frameSortColumn = 0;
  bool _frameSortAscending = false;
  int _aircraftSortColumn = 0;
  bool _aircraftSortAscending = true;
  bool _showStale = true;
  bool _positionOnly = false;

  // The spectrum's colors span its range in dB above the noise floor, rather than absolute dB: the bins aren't
  // calibrated, so the floor moves with the radio and its gain, and a range relative to it stays right.
  // Rather than scaled to the strongest bin either, so a row of noise stays dark and a burst stands out by its
  // strength above the noise.
  static const double spectrumMinDb = -20;
  static const double spectrumMaxDb = 80;
  static const double defaultSpectrumLowDb = -3;
  static const double defaultSpectrumHighDb = 27;
  /// How many rows the PSD's older traces fade over, at most the waterfall's 400.
  static const spectrumPersistChoices = [0, 5, 10, 20, 40, 80, 160, 400];
  static const int defaultSpectrumPersistRows = 40;
  static const defaultServerUrl = 'ws://127.0.0.1:8765';

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
  /// Rows behind the PSD's newest trace, fading out with age.
  int get spectrumPersistRows => _spectrumPersistRows;
  /// Whether the PSD's traces take the colormap's color for their level, as the waterfall's bins do.
  bool get spectrumPsdColored => _spectrumPsdColored;
  /// The WebSocket URL last connected to, to fill in the next time.
  String get serverUrl => _serverUrl;

  // The page's layout and tables, as it last left them. The page keeps its own copy and checks these against
  // what it has (a column that's gone, a fraction off the screen), so they only save, without notifying.
  bool get framesShown => _framesShown;
  bool get spectrumShown => _spectrumShown;
  bool get receptionShown => _receptionShown;
  /// The share of the aircraft pane's height above the table that the map takes.
  double get mapFraction => _mapFraction;
  double get dockWidth => _dockWidth;
  /// Each dock section's share of the dock's height, by name; those missing take the page's default.
  Map<String, double> get dockWeights => _dockWeights;
  int get frameSortColumn => _frameSortColumn;
  bool get frameSortAscending => _frameSortAscending;
  int get aircraftSortColumn => _aircraftSortColumn;
  bool get aircraftSortAscending => _aircraftSortAscending;
  bool get showStale => _showStale;
  bool get positionOnly => _positionOnly;

  set speed(SpeedUnit v) => _set(_speedKey, () => _speed = v, v);
  set altitude(AltitudeUnit v) => _set(_altitudeKey, () => _altitude = v, v);
  set verticalRate(VerticalRateUnit v) => _set(_verticalRateKey, () => _verticalRate = v, v);
  set distance(DistanceUnit v) => _set(_distanceKey, () => _distance = v, v);
  set pressure(PressureUnit v) => _set(_pressureKey, () => _pressure = v, v);
  set clock(ClockFormat v) => _set(_clockKey, () => _clock = v, v);
  set timeZone(TimeZoneMode v) => _set(_timeZoneKey, () => _timeZone = v, v);
  set spectrumColormap(SpectrumColormap v) => _set(_spectrumColormapKey, () => _spectrumColormap = v, v);

  set spectrumPersistRows(int v) {
    _spectrumPersistRows = v;
    notifyListeners();
    _prefs?.setInt(_spectrumPersistKey, v).catchError((Object e) => debugPrint('display settings: not saved: $e'));
  }

  set spectrumPsdColored(bool v) {
    _spectrumPsdColored = v;
    notifyListeners();
    _prefs?.setBool(_spectrumPsdColoredKey, v).catchError((Object e) => debugPrint('display settings: not saved: $e'));
  }

  // No notify: nothing shows it but the text field it came from.
  set serverUrl(String v) {
    if (v == _serverUrl) return;
    _serverUrl = v;
    _prefs?.setString(_serverUrlKey, v).catchError((Object e) => debugPrint('display settings: not saved: $e'));
  }

  set framesShown(bool v) => _save(_prefs?.setBool(_framesShownKey, _framesShown = v));
  set spectrumShown(bool v) => _save(_prefs?.setBool(_spectrumShownKey, _spectrumShown = v));
  set receptionShown(bool v) => _save(_prefs?.setBool(_receptionShownKey, _receptionShown = v));
  set mapFraction(double v) => _save(_prefs?.setDouble(_mapFractionKey, _mapFraction = v));
  set dockWidth(double v) => _save(_prefs?.setDouble(_dockWidthKey, _dockWidth = v));
  set dockWeights(Map<String, double> v) =>
      _save(_prefs?.setString(_dockWeightsKey, jsonEncode(_dockWeights = Map.unmodifiable(v))));
  set showStale(bool v) => _save(_prefs?.setBool(_showStaleKey, _showStale = v));
  set positionOnly(bool v) => _save(_prefs?.setBool(_positionOnlyKey, _positionOnly = v));

  void setFrameSort(int column, bool ascending) {
    _save(_prefs?.setInt(_frameSortColumnKey, _frameSortColumn = column));
    _save(_prefs?.setBool(_frameSortAscendingKey, _frameSortAscending = ascending));
  }

  void setAircraftSort(int column, bool ascending) {
    _save(_prefs?.setInt(_aircraftSortColumnKey, _aircraftSortColumn = column));
    _save(_prefs?.setBool(_aircraftSortAscendingKey, _aircraftSortAscending = ascending));
  }

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
  static const _spectrumPersistKey = 'spectrum.persistRows';
  static const _spectrumPsdColoredKey = 'spectrum.psdColored';
  static const _serverUrlKey = 'connection.url';
  static const _framesShownKey = 'layout.framesShown';
  static const _spectrumShownKey = 'layout.spectrumShown';
  static const _receptionShownKey = 'layout.receptionShown';
  static const _mapFractionKey = 'layout.mapFraction';
  static const _dockWidthKey = 'layout.dockWidth';
  static const _dockWeightsKey = 'layout.dockWeights';
  static const _frameSortColumnKey = 'frames.sortColumn';
  static const _frameSortAscendingKey = 'frames.sortAscending';
  static const _aircraftSortColumnKey = 'aircraft.sortColumn';
  static const _aircraftSortAscendingKey = 'aircraft.sortAscending';
  static const _showStaleKey = 'aircraft.showStale';
  static const _positionOnlyKey = 'aircraft.positionOnly';

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
      final persist = await prefs.getInt(_spectrumPersistKey);
      if (spectrumPersistChoices.contains(persist)) s._spectrumPersistRows = persist!;
      s._spectrumPsdColored = await prefs.getBool(_spectrumPsdColoredKey) ?? s._spectrumPsdColored;
      s._serverUrl = await prefs.getString(_serverUrlKey) ?? s._serverUrl;
      s._framesShown = await prefs.getBool(_framesShownKey) ?? s._framesShown;
      s._spectrumShown = await prefs.getBool(_spectrumShownKey) ?? s._spectrumShown;
      s._receptionShown = await prefs.getBool(_receptionShownKey) ?? s._receptionShown;
      final fraction = await prefs.getDouble(_mapFractionKey);
      if (fraction != null && 0 < fraction && fraction < 1) s._mapFraction = fraction;
      final width = await prefs.getDouble(_dockWidthKey);
      if (width != null && width.isFinite && width > 0) s._dockWidth = width;
      s._frameSortColumn = await prefs.getInt(_frameSortColumnKey) ?? s._frameSortColumn;
      s._frameSortAscending = await prefs.getBool(_frameSortAscendingKey) ?? s._frameSortAscending;
      s._aircraftSortColumn = await prefs.getInt(_aircraftSortColumnKey) ?? s._aircraftSortColumn;
      s._aircraftSortAscending = await prefs.getBool(_aircraftSortAscendingKey) ?? s._aircraftSortAscending;
      s._showStale = await prefs.getBool(_showStaleKey) ?? s._showStale;
      s._positionOnly = await prefs.getBool(_positionOnlyKey) ?? s._positionOnly;
      // Last, as the one that can fail to parse.
      final weights = await prefs.getString(_dockWeightsKey);
      if (weights != null) {
        s._dockWeights = Map.unmodifiable({
          for (final MapEntry(:key, :value) in (jsonDecode(weights) as Map<String, dynamic>).entries)
            if (value is num && value.isFinite && value > 0) key: value.toDouble(),
        });
      }
    } catch (e) {
      debugPrint('display settings: using defaults, could not read saved ones: $e');
    }
    return s;
  }

  void _set(String key, void Function() assign, Enum value) {
    assign();
    notifyListeners();
    _save(_prefs?.setString(key, value.name));
  }

  // Fire and forget: a failed save only loses the setting next launch.
  static void _save(Future<void>? write) =>
      write?.catchError((Object e) => debugPrint('display settings: not saved: $e'));

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
