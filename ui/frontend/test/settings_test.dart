import 'package:flutter_test/flutter_test.dart';

import 'package:adsb_ui/settings.dart';

void main() {
  test('defaults are aviation units, as the backend sends them', () {
    final s = DisplaySettings();
    expect(s.preset, UnitPreset.aviation);
    expect(s.formatSpeed(450.4), '450 kt');
    expect(s.formatAltitude(37000), '37000 ft');
    expect(s.formatVerticalRate(-1200), '-1200 fpm');
    expect(s.formatPressure(1013.2), '1013.2 hPa');
    expect(s.formatDistanceM(0.5 * 1852), '0.5 NM');
    expect(s.formatDistanceM(10 * 1852), '10 NM');
    expect(s.formatDistanceM(93), '93 m');
  });

  test('metric preset converts', () {
    final s = DisplaySettings()..applyPreset(UnitPreset.metric);
    expect(s.preset, UnitPreset.metric);
    expect(s.formatSpeed(100), '185 km/h');
    expect(s.formatAltitude(10000), '3048 m');
    expect(s.formatAltitude(10000, unit: false), '3048');
    expect(s.formatVerticalRate(1000), '5.1 m/s');
    expect(s.formatDistanceM(4 * 1852), '7.4 km');
  });

  test('imperial preset converts', () {
    final s = DisplaySettings()..applyPreset(UnitPreset.imperial);
    expect(s.formatSpeed(100), '115 mph');
    expect(s.formatPressure(1013.25), '29.92 inHg');
    expect(s.formatDistanceM(1852), '1.2 mi');
    expect(s.formatDistanceM(30), '98 ft');
  });

  test('a mix of units matches no preset', () {
    final s = DisplaySettings()..speed = SpeedUnit.kmh;
    expect(s.preset, isNull);
  });

  test('time in UTC, 24 and 12 hour', () {
    final s = DisplaySettings()..timeZone = TimeZoneMode.utc;
    const t = 1790950509.0; // 2026-10-02 14:15:09 UTC
    expect(s.formatDateTime(t), '2026-10-02 14:15:09');
    s.clock = ClockFormat.h12;
    expect(s.formatDateTime(t), '2026-10-02 2:15:09 PM');
    expect(s.formatDateTime(t - 14 * 3600), '2026-10-02 12:15:09 AM');
    expect(s.utcOffsetS(t), 0);
    expect(s.fromDisplayTime(2026, 10, 2, 14, 15), t - 9);
  });
}
