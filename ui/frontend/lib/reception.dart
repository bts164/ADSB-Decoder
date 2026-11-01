import 'dart:collection';
import 'dart:math' as math;

import 'package:latlong2/latlong.dart';

import 'models.dart';

// The data behind the Reception plots: where the receiver hears aircraft from, and how strongly, relative to
// the receiver's own position.

/// Positions farther than this are taken as bad decodes. An aircraft at 45,000 ft is over the horizon of a
/// 100 m antenna by about 280 NM.
const double receptionMaxRangeM = 300 * 1852.0;

const double _earthRadiusM = 6371008.8;

/// Range (m, great circle) and initial bearing (degrees true, 0-360) from `from` to `to`, on a sphere. Cheaper
/// than ReceiverPosition.rangeBearingTo's ellipsoid, for the many points of the coverage plot; within about
/// 0.5% of it.
({double rangeM, double bearingDeg}) sphereRangeBearing(LatLng from, LatLng to) {
  final lat1 = from.latitude * math.pi / 180, lat2 = to.latitude * math.pi / 180;
  final dLat = lat2 - lat1, dLon = (to.longitude - from.longitude) * math.pi / 180;
  final h = math.pow(math.sin(dLat / 2), 2) + math.cos(lat1) * math.cos(lat2) * math.pow(math.sin(dLon / 2), 2);
  final rangeM = 2 * _earthRadiusM * math.asin(math.min(1.0, math.sqrt(h)));
  final y = math.sin(dLon) * math.cos(lat2);
  final x = math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(dLon);
  return (rangeM: rangeM, bearingDeg: (math.atan2(y, x) * 180 / math.pi + 360) % 360);
}

/// One bearing sector's farthest reception.
typedef CoverageBin = ({double rangeM, String icao, int? altFt});

/// The farthest position heard in each of `bins` equal bearing sectors (sector 0 centred on north), from the
/// trails. A bin is null where nothing was heard.
///
/// A single bad position decode would otherwise set a sector's range on its own, so each aircraft counts in a
/// sector by its second-farthest position there: it takes two positions to reach a range. That reads a little
/// short, by about one position update's travel.
List<CoverageBin?> coverageOf(LatLng receiver, Map<String, List<TrailPoint>> trails, {int bins = 72}) {
  final width = 360 / bins;
  final coverage = List<CoverageBin?>.filled(bins, null);
  for (final MapEntry(key: icao, value: trail) in trails.entries) {
    // Per sector, this aircraft's two farthest positions so far, farthest first.
    final farthest = <int, (TrailPoint, double, TrailPoint?, double)>{};
    for (final p in trail) {
      final rb = sphereRangeBearing(receiver, p.pos);
      if (rb.rangeM > receptionMaxRangeM) continue;
      final bin = ((rb.bearingDeg + width / 2) / width).floor() % bins;
      final two = farthest[bin];
      if (two == null) {
        farthest[bin] = (p, rb.rangeM, null, -1);
      } else if (rb.rangeM > two.$2) {
        farthest[bin] = (p, rb.rangeM, two.$1, two.$2);
      } else if (rb.rangeM > two.$4) {
        farthest[bin] = (two.$1, two.$2, p, rb.rangeM);
      }
    }
    for (final MapEntry(key: bin, value: (_, _, second, rangeM)) in farthest.entries) {
      if (second == null) continue;
      final best = coverage[bin];
      if (best == null || rangeM > best.rangeM) coverage[bin] = (rangeM: rangeM, icao: icao, altFt: second.altFt);
    }
  }
  return coverage;
}

/// One aircraft's signal at one new position, for the signal-vs-range plot.
typedef SignalSample = ({String icao, double t, double rangeM, double bearingDeg, double signalDb});

/// The signal samples since connecting, oldest first, capped at [maxSamples] (the oldest dropped first).
class SignalLog {
  static const int maxSamples = 5000;

  final Queue<SignalSample> _samples = Queue<SignalSample>();

  Iterable<SignalSample> get samples => _samples;
  bool get isEmpty => _samples.isEmpty;

  /// Logs `a` if it has a signal and a position new since `previous`, its last update: the signal is a
  /// moving average that changes with every frame, so only a new position pairs it with a range.
  void add(AircraftRecord a, AircraftRecord? previous, ReceiverPosition receiver) {
    final signal = a.signalDb;
    if (signal == null || a.lat == null || a.lon == null) return;
    if (previous != null && previous.lat == a.lat && previous.lon == a.lon) return;
    final rb = receiver.rangeBearingTo(LatLng(a.lat!, a.lon!));
    if (rb == null || rb.rangeM > receptionMaxRangeM) return;
    _samples.add((icao: a.icao, t: a.lastSeenUnixS, rangeM: rb.rangeM, bearingDeg: rb.bearingDeg, signalDb: signal));
    while (_samples.length > maxSamples) {
      _samples.removeFirst();
    }
  }

  void clear() => _samples.clear();
}

/// The level, in dB at 1 km, of the free-space line (signal falling 20 dB per decade of range) that best fits
/// `samples`: the median of each sample's level brought back to 1 km. Null with no samples beyond 1 km, where
/// the line isn't meaningful.
double? freeSpaceFitDb(Iterable<SignalSample> samples) {
  final at1Km = [
    for (final s in samples)
      if (s.rangeM > 1000) s.signalDb + 20 * math.log(s.rangeM / 1000) / math.ln10,
  ]..sort();
  if (at1Km.isEmpty) return null;
  final mid = at1Km.length ~/ 2;
  return at1Km.length.isOdd ? at1Km[mid] : (at1Km[mid - 1] + at1Km[mid]) / 2;
}

/// The lowest altitude, in metres above the ground around the antenna, that's in line of sight at `rangeM`
/// from an antenna `antennaAglM` above that ground, over a smooth earth with standard refraction (4/3 earth
/// radius): d_km = 4.12 * (sqrt(h_antenna_m) + sqrt(h_aircraft_m)).
double radioHorizonAglM(double rangeM, double antennaAglM) {
  final beyond = rangeM / 1000 / 4.12 - math.sqrt(math.max(0, antennaAglM));
  return beyond > 0 ? beyond * beyond : 0;
}
