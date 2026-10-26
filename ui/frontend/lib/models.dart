import 'package:latlong2/latlong.dart';

// Prefixed: the generated Error would hide dart:core's.
import 'api/adsb/api/v1/api.pb.dart' as api;

/// An ICAO address as the UI shows and keys it: six lowercase hex digits.
String icaoHex(int icao) => icao.toRadixString(16).padLeft(6, '0');

/// One decoded frame, from the frames watch (api.Frame).
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
  /// In hex.
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

  factory FrameRecord.fromProto(api.Frame f) {
    return FrameRecord(
      idx: f.idx.toInt(),
      bits: f.bits,
      confidence: f.confidence,
      df: f.df,
      icao: f.hasIcao() ? icaoHex(f.icao) : null,
      crcOk: f.crcOk,
      crcChecked: f.crcChecked,
      crcFixedBit: f.hasCrcFixedBit() ? f.crcFixedBit : null,
      payload: [for (final b in f.payload) b.toRadixString(16).padLeft(2, '0')].join(),
      summary: f.summary.isEmpty ? null : f.summary,
    );
  }
}

/// Current tracked state of one aircraft (api.Aircraft), from the aircraft
/// watch or a snapshot, with each unset field null. Unlike FrameRecord this isn't
/// appended to a log -- each update carries the aircraft's full current
/// state, so the frontend just replaces its map entry for that ICAO wholesale.
class AircraftRecord {
  final String icao;
  final String? callsign;
  /// ADS-B emitter category code, e.g. "A3"; see categoryNames.
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

  factory AircraftRecord.fromProto(api.Aircraft a) {
    return AircraftRecord(
      icao: icaoHex(a.icao),
      callsign: a.hasCallsign() ? a.callsign : null,
      category: a.hasCategory() ? a.category : null,
      squawk: a.hasSquawk() ? a.squawk : null,
      altitudeFt: a.hasAltitudeFt() ? a.altitudeFt : null,
      lat: a.hasLat() ? a.lat : null,
      lon: a.hasLon() ? a.lon : null,
      groundSpeedKt: a.hasGroundSpeedKt() ? a.groundSpeedKt : null,
      trackDeg: a.hasTrackDeg() ? a.trackDeg : null,
      verticalRateFpm: a.hasVerticalRateFpm() ? a.verticalRateFpm : null,
      headingDeg: a.hasHeadingDeg() ? a.headingDeg : null,
      iasKt: a.hasIasKt() ? a.iasKt : null,
      tasKt: a.hasTasKt() ? a.tasKt : null,
      mach: a.hasMach() ? a.mach : null,
      rollDeg: a.hasRollDeg() ? a.rollDeg : null,
      onGround: a.hasOnGround() ? a.onGround : null,
      alert: a.alert,
      spi: a.spi,
      selectedAltitudeFt: a.hasSelectedAltitudeFt() ? a.selectedAltitudeFt : null,
      selectedAltitudeSource: a.hasSelectedAltitudeSource() ? a.selectedAltitudeSource.name : null,
      selectedHeadingDeg: a.hasSelectedHeadingDeg() ? a.selectedHeadingDeg : null,
      baroSettingHpa: a.hasBaroSettingHpa() ? a.baroSettingHpa : null,
      autopilotModes: a.hasAutopilotModes() ? [for (final m in a.autopilotModes.modes) m.name] : null,
      adsbVersion: a.hasAdsbVersion() ? a.adsbVersion : null,
      nacP: a.hasNacP() ? a.nacP : null,
      sil: a.hasSil() ? a.sil : null,
      lastSeenUnixS: a.lastSeenUnixS,
      messages: a.messages.toInt(),
      signalDb: a.hasSignalDb() ? a.signalDb : null,
    );
  }
}

/// Short names for ADS-B emitter categories (DO-260B 2.2.3.2.5.2); codes
/// missing here are reserved and shown as-is.
const categoryNames = {
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

/// What the emergency squawks mean.
const emergencySquawks = {'7500': 'Hijack', '7600': 'Radio failure', '7700': 'Emergency'};

/// The sky at one time, from the backend's history db (GetSnapshot; see
/// src/output/aircraft_history.h: AircraftHistory::snapshot): every
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

  factory AircraftSnapshot.fromProto(api.Snapshot s) {
    final aircraft = <String, AircraftRecord>{};
    final trails = <String, List<TrailPoint>>{};
    for (final e in s.aircraft) {
      final a = AircraftRecord.fromProto(e.state);
      aircraft[a.icao] = a;
      trails[a.icao] = [
        for (final p in e.track) (pos: LatLng(p.lat, p.lon), altFt: p.hasAltitudeFt() ? p.altitudeFt : null),
      ];
    }
    return AircraftSnapshot(
      timeS: s.t,
      startS: s.hasTMin() ? s.tMin : null,
      endS: s.hasTMax() ? s.tMax : null,
      aircraft: aircraft,
      trails: trails,
    );
  }
}

/// One point of a map trail, with the altitude there to colour it by.
typedef TrailPoint = ({LatLng pos, int? altFt});

/// One update from an aircraft's history, for the detail panel's charts.
typedef HistoryPoint = ({double t, int? altFt, double? gsKt, int? vrFpm, double? signalDb, int? selAltFt});

HistoryPoint historyPointOf(AircraftRecord a) => (
      t: a.lastSeenUnixS,
      altFt: a.altitudeFt,
      gsKt: a.groundSpeedKt,
      vrFpm: a.verticalRateFpm,
      signalDb: a.signalDb,
      selAltFt: a.selectedAltitudeFt,
    );

HistoryPoint historyPointFromProto(api.AircraftHistory_Point p) => (
      t: p.t,
      altFt: p.hasAltitudeFt() ? p.altitudeFt : null,
      gsKt: p.hasGroundSpeedKt() ? p.groundSpeedKt : null,
      vrFpm: p.hasVerticalRateFpm() ? p.verticalRateFpm : null,
      signalDb: p.hasSignalDb() ? p.signalDb : null,
      selAltFt: p.hasSelectedAltitudeFt() ? p.selectedAltitudeFt : null,
    );

// Downlink format codes ADS-B/Mode S actually defines. A frame decoded with
// a df outside this set almost certainly came from a false-positive preamble
// detection on noise, not a real transponder reply. df 24-31 all decode to
// Comm-D (ELM) -- the top 3 bits (110) are what's significant, not the full
// 5-bit value -- so the whole range counts as known.
/// DFs whose CRC validates the frame on its own.
const Set<int> selfCheckDfs = {11, 17, 18};

/// DFs whose parity field is ICAO XOR CRC (DF24 is every DF from 24 up).
const Set<int> addrParityDfs = {0, 4, 5, 16, 20, 21};

/// Sort order for the crc column: fail, unchecked, ok.
int crcRank(FrameRecord f) => f.crcFixedBit != null ? 2 : f.crcOk ? 3 : f.crcChecked ? 0 : 1;

final Set<int> knownDfValues = {0, 4, 5, 11, 16, 17, 18, 19, 20, 21, ...List.generate(8, (i) => 24 + i)};

/// Compares optional numbers, nulls first.
int compareNullable(num? a, num? b) {
  if (a == null && b == null) return 0;
  if (a == null) return -1;
  if (b == null) return 1;
  return a.compareTo(b);
}

/// The backend's report on whether its live input keeps up with real time
/// (api.InputStatus, from the input status watch, every couple of seconds
/// while packets arrive).
class InputStatus {
  InputStatus.fromProto(api.InputStatus s)
      : ok = s.ok,
        summary = s.summary,
        rateHz = s.rateHz,
        delivered = _delivered(s),
        received = DateTime.now();

  final bool ok;
  final String summary;
  final double rateHz;

  /// Fraction of the real-time signal that was demodulated.
  final double delivered;
  final DateTime received;

  static double _delivered(api.InputStatus s) {
    if (s.windowS <= 0) return 0;
    final d = (s.streamS - s.lostS - s.droppedS) / s.windowS;
    return d.clamp(0.0, 1.0).toDouble();
  }
}
