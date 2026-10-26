// This is a generated file - do not edit.
//
// Generated from adsb/api/v1/api.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use capturePointDescriptor instead')
const CapturePoint$json = {
  '1': 'CapturePoint',
  '2': [
    {'1': 'CAPTURE_POINT_UNSPECIFIED', '2': 0},
    {'1': 'INPUT', '2': 1},
    {'1': 'ENVELOPE', '2': 2},
  ],
};

/// Descriptor for `CapturePoint`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List capturePointDescriptor = $convert.base64Decode(
    'CgxDYXB0dXJlUG9pbnQSHQoZQ0FQVFVSRV9QT0lOVF9VTlNQRUNJRklFRBAAEgkKBUlOUFVUEA'
    'ESDAoIRU5WRUxPUEUQAg==');

@$core.Deprecated('Use getReceiverInfoRequestDescriptor instead')
const GetReceiverInfoRequest$json = {
  '1': 'GetReceiverInfoRequest',
};

/// Descriptor for `GetReceiverInfoRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getReceiverInfoRequestDescriptor =
    $convert.base64Decode('ChZHZXRSZWNlaXZlckluZm9SZXF1ZXN0');

@$core.Deprecated('Use receiverInfoDescriptor instead')
const ReceiverInfo$json = {
  '1': 'ReceiverInfo',
  '2': [
    {'1': 'source', '3': 1, '4': 1, '5': 9, '10': 'source'},
    {
      '1': 'default_interval_s',
      '3': 2,
      '4': 1,
      '5': 1,
      '10': 'defaultIntervalS'
    },
    {'1': 'max_capture_s', '3': 3, '4': 1, '5': 1, '10': 'maxCaptureS'},
  ],
};

/// Descriptor for `ReceiverInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List receiverInfoDescriptor = $convert.base64Decode(
    'CgxSZWNlaXZlckluZm8SFgoGc291cmNlGAEgASgJUgZzb3VyY2USLAoSZGVmYXVsdF9pbnRlcn'
    'ZhbF9zGAIgASgBUhBkZWZhdWx0SW50ZXJ2YWxTEiIKDW1heF9jYXB0dXJlX3MYAyABKAFSC21h'
    'eENhcHR1cmVT');

@$core.Deprecated('Use watchFramesRequestDescriptor instead')
const WatchFramesRequest$json = {
  '1': 'WatchFramesRequest',
  '2': [
    {
      '1': 'interval_s',
      '3': 1,
      '4': 1,
      '5': 1,
      '9': 0,
      '10': 'intervalS',
      '17': true
    },
  ],
  '8': [
    {'1': '_interval_s'},
  ],
};

/// Descriptor for `WatchFramesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List watchFramesRequestDescriptor = $convert.base64Decode(
    'ChJXYXRjaEZyYW1lc1JlcXVlc3QSIgoKaW50ZXJ2YWxfcxgBIAEoAUgAUglpbnRlcnZhbFOIAQ'
    'FCDQoLX2ludGVydmFsX3M=');

@$core.Deprecated('Use frameBatchDescriptor instead')
const FrameBatch$json = {
  '1': 'FrameBatch',
  '2': [
    {
      '1': 'frames',
      '3': 1,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Frame',
      '10': 'frames'
    },
    {'1': 'frames_dropped', '3': 2, '4': 1, '5': 8, '10': 'framesDropped'},
  ],
};

/// Descriptor for `FrameBatch`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List frameBatchDescriptor = $convert.base64Decode(
    'CgpGcmFtZUJhdGNoEioKBmZyYW1lcxgBIAMoCzISLmFkc2IuYXBpLnYxLkZyYW1lUgZmcmFtZX'
    'MSJQoOZnJhbWVzX2Ryb3BwZWQYAiABKAhSDWZyYW1lc0Ryb3BwZWQ=');

@$core.Deprecated('Use frameDescriptor instead')
const Frame$json = {
  '1': 'Frame',
  '2': [
    {'1': 'idx', '3': 1, '4': 1, '5': 4, '10': 'idx'},
    {'1': 'bits', '3': 2, '4': 1, '5': 13, '10': 'bits'},
    {'1': 'confidence', '3': 3, '4': 1, '5': 2, '10': 'confidence'},
    {'1': 'df', '3': 4, '4': 1, '5': 13, '10': 'df'},
    {'1': 'icao', '3': 5, '4': 1, '5': 13, '9': 0, '10': 'icao', '17': true},
    {'1': 'crc_ok', '3': 6, '4': 1, '5': 8, '10': 'crcOk'},
    {'1': 'crc_checked', '3': 7, '4': 1, '5': 8, '10': 'crcChecked'},
    {
      '1': 'crc_fixed_bit',
      '3': 8,
      '4': 1,
      '5': 13,
      '9': 1,
      '10': 'crcFixedBit',
      '17': true
    },
    {'1': 'payload', '3': 9, '4': 1, '5': 12, '10': 'payload'},
    {'1': 'summary', '3': 10, '4': 1, '5': 9, '10': 'summary'},
  ],
  '8': [
    {'1': '_icao'},
    {'1': '_crc_fixed_bit'},
  ],
};

/// Descriptor for `Frame`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List frameDescriptor = $convert.base64Decode(
    'CgVGcmFtZRIQCgNpZHgYASABKARSA2lkeBISCgRiaXRzGAIgASgNUgRiaXRzEh4KCmNvbmZpZG'
    'VuY2UYAyABKAJSCmNvbmZpZGVuY2USDgoCZGYYBCABKA1SAmRmEhcKBGljYW8YBSABKA1IAFIE'
    'aWNhb4gBARIVCgZjcmNfb2sYBiABKAhSBWNyY09rEh8KC2NyY19jaGVja2VkGAcgASgIUgpjcm'
    'NDaGVja2VkEicKDWNyY19maXhlZF9iaXQYCCABKA1IAVILY3JjRml4ZWRCaXSIAQESGAoHcGF5'
    'bG9hZBgJIAEoDFIHcGF5bG9hZBIYCgdzdW1tYXJ5GAogASgJUgdzdW1tYXJ5QgcKBV9pY2FvQh'
    'AKDl9jcmNfZml4ZWRfYml0');

@$core.Deprecated('Use watchAircraftRequestDescriptor instead')
const WatchAircraftRequest$json = {
  '1': 'WatchAircraftRequest',
  '2': [
    {
      '1': 'interval_s',
      '3': 1,
      '4': 1,
      '5': 1,
      '9': 0,
      '10': 'intervalS',
      '17': true
    },
  ],
  '8': [
    {'1': '_interval_s'},
  ],
};

/// Descriptor for `WatchAircraftRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List watchAircraftRequestDescriptor = $convert.base64Decode(
    'ChRXYXRjaEFpcmNyYWZ0UmVxdWVzdBIiCgppbnRlcnZhbF9zGAEgASgBSABSCWludGVydmFsU4'
    'gBAUINCgtfaW50ZXJ2YWxfcw==');

@$core.Deprecated('Use aircraftChangesDescriptor instead')
const AircraftChanges$json = {
  '1': 'AircraftChanges',
  '2': [
    {
      '1': 'aircraft',
      '3': 1,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Aircraft',
      '10': 'aircraft'
    },
  ],
};

/// Descriptor for `AircraftChanges`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List aircraftChangesDescriptor = $convert.base64Decode(
    'Cg9BaXJjcmFmdENoYW5nZXMSMQoIYWlyY3JhZnQYASADKAsyFS5hZHNiLmFwaS52MS5BaXJjcm'
    'FmdFIIYWlyY3JhZnQ=');

@$core.Deprecated('Use aircraftDescriptor instead')
const Aircraft$json = {
  '1': 'Aircraft',
  '2': [
    {'1': 'icao', '3': 1, '4': 1, '5': 13, '10': 'icao'},
    {
      '1': 'callsign',
      '3': 2,
      '4': 1,
      '5': 9,
      '9': 0,
      '10': 'callsign',
      '17': true
    },
    {
      '1': 'category',
      '3': 3,
      '4': 1,
      '5': 9,
      '9': 1,
      '10': 'category',
      '17': true
    },
    {'1': 'squawk', '3': 4, '4': 1, '5': 9, '9': 2, '10': 'squawk', '17': true},
    {
      '1': 'altitude_ft',
      '3': 5,
      '4': 1,
      '5': 5,
      '9': 3,
      '10': 'altitudeFt',
      '17': true
    },
    {'1': 'lat', '3': 6, '4': 1, '5': 1, '9': 4, '10': 'lat', '17': true},
    {'1': 'lon', '3': 7, '4': 1, '5': 1, '9': 5, '10': 'lon', '17': true},
    {
      '1': 'ground_speed_kt',
      '3': 8,
      '4': 1,
      '5': 1,
      '9': 6,
      '10': 'groundSpeedKt',
      '17': true
    },
    {
      '1': 'track_deg',
      '3': 9,
      '4': 1,
      '5': 1,
      '9': 7,
      '10': 'trackDeg',
      '17': true
    },
    {
      '1': 'vertical_rate_fpm',
      '3': 10,
      '4': 1,
      '5': 5,
      '9': 8,
      '10': 'verticalRateFpm',
      '17': true
    },
    {
      '1': 'heading_deg',
      '3': 11,
      '4': 1,
      '5': 1,
      '9': 9,
      '10': 'headingDeg',
      '17': true
    },
    {
      '1': 'ias_kt',
      '3': 12,
      '4': 1,
      '5': 5,
      '9': 10,
      '10': 'iasKt',
      '17': true
    },
    {
      '1': 'tas_kt',
      '3': 13,
      '4': 1,
      '5': 5,
      '9': 11,
      '10': 'tasKt',
      '17': true
    },
    {'1': 'mach', '3': 14, '4': 1, '5': 1, '9': 12, '10': 'mach', '17': true},
    {
      '1': 'roll_deg',
      '3': 15,
      '4': 1,
      '5': 1,
      '9': 13,
      '10': 'rollDeg',
      '17': true
    },
    {
      '1': 'on_ground',
      '3': 16,
      '4': 1,
      '5': 8,
      '9': 14,
      '10': 'onGround',
      '17': true
    },
    {'1': 'alert', '3': 17, '4': 1, '5': 8, '10': 'alert'},
    {'1': 'spi', '3': 18, '4': 1, '5': 8, '10': 'spi'},
    {
      '1': 'selected_altitude_ft',
      '3': 19,
      '4': 1,
      '5': 5,
      '9': 15,
      '10': 'selectedAltitudeFt',
      '17': true
    },
    {
      '1': 'selected_altitude_source',
      '3': 20,
      '4': 1,
      '5': 14,
      '6': '.adsb.api.v1.Aircraft.AltitudeSource',
      '9': 16,
      '10': 'selectedAltitudeSource',
      '17': true
    },
    {
      '1': 'selected_heading_deg',
      '3': 21,
      '4': 1,
      '5': 1,
      '9': 17,
      '10': 'selectedHeadingDeg',
      '17': true
    },
    {
      '1': 'baro_setting_hpa',
      '3': 22,
      '4': 1,
      '5': 1,
      '9': 18,
      '10': 'baroSettingHpa',
      '17': true
    },
    {
      '1': 'autopilot_modes',
      '3': 23,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.AutopilotModes',
      '10': 'autopilotModes'
    },
    {
      '1': 'adsb_version',
      '3': 24,
      '4': 1,
      '5': 5,
      '9': 19,
      '10': 'adsbVersion',
      '17': true
    },
    {'1': 'nac_p', '3': 25, '4': 1, '5': 5, '9': 20, '10': 'nacP', '17': true},
    {'1': 'sil', '3': 26, '4': 1, '5': 5, '9': 21, '10': 'sil', '17': true},
    {'1': 'messages', '3': 27, '4': 1, '5': 4, '10': 'messages'},
    {
      '1': 'signal_db',
      '3': 28,
      '4': 1,
      '5': 1,
      '9': 22,
      '10': 'signalDb',
      '17': true
    },
    {'1': 'last_seen_s', '3': 29, '4': 1, '5': 1, '10': 'lastSeenS'},
    {'1': 'last_seen_unix_s', '3': 30, '4': 1, '5': 1, '10': 'lastSeenUnixS'},
  ],
  '4': [Aircraft_AltitudeSource$json],
  '8': [
    {'1': '_callsign'},
    {'1': '_category'},
    {'1': '_squawk'},
    {'1': '_altitude_ft'},
    {'1': '_lat'},
    {'1': '_lon'},
    {'1': '_ground_speed_kt'},
    {'1': '_track_deg'},
    {'1': '_vertical_rate_fpm'},
    {'1': '_heading_deg'},
    {'1': '_ias_kt'},
    {'1': '_tas_kt'},
    {'1': '_mach'},
    {'1': '_roll_deg'},
    {'1': '_on_ground'},
    {'1': '_selected_altitude_ft'},
    {'1': '_selected_altitude_source'},
    {'1': '_selected_heading_deg'},
    {'1': '_baro_setting_hpa'},
    {'1': '_adsb_version'},
    {'1': '_nac_p'},
    {'1': '_sil'},
    {'1': '_signal_db'},
  ],
};

@$core.Deprecated('Use aircraftDescriptor instead')
const Aircraft_AltitudeSource$json = {
  '1': 'AltitudeSource',
  '2': [
    {'1': 'ALTITUDE_SOURCE_UNSPECIFIED', '2': 0},
    {'1': 'MCP', '2': 1},
    {'1': 'FMS', '2': 2},
  ],
};

/// Descriptor for `Aircraft`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List aircraftDescriptor = $convert.base64Decode(
    'CghBaXJjcmFmdBISCgRpY2FvGAEgASgNUgRpY2FvEh8KCGNhbGxzaWduGAIgASgJSABSCGNhbG'
    'xzaWduiAEBEh8KCGNhdGVnb3J5GAMgASgJSAFSCGNhdGVnb3J5iAEBEhsKBnNxdWF3axgEIAEo'
    'CUgCUgZzcXVhd2uIAQESJAoLYWx0aXR1ZGVfZnQYBSABKAVIA1IKYWx0aXR1ZGVGdIgBARIVCg'
    'NsYXQYBiABKAFIBFIDbGF0iAEBEhUKA2xvbhgHIAEoAUgFUgNsb26IAQESKwoPZ3JvdW5kX3Nw'
    'ZWVkX2t0GAggASgBSAZSDWdyb3VuZFNwZWVkS3SIAQESIAoJdHJhY2tfZGVnGAkgASgBSAdSCH'
    'RyYWNrRGVniAEBEi8KEXZlcnRpY2FsX3JhdGVfZnBtGAogASgFSAhSD3ZlcnRpY2FsUmF0ZUZw'
    'bYgBARIkCgtoZWFkaW5nX2RlZxgLIAEoAUgJUgpoZWFkaW5nRGVniAEBEhoKBmlhc19rdBgMIA'
    'EoBUgKUgVpYXNLdIgBARIaCgZ0YXNfa3QYDSABKAVIC1IFdGFzS3SIAQESFwoEbWFjaBgOIAEo'
    'AUgMUgRtYWNoiAEBEh4KCHJvbGxfZGVnGA8gASgBSA1SB3JvbGxEZWeIAQESIAoJb25fZ3JvdW'
    '5kGBAgASgISA5SCG9uR3JvdW5kiAEBEhQKBWFsZXJ0GBEgASgIUgVhbGVydBIQCgNzcGkYEiAB'
    'KAhSA3NwaRI1ChRzZWxlY3RlZF9hbHRpdHVkZV9mdBgTIAEoBUgPUhJzZWxlY3RlZEFsdGl0dW'
    'RlRnSIAQESYwoYc2VsZWN0ZWRfYWx0aXR1ZGVfc291cmNlGBQgASgOMiQuYWRzYi5hcGkudjEu'
    'QWlyY3JhZnQuQWx0aXR1ZGVTb3VyY2VIEFIWc2VsZWN0ZWRBbHRpdHVkZVNvdXJjZYgBARI1Ch'
    'RzZWxlY3RlZF9oZWFkaW5nX2RlZxgVIAEoAUgRUhJzZWxlY3RlZEhlYWRpbmdEZWeIAQESLQoQ'
    'YmFyb19zZXR0aW5nX2hwYRgWIAEoAUgSUg5iYXJvU2V0dGluZ0hwYYgBARJECg9hdXRvcGlsb3'
    'RfbW9kZXMYFyABKAsyGy5hZHNiLmFwaS52MS5BdXRvcGlsb3RNb2Rlc1IOYXV0b3BpbG90TW9k'
    'ZXMSJgoMYWRzYl92ZXJzaW9uGBggASgFSBNSC2Fkc2JWZXJzaW9uiAEBEhgKBW5hY19wGBkgAS'
    'gFSBRSBG5hY1CIAQESFQoDc2lsGBogASgFSBVSA3NpbIgBARIaCghtZXNzYWdlcxgbIAEoBFII'
    'bWVzc2FnZXMSIAoJc2lnbmFsX2RiGBwgASgBSBZSCHNpZ25hbERiiAEBEh4KC2xhc3Rfc2Vlbl'
    '9zGB0gASgBUglsYXN0U2VlblMSJwoQbGFzdF9zZWVuX3VuaXhfcxgeIAEoAVINbGFzdFNlZW5V'
    'bml4UyJDCg5BbHRpdHVkZVNvdXJjZRIfChtBTFRJVFVERV9TT1VSQ0VfVU5TUEVDSUZJRUQQAB'
    'IHCgNNQ1AQARIHCgNGTVMQAkILCglfY2FsbHNpZ25CCwoJX2NhdGVnb3J5QgkKB19zcXVhd2tC'
    'DgoMX2FsdGl0dWRlX2Z0QgYKBF9sYXRCBgoEX2xvbkISChBfZ3JvdW5kX3NwZWVkX2t0QgwKCl'
    '90cmFja19kZWdCFAoSX3ZlcnRpY2FsX3JhdGVfZnBtQg4KDF9oZWFkaW5nX2RlZ0IJCgdfaWFz'
    'X2t0QgkKB190YXNfa3RCBwoFX21hY2hCCwoJX3JvbGxfZGVnQgwKCl9vbl9ncm91bmRCFwoVX3'
    'NlbGVjdGVkX2FsdGl0dWRlX2Z0QhsKGV9zZWxlY3RlZF9hbHRpdHVkZV9zb3VyY2VCFwoVX3Nl'
    'bGVjdGVkX2hlYWRpbmdfZGVnQhMKEV9iYXJvX3NldHRpbmdfaHBhQg8KDV9hZHNiX3ZlcnNpb2'
    '5CCAoGX25hY19wQgYKBF9zaWxCDAoKX3NpZ25hbF9kYg==');

@$core.Deprecated('Use autopilotModesDescriptor instead')
const AutopilotModes$json = {
  '1': 'AutopilotModes',
  '2': [
    {
      '1': 'modes',
      '3': 1,
      '4': 3,
      '5': 14,
      '6': '.adsb.api.v1.AutopilotModes.Mode',
      '10': 'modes'
    },
  ],
  '4': [AutopilotModes_Mode$json],
};

@$core.Deprecated('Use autopilotModesDescriptor instead')
const AutopilotModes_Mode$json = {
  '1': 'Mode',
  '2': [
    {'1': 'MODE_UNSPECIFIED', '2': 0},
    {'1': 'AP', '2': 1},
    {'1': 'VNAV', '2': 2},
    {'1': 'ALT', '2': 3},
    {'1': 'APP', '2': 4},
    {'1': 'LNAV', '2': 5},
  ],
};

/// Descriptor for `AutopilotModes`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List autopilotModesDescriptor = $convert.base64Decode(
    'Cg5BdXRvcGlsb3RNb2RlcxI2CgVtb2RlcxgBIAMoDjIgLmFkc2IuYXBpLnYxLkF1dG9waWxvdE'
    '1vZGVzLk1vZGVSBW1vZGVzIkoKBE1vZGUSFAoQTU9ERV9VTlNQRUNJRklFRBAAEgYKAkFQEAES'
    'CAoEVk5BVhACEgcKA0FMVBADEgcKA0FQUBAEEggKBExOQVYQBQ==');

@$core.Deprecated('Use watchInputStatusRequestDescriptor instead')
const WatchInputStatusRequest$json = {
  '1': 'WatchInputStatusRequest',
};

/// Descriptor for `WatchInputStatusRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List watchInputStatusRequestDescriptor =
    $convert.base64Decode('ChdXYXRjaElucHV0U3RhdHVzUmVxdWVzdA==');

@$core.Deprecated('Use inputStatusDescriptor instead')
const InputStatus$json = {
  '1': 'InputStatus',
  '2': [
    {'1': 'window_s', '3': 1, '4': 1, '5': 1, '10': 'windowS'},
    {'1': 'rate_hz', '3': 2, '4': 1, '5': 1, '10': 'rateHz'},
    {'1': 'stream_s', '3': 3, '4': 1, '5': 1, '10': 'streamS'},
    {'1': 'lost_s', '3': 4, '4': 1, '5': 1, '10': 'lostS'},
    {'1': 'dropped_s', '3': 5, '4': 1, '5': 1, '10': 'droppedS'},
    {
      '1': 'kernel_drops',
      '3': 6,
      '4': 1,
      '5': 4,
      '9': 0,
      '10': 'kernelDrops',
      '17': true
    },
    {'1': 'ok', '3': 7, '4': 1, '5': 8, '10': 'ok'},
    {'1': 'summary', '3': 8, '4': 1, '5': 9, '10': 'summary'},
  ],
  '8': [
    {'1': '_kernel_drops'},
  ],
};

/// Descriptor for `InputStatus`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List inputStatusDescriptor = $convert.base64Decode(
    'CgtJbnB1dFN0YXR1cxIZCgh3aW5kb3dfcxgBIAEoAVIHd2luZG93UxIXCgdyYXRlX2h6GAIgAS'
    'gBUgZyYXRlSHoSGQoIc3RyZWFtX3MYAyABKAFSB3N0cmVhbVMSFQoGbG9zdF9zGAQgASgBUgVs'
    'b3N0UxIbCglkcm9wcGVkX3MYBSABKAFSCGRyb3BwZWRTEiYKDGtlcm5lbF9kcm9wcxgGIAEoBE'
    'gAUgtrZXJuZWxEcm9wc4gBARIOCgJvaxgHIAEoCFICb2sSGAoHc3VtbWFyeRgIIAEoCVIHc3Vt'
    'bWFyeUIPCg1fa2VybmVsX2Ryb3Bz');

@$core.Deprecated('Use watchSpectrumRequestDescriptor instead')
const WatchSpectrumRequest$json = {
  '1': 'WatchSpectrumRequest',
};

/// Descriptor for `WatchSpectrumRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List watchSpectrumRequestDescriptor =
    $convert.base64Decode('ChRXYXRjaFNwZWN0cnVtUmVxdWVzdA==');

@$core.Deprecated('Use spectrumRowDescriptor instead')
const SpectrumRow$json = {
  '1': 'SpectrumRow',
  '2': [
    {'1': 'freq_hz', '3': 1, '4': 1, '5': 1, '10': 'freqHz'},
    {'1': 'rate_hz', '3': 2, '4': 1, '5': 1, '10': 'rateHz'},
    {'1': 'bins_db', '3': 3, '4': 3, '5': 2, '10': 'binsDb'},
  ],
};

/// Descriptor for `SpectrumRow`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List spectrumRowDescriptor = $convert.base64Decode(
    'CgtTcGVjdHJ1bVJvdxIXCgdmcmVxX2h6GAEgASgBUgZmcmVxSHoSFwoHcmF0ZV9oehgCIAEoAV'
    'IGcmF0ZUh6EhcKB2JpbnNfZGIYAyADKAJSBmJpbnNEYg==');

@$core.Deprecated('Use watchSettingsRequestDescriptor instead')
const WatchSettingsRequest$json = {
  '1': 'WatchSettingsRequest',
};

/// Descriptor for `WatchSettingsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List watchSettingsRequestDescriptor =
    $convert.base64Decode('ChRXYXRjaFNldHRpbmdzUmVxdWVzdA==');

@$core.Deprecated('Use settingsDescriptor instead')
const Settings$json = {
  '1': 'Settings',
  '2': [
    {'1': 'capture_dir', '3': 1, '4': 1, '5': 9, '10': 'captureDir'},
  ],
};

/// Descriptor for `Settings`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List settingsDescriptor = $convert.base64Decode(
    'CghTZXR0aW5ncxIfCgtjYXB0dXJlX2RpchgBIAEoCVIKY2FwdHVyZURpcg==');

@$core.Deprecated('Use updateSettingsRequestDescriptor instead')
const UpdateSettingsRequest$json = {
  '1': 'UpdateSettingsRequest',
  '2': [
    {
      '1': 'capture_dir',
      '3': 1,
      '4': 1,
      '5': 9,
      '9': 0,
      '10': 'captureDir',
      '17': true
    },
  ],
  '8': [
    {'1': '_capture_dir'},
  ],
};

/// Descriptor for `UpdateSettingsRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateSettingsRequestDescriptor = $convert.base64Decode(
    'ChVVcGRhdGVTZXR0aW5nc1JlcXVlc3QSJAoLY2FwdHVyZV9kaXIYASABKAlIAFIKY2FwdHVyZU'
    'RpcogBAUIOCgxfY2FwdHVyZV9kaXI=');

@$core.Deprecated('Use recordCaptureRequestDescriptor instead')
const RecordCaptureRequest$json = {
  '1': 'RecordCaptureRequest',
  '2': [
    {
      '1': 'point',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.adsb.api.v1.CapturePoint',
      '10': 'point'
    },
    {'1': 'duration_s', '3': 2, '4': 1, '5': 1, '10': 'durationS'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '10': 'name'},
  ],
};

/// Descriptor for `RecordCaptureRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List recordCaptureRequestDescriptor = $convert.base64Decode(
    'ChRSZWNvcmRDYXB0dXJlUmVxdWVzdBIvCgVwb2ludBgBIAEoDjIZLmFkc2IuYXBpLnYxLkNhcH'
    'R1cmVQb2ludFIFcG9pbnQSHQoKZHVyYXRpb25fcxgCIAEoAVIJZHVyYXRpb25TEhIKBG5hbWUY'
    'AyABKAlSBG5hbWU=');

@$core.Deprecated('Use recordCaptureProgressDescriptor instead')
const RecordCaptureProgress$json = {
  '1': 'RecordCaptureProgress',
  '2': [
    {'1': 'start_index', '3': 1, '4': 1, '5': 4, '10': 'startIndex'},
    {'1': 'samples', '3': 2, '4': 1, '5': 4, '10': 'samples'},
    {'1': 'missing', '3': 3, '4': 1, '5': 4, '10': 'missing'},
    {
      '1': 'capture',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Capture',
      '10': 'capture'
    },
  ],
};

/// Descriptor for `RecordCaptureProgress`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List recordCaptureProgressDescriptor = $convert.base64Decode(
    'ChVSZWNvcmRDYXB0dXJlUHJvZ3Jlc3MSHwoLc3RhcnRfaW5kZXgYASABKARSCnN0YXJ0SW5kZX'
    'gSGAoHc2FtcGxlcxgCIAEoBFIHc2FtcGxlcxIYCgdtaXNzaW5nGAMgASgEUgdtaXNzaW5nEi4K'
    'B2NhcHR1cmUYBCABKAsyFC5hZHNiLmFwaS52MS5DYXB0dXJlUgdjYXB0dXJl');

@$core.Deprecated('Use listCapturesRequestDescriptor instead')
const ListCapturesRequest$json = {
  '1': 'ListCapturesRequest',
};

/// Descriptor for `ListCapturesRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List listCapturesRequestDescriptor =
    $convert.base64Decode('ChNMaXN0Q2FwdHVyZXNSZXF1ZXN0');

@$core.Deprecated('Use listCapturesResponseDescriptor instead')
const ListCapturesResponse$json = {
  '1': 'ListCapturesResponse',
  '2': [
    {'1': 'dir', '3': 1, '4': 1, '5': 9, '10': 'dir'},
    {
      '1': 'captures',
      '3': 2,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Capture',
      '10': 'captures'
    },
    {'1': 'not_listed', '3': 3, '4': 1, '5': 13, '10': 'notListed'},
  ],
};

/// Descriptor for `ListCapturesResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List listCapturesResponseDescriptor = $convert.base64Decode(
    'ChRMaXN0Q2FwdHVyZXNSZXNwb25zZRIQCgNkaXIYASABKAlSA2RpchIwCghjYXB0dXJlcxgCIA'
    'MoCzIULmFkc2IuYXBpLnYxLkNhcHR1cmVSCGNhcHR1cmVzEh0KCm5vdF9saXN0ZWQYAyABKA1S'
    'CW5vdExpc3RlZA==');

@$core.Deprecated('Use captureDescriptor instead')
const Capture$json = {
  '1': 'Capture',
  '2': [
    {'1': 'name', '3': 1, '4': 1, '5': 9, '10': 'name'},
    {'1': 'data_path', '3': 2, '4': 1, '5': 9, '10': 'dataPath'},
    {'1': 'data_bytes', '3': 3, '4': 1, '5': 4, '10': 'dataBytes'},
    {'1': 'modified_unix_s', '3': 4, '4': 1, '5': 1, '10': 'modifiedUnixS'},
    {
      '1': 'meta',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.CaptureMeta',
      '10': 'meta'
    },
  ],
};

/// Descriptor for `Capture`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List captureDescriptor = $convert.base64Decode(
    'CgdDYXB0dXJlEhIKBG5hbWUYASABKAlSBG5hbWUSGwoJZGF0YV9wYXRoGAIgASgJUghkYXRhUG'
    'F0aBIdCgpkYXRhX2J5dGVzGAMgASgEUglkYXRhQnl0ZXMSJgoPbW9kaWZpZWRfdW5peF9zGAQg'
    'ASgBUg1tb2RpZmllZFVuaXhTEiwKBG1ldGEYBSABKAsyGC5hZHNiLmFwaS52MS5DYXB0dXJlTW'
    'V0YVIEbWV0YQ==');

@$core.Deprecated('Use captureMetaDescriptor instead')
const CaptureMeta$json = {
  '1': 'CaptureMeta',
  '2': [
    {'1': 'datatype', '3': 1, '4': 1, '5': 9, '10': 'datatype'},
    {'1': 'rate_hz', '3': 2, '4': 1, '5': 1, '10': 'rateHz'},
    {
      '1': 'freq_hz',
      '3': 3,
      '4': 1,
      '5': 1,
      '9': 0,
      '10': 'freqHz',
      '17': true
    },
    {'1': 'description', '3': 4, '4': 1, '5': 9, '10': 'description'},
    {'1': 'duration_s', '3': 5, '4': 1, '5': 1, '10': 'durationS'},
  ],
  '8': [
    {'1': '_freq_hz'},
  ],
};

/// Descriptor for `CaptureMeta`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List captureMetaDescriptor = $convert.base64Decode(
    'CgtDYXB0dXJlTWV0YRIaCghkYXRhdHlwZRgBIAEoCVIIZGF0YXR5cGUSFwoHcmF0ZV9oehgCIA'
    'EoAVIGcmF0ZUh6EhwKB2ZyZXFfaHoYAyABKAFIAFIGZnJlcUh6iAEBEiAKC2Rlc2NyaXB0aW9u'
    'GAQgASgJUgtkZXNjcmlwdGlvbhIdCgpkdXJhdGlvbl9zGAUgASgBUglkdXJhdGlvblNCCgoIX2'
    'ZyZXFfaHo=');

@$core.Deprecated('Use getSnapshotRequestDescriptor instead')
const GetSnapshotRequest$json = {
  '1': 'GetSnapshotRequest',
  '2': [
    {'1': 't', '3': 1, '4': 1, '5': 1, '9': 0, '10': 't', '17': true},
  ],
  '8': [
    {'1': '_t'},
  ],
};

/// Descriptor for `GetSnapshotRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getSnapshotRequestDescriptor =
    $convert.base64Decode(
        'ChJHZXRTbmFwc2hvdFJlcXVlc3QSEQoBdBgBIAEoAUgAUgF0iAEBQgQKAl90');

@$core.Deprecated('Use snapshotDescriptor instead')
const Snapshot$json = {
  '1': 'Snapshot',
  '2': [
    {'1': 't', '3': 1, '4': 1, '5': 1, '10': 't'},
    {'1': 't_min', '3': 2, '4': 1, '5': 1, '9': 0, '10': 'tMin', '17': true},
    {'1': 't_max', '3': 3, '4': 1, '5': 1, '9': 1, '10': 'tMax', '17': true},
    {
      '1': 'aircraft',
      '3': 4,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Snapshot.Entry',
      '10': 'aircraft'
    },
  ],
  '3': [Snapshot_Entry$json],
  '8': [
    {'1': '_t_min'},
    {'1': '_t_max'},
  ],
};

@$core.Deprecated('Use snapshotDescriptor instead')
const Snapshot_Entry$json = {
  '1': 'Entry',
  '2': [
    {
      '1': 'state',
      '3': 1,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Aircraft',
      '10': 'state'
    },
    {
      '1': 'track',
      '3': 2,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.TrackPoint',
      '10': 'track'
    },
  ],
};

/// Descriptor for `Snapshot`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List snapshotDescriptor = $convert.base64Decode(
    'CghTbmFwc2hvdBIMCgF0GAEgASgBUgF0EhgKBXRfbWluGAIgASgBSABSBHRNaW6IAQESGAoFdF'
    '9tYXgYAyABKAFIAVIEdE1heIgBARI3CghhaXJjcmFmdBgEIAMoCzIbLmFkc2IuYXBpLnYxLlNu'
    'YXBzaG90LkVudHJ5UghhaXJjcmFmdBpjCgVFbnRyeRIrCgVzdGF0ZRgBIAEoCzIVLmFkc2IuYX'
    'BpLnYxLkFpcmNyYWZ0UgVzdGF0ZRItCgV0cmFjaxgCIAMoCzIXLmFkc2IuYXBpLnYxLlRyYWNr'
    'UG9pbnRSBXRyYWNrQggKBl90X21pbkIICgZfdF9tYXg=');

@$core.Deprecated('Use trackPointDescriptor instead')
const TrackPoint$json = {
  '1': 'TrackPoint',
  '2': [
    {'1': 'lat', '3': 1, '4': 1, '5': 1, '10': 'lat'},
    {'1': 'lon', '3': 2, '4': 1, '5': 1, '10': 'lon'},
    {
      '1': 'altitude_ft',
      '3': 3,
      '4': 1,
      '5': 5,
      '9': 0,
      '10': 'altitudeFt',
      '17': true
    },
  ],
  '8': [
    {'1': '_altitude_ft'},
  ],
};

/// Descriptor for `TrackPoint`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List trackPointDescriptor = $convert.base64Decode(
    'CgpUcmFja1BvaW50EhAKA2xhdBgBIAEoAVIDbGF0EhAKA2xvbhgCIAEoAVIDbG9uEiQKC2FsdG'
    'l0dWRlX2Z0GAMgASgFSABSCmFsdGl0dWRlRnSIAQFCDgoMX2FsdGl0dWRlX2Z0');

@$core.Deprecated('Use getAircraftHistoryRequestDescriptor instead')
const GetAircraftHistoryRequest$json = {
  '1': 'GetAircraftHistoryRequest',
  '2': [
    {'1': 'icao', '3': 1, '4': 1, '5': 13, '10': 'icao'},
    {'1': 't', '3': 2, '4': 1, '5': 1, '9': 0, '10': 't', '17': true},
  ],
  '8': [
    {'1': '_t'},
  ],
};

/// Descriptor for `GetAircraftHistoryRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getAircraftHistoryRequestDescriptor =
    $convert.base64Decode(
        'ChlHZXRBaXJjcmFmdEhpc3RvcnlSZXF1ZXN0EhIKBGljYW8YASABKA1SBGljYW8SEQoBdBgCIA'
        'EoAUgAUgF0iAEBQgQKAl90');

@$core.Deprecated('Use aircraftHistoryDescriptor instead')
const AircraftHistory$json = {
  '1': 'AircraftHistory',
  '2': [
    {'1': 't', '3': 1, '4': 1, '5': 1, '10': 't'},
    {
      '1': 'points',
      '3': 2,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.AircraftHistory.Point',
      '10': 'points'
    },
  ],
  '3': [AircraftHistory_Point$json],
};

@$core.Deprecated('Use aircraftHistoryDescriptor instead')
const AircraftHistory_Point$json = {
  '1': 'Point',
  '2': [
    {'1': 't', '3': 1, '4': 1, '5': 1, '10': 't'},
    {
      '1': 'altitude_ft',
      '3': 2,
      '4': 1,
      '5': 5,
      '9': 0,
      '10': 'altitudeFt',
      '17': true
    },
    {
      '1': 'ground_speed_kt',
      '3': 3,
      '4': 1,
      '5': 1,
      '9': 1,
      '10': 'groundSpeedKt',
      '17': true
    },
    {
      '1': 'vertical_rate_fpm',
      '3': 4,
      '4': 1,
      '5': 5,
      '9': 2,
      '10': 'verticalRateFpm',
      '17': true
    },
    {
      '1': 'signal_db',
      '3': 5,
      '4': 1,
      '5': 1,
      '9': 3,
      '10': 'signalDb',
      '17': true
    },
    {
      '1': 'selected_altitude_ft',
      '3': 6,
      '4': 1,
      '5': 5,
      '9': 4,
      '10': 'selectedAltitudeFt',
      '17': true
    },
  ],
  '8': [
    {'1': '_altitude_ft'},
    {'1': '_ground_speed_kt'},
    {'1': '_vertical_rate_fpm'},
    {'1': '_signal_db'},
    {'1': '_selected_altitude_ft'},
  ],
};

/// Descriptor for `AircraftHistory`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List aircraftHistoryDescriptor = $convert.base64Decode(
    'Cg9BaXJjcmFmdEhpc3RvcnkSDAoBdBgBIAEoAVIBdBI6CgZwb2ludHMYAiADKAsyIi5hZHNiLm'
    'FwaS52MS5BaXJjcmFmdEhpc3RvcnkuUG9pbnRSBnBvaW50cxrTAgoFUG9pbnQSDAoBdBgBIAEo'
    'AVIBdBIkCgthbHRpdHVkZV9mdBgCIAEoBUgAUgphbHRpdHVkZUZ0iAEBEisKD2dyb3VuZF9zcG'
    'VlZF9rdBgDIAEoAUgBUg1ncm91bmRTcGVlZEt0iAEBEi8KEXZlcnRpY2FsX3JhdGVfZnBtGAQg'
    'ASgFSAJSD3ZlcnRpY2FsUmF0ZUZwbYgBARIgCglzaWduYWxfZGIYBSABKAFIA1IIc2lnbmFsRG'
    'KIAQESNQoUc2VsZWN0ZWRfYWx0aXR1ZGVfZnQYBiABKAVIBFISc2VsZWN0ZWRBbHRpdHVkZUZ0'
    'iAEBQg4KDF9hbHRpdHVkZV9mdEISChBfZ3JvdW5kX3NwZWVkX2t0QhQKEl92ZXJ0aWNhbF9yYX'
    'RlX2ZwbUIMCgpfc2lnbmFsX2RiQhcKFV9zZWxlY3RlZF9hbHRpdHVkZV9mdA==');

@$core.Deprecated('Use getActivityRequestDescriptor instead')
const GetActivityRequest$json = {
  '1': 'GetActivityRequest',
  '2': [
    {'1': 'from', '3': 1, '4': 1, '5': 1, '9': 0, '10': 'from', '17': true},
  ],
  '8': [
    {'1': '_from'},
  ],
};

/// Descriptor for `GetActivityRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getActivityRequestDescriptor = $convert.base64Decode(
    'ChJHZXRBY3Rpdml0eVJlcXVlc3QSFwoEZnJvbRgBIAEoAUgAUgRmcm9tiAEBQgcKBV9mcm9t');

@$core.Deprecated('Use activityDescriptor instead')
const Activity$json = {
  '1': 'Activity',
  '2': [
    {'1': 'bucket_s', '3': 1, '4': 1, '5': 1, '10': 'bucketS'},
    {'1': 't0', '3': 2, '4': 1, '5': 1, '9': 0, '10': 't0', '17': true},
    {'1': 'counts', '3': 3, '4': 3, '5': 13, '10': 'counts'},
  ],
  '8': [
    {'1': '_t0'},
  ],
};

/// Descriptor for `Activity`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List activityDescriptor = $convert.base64Decode(
    'CghBY3Rpdml0eRIZCghidWNrZXRfcxgBIAEoAVIHYnVja2V0UxITCgJ0MBgCIAEoAUgAUgJ0MI'
    'gBARIWCgZjb3VudHMYAyADKA1SBmNvdW50c0IFCgNfdDA=');

@$core.Deprecated('Use getHeatmapRequestDescriptor instead')
const GetHeatmapRequest$json = {
  '1': 'GetHeatmapRequest',
  '2': [
    {'1': 'west', '3': 1, '4': 1, '5': 1, '10': 'west'},
    {'1': 'south', '3': 2, '4': 1, '5': 1, '10': 'south'},
    {'1': 'east', '3': 3, '4': 1, '5': 1, '10': 'east'},
    {'1': 'north', '3': 4, '4': 1, '5': 1, '10': 'north'},
    {'1': 'width', '3': 5, '4': 1, '5': 13, '10': 'width'},
    {'1': 'height', '3': 6, '4': 1, '5': 13, '10': 'height'},
    {'1': 't0', '3': 7, '4': 1, '5': 1, '9': 0, '10': 't0', '17': true},
    {'1': 't1', '3': 8, '4': 1, '5': 1, '9': 1, '10': 't1', '17': true},
  ],
  '8': [
    {'1': '_t0'},
    {'1': '_t1'},
  ],
};

/// Descriptor for `GetHeatmapRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getHeatmapRequestDescriptor = $convert.base64Decode(
    'ChFHZXRIZWF0bWFwUmVxdWVzdBISCgR3ZXN0GAEgASgBUgR3ZXN0EhQKBXNvdXRoGAIgASgBUg'
    'Vzb3V0aBISCgRlYXN0GAMgASgBUgRlYXN0EhQKBW5vcnRoGAQgASgBUgVub3J0aBIUCgV3aWR0'
    'aBgFIAEoDVIFd2lkdGgSFgoGaGVpZ2h0GAYgASgNUgZoZWlnaHQSEwoCdDAYByABKAFIAFICdD'
    'CIAQESEwoCdDEYCCABKAFIAVICdDGIAQFCBQoDX3QwQgUKA190MQ==');

@$core.Deprecated('Use heatmapDescriptor instead')
const Heatmap$json = {
  '1': 'Heatmap',
  '2': [
    {'1': 'max', '3': 1, '4': 1, '5': 13, '10': 'max'},
    {'1': 'cell_index', '3': 2, '4': 3, '5': 13, '10': 'cellIndex'},
    {'1': 'cell_count', '3': 3, '4': 3, '5': 13, '10': 'cellCount'},
  ],
};

/// Descriptor for `Heatmap`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List heatmapDescriptor = $convert.base64Decode(
    'CgdIZWF0bWFwEhAKA21heBgBIAEoDVIDbWF4Eh0KCmNlbGxfaW5kZXgYAiADKA1SCWNlbGxJbm'
    'RleBIdCgpjZWxsX2NvdW50GAMgAygNUgljZWxsQ291bnQ=');

const $core.Map<$core.String, $core.dynamic> ReceiverServiceBase$json = {
  '1': 'Receiver',
  '2': [
    {
      '1': 'GetReceiverInfo',
      '2': '.adsb.api.v1.GetReceiverInfoRequest',
      '3': '.adsb.api.v1.ReceiverInfo'
    },
    {
      '1': 'WatchFrames',
      '2': '.adsb.api.v1.WatchFramesRequest',
      '3': '.adsb.api.v1.FrameBatch',
      '6': true
    },
    {
      '1': 'WatchAircraft',
      '2': '.adsb.api.v1.WatchAircraftRequest',
      '3': '.adsb.api.v1.AircraftChanges',
      '6': true
    },
    {
      '1': 'WatchInputStatus',
      '2': '.adsb.api.v1.WatchInputStatusRequest',
      '3': '.adsb.api.v1.InputStatus',
      '6': true
    },
    {
      '1': 'WatchSpectrum',
      '2': '.adsb.api.v1.WatchSpectrumRequest',
      '3': '.adsb.api.v1.SpectrumRow',
      '6': true
    },
    {
      '1': 'WatchSettings',
      '2': '.adsb.api.v1.WatchSettingsRequest',
      '3': '.adsb.api.v1.Settings',
      '6': true
    },
    {
      '1': 'UpdateSettings',
      '2': '.adsb.api.v1.UpdateSettingsRequest',
      '3': '.adsb.api.v1.Settings'
    },
    {
      '1': 'RecordCapture',
      '2': '.adsb.api.v1.RecordCaptureRequest',
      '3': '.adsb.api.v1.RecordCaptureProgress',
      '6': true
    },
    {
      '1': 'ListCaptures',
      '2': '.adsb.api.v1.ListCapturesRequest',
      '3': '.adsb.api.v1.ListCapturesResponse'
    },
  ],
};

@$core.Deprecated('Use receiverServiceDescriptor instead')
const $core.Map<$core.String, $core.Map<$core.String, $core.dynamic>>
    ReceiverServiceBase$messageJson = {
  '.adsb.api.v1.GetReceiverInfoRequest': GetReceiverInfoRequest$json,
  '.adsb.api.v1.ReceiverInfo': ReceiverInfo$json,
  '.adsb.api.v1.WatchFramesRequest': WatchFramesRequest$json,
  '.adsb.api.v1.FrameBatch': FrameBatch$json,
  '.adsb.api.v1.Frame': Frame$json,
  '.adsb.api.v1.WatchAircraftRequest': WatchAircraftRequest$json,
  '.adsb.api.v1.AircraftChanges': AircraftChanges$json,
  '.adsb.api.v1.Aircraft': Aircraft$json,
  '.adsb.api.v1.AutopilotModes': AutopilotModes$json,
  '.adsb.api.v1.WatchInputStatusRequest': WatchInputStatusRequest$json,
  '.adsb.api.v1.InputStatus': InputStatus$json,
  '.adsb.api.v1.WatchSpectrumRequest': WatchSpectrumRequest$json,
  '.adsb.api.v1.SpectrumRow': SpectrumRow$json,
  '.adsb.api.v1.WatchSettingsRequest': WatchSettingsRequest$json,
  '.adsb.api.v1.Settings': Settings$json,
  '.adsb.api.v1.UpdateSettingsRequest': UpdateSettingsRequest$json,
  '.adsb.api.v1.RecordCaptureRequest': RecordCaptureRequest$json,
  '.adsb.api.v1.RecordCaptureProgress': RecordCaptureProgress$json,
  '.adsb.api.v1.Capture': Capture$json,
  '.adsb.api.v1.CaptureMeta': CaptureMeta$json,
  '.adsb.api.v1.ListCapturesRequest': ListCapturesRequest$json,
  '.adsb.api.v1.ListCapturesResponse': ListCapturesResponse$json,
};

/// Descriptor for `Receiver`. Decode as a `google.protobuf.ServiceDescriptorProto`.
final $typed_data.Uint8List receiverServiceDescriptor = $convert.base64Decode(
    'CghSZWNlaXZlchJRCg9HZXRSZWNlaXZlckluZm8SIy5hZHNiLmFwaS52MS5HZXRSZWNlaXZlck'
    'luZm9SZXF1ZXN0GhkuYWRzYi5hcGkudjEuUmVjZWl2ZXJJbmZvEkkKC1dhdGNoRnJhbWVzEh8u'
    'YWRzYi5hcGkudjEuV2F0Y2hGcmFtZXNSZXF1ZXN0GhcuYWRzYi5hcGkudjEuRnJhbWVCYXRjaD'
    'ABElIKDVdhdGNoQWlyY3JhZnQSIS5hZHNiLmFwaS52MS5XYXRjaEFpcmNyYWZ0UmVxdWVzdBoc'
    'LmFkc2IuYXBpLnYxLkFpcmNyYWZ0Q2hhbmdlczABElQKEFdhdGNoSW5wdXRTdGF0dXMSJC5hZH'
    'NiLmFwaS52MS5XYXRjaElucHV0U3RhdHVzUmVxdWVzdBoYLmFkc2IuYXBpLnYxLklucHV0U3Rh'
    'dHVzMAESTgoNV2F0Y2hTcGVjdHJ1bRIhLmFkc2IuYXBpLnYxLldhdGNoU3BlY3RydW1SZXF1ZX'
    'N0GhguYWRzYi5hcGkudjEuU3BlY3RydW1Sb3cwARJLCg1XYXRjaFNldHRpbmdzEiEuYWRzYi5h'
    'cGkudjEuV2F0Y2hTZXR0aW5nc1JlcXVlc3QaFS5hZHNiLmFwaS52MS5TZXR0aW5nczABEksKDl'
    'VwZGF0ZVNldHRpbmdzEiIuYWRzYi5hcGkudjEuVXBkYXRlU2V0dGluZ3NSZXF1ZXN0GhUuYWRz'
    'Yi5hcGkudjEuU2V0dGluZ3MSWAoNUmVjb3JkQ2FwdHVyZRIhLmFkc2IuYXBpLnYxLlJlY29yZE'
    'NhcHR1cmVSZXF1ZXN0GiIuYWRzYi5hcGkudjEuUmVjb3JkQ2FwdHVyZVByb2dyZXNzMAESUwoM'
    'TGlzdENhcHR1cmVzEiAuYWRzYi5hcGkudjEuTGlzdENhcHR1cmVzUmVxdWVzdBohLmFkc2IuYX'
    'BpLnYxLkxpc3RDYXB0dXJlc1Jlc3BvbnNl');

const $core.Map<$core.String, $core.dynamic> HistoryServiceBase$json = {
  '1': 'History',
  '2': [
    {
      '1': 'GetSnapshot',
      '2': '.adsb.api.v1.GetSnapshotRequest',
      '3': '.adsb.api.v1.Snapshot'
    },
    {
      '1': 'GetAircraftHistory',
      '2': '.adsb.api.v1.GetAircraftHistoryRequest',
      '3': '.adsb.api.v1.AircraftHistory'
    },
    {
      '1': 'GetActivity',
      '2': '.adsb.api.v1.GetActivityRequest',
      '3': '.adsb.api.v1.Activity'
    },
    {
      '1': 'GetHeatmap',
      '2': '.adsb.api.v1.GetHeatmapRequest',
      '3': '.adsb.api.v1.Heatmap'
    },
  ],
};

@$core.Deprecated('Use historyServiceDescriptor instead')
const $core.Map<$core.String, $core.Map<$core.String, $core.dynamic>>
    HistoryServiceBase$messageJson = {
  '.adsb.api.v1.GetSnapshotRequest': GetSnapshotRequest$json,
  '.adsb.api.v1.Snapshot': Snapshot$json,
  '.adsb.api.v1.Snapshot.Entry': Snapshot_Entry$json,
  '.adsb.api.v1.Aircraft': Aircraft$json,
  '.adsb.api.v1.AutopilotModes': AutopilotModes$json,
  '.adsb.api.v1.TrackPoint': TrackPoint$json,
  '.adsb.api.v1.GetAircraftHistoryRequest': GetAircraftHistoryRequest$json,
  '.adsb.api.v1.AircraftHistory': AircraftHistory$json,
  '.adsb.api.v1.AircraftHistory.Point': AircraftHistory_Point$json,
  '.adsb.api.v1.GetActivityRequest': GetActivityRequest$json,
  '.adsb.api.v1.Activity': Activity$json,
  '.adsb.api.v1.GetHeatmapRequest': GetHeatmapRequest$json,
  '.adsb.api.v1.Heatmap': Heatmap$json,
};

/// Descriptor for `History`. Decode as a `google.protobuf.ServiceDescriptorProto`.
final $typed_data.Uint8List historyServiceDescriptor = $convert.base64Decode(
    'CgdIaXN0b3J5EkUKC0dldFNuYXBzaG90Eh8uYWRzYi5hcGkudjEuR2V0U25hcHNob3RSZXF1ZX'
    'N0GhUuYWRzYi5hcGkudjEuU25hcHNob3QSWgoSR2V0QWlyY3JhZnRIaXN0b3J5EiYuYWRzYi5h'
    'cGkudjEuR2V0QWlyY3JhZnRIaXN0b3J5UmVxdWVzdBocLmFkc2IuYXBpLnYxLkFpcmNyYWZ0SG'
    'lzdG9yeRJFCgtHZXRBY3Rpdml0eRIfLmFkc2IuYXBpLnYxLkdldEFjdGl2aXR5UmVxdWVzdBoV'
    'LmFkc2IuYXBpLnYxLkFjdGl2aXR5EkIKCkdldEhlYXRtYXASHi5hZHNiLmFwaS52MS5HZXRIZW'
    'F0bWFwUmVxdWVzdBoULmFkc2IuYXBpLnYxLkhlYXRtYXA=');
