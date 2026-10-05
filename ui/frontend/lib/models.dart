import 'package:latlong2/latlong.dart';

/// One decoded frame, as published by the backend's --ws-port
/// (see src/decode/frame_decode.cpp: to_json).
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
/// --ws-port on every change (see src/decode/aircraft.cpp: to_json).
/// Unlike FrameRecord this isn't appended to a log -- each message carries
/// the aircraft's full current state, so the frontend just replaces its map
/// entry for that ICAO wholesale.
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

/// The sky at one time, from the backend's history db (--history-db; see
/// src/output/aircraft_history.h: aircraft_history_snapshot): every
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

HistoryPoint historyPointOf(AircraftRecord a) => (
      t: a.lastSeenUnixS,
      altFt: a.altitudeFt,
      gsKt: a.groundSpeedKt,
      vrFpm: a.verticalRateFpm,
      signalDb: a.signalDb,
      selAltFt: a.selectedAltitudeFt,
    );

/// One FFT snapshot of the raw pre-resample IQ, as published by the
/// backend's --ws-port (see src/dsp/spectrum.cpp: to_json). `bins` is
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
/// (an `input_status` message, sent every couple of seconds while packets
/// arrive).
class InputStatus {
  InputStatus.fromJson(Map<String, dynamic> j)
      : ok = j['ok'] as bool,
        summary = j['summary'] as String,
        rateHz = (j['rate_hz'] as num).toDouble(),
        delivered = _delivered(j),
        received = DateTime.now();

  final bool ok;
  final String summary;
  final double rateHz;

  /// Fraction of the real-time signal that was demodulated.
  final double delivered;
  final DateTime received;

  static double _delivered(Map<String, dynamic> j) {
    final window = (j['window_s'] as num).toDouble();
    if (window <= 0) return 0;
    final d = ((j['stream_s'] as num) - (j['lost_s'] as num) - (j['dropped_s'] as num)) / window;
    return d.clamp(0.0, 1.0).toDouble();
  }
}
