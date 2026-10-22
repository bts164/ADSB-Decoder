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

@$core.Deprecated('Use clientMessageDescriptor instead')
const ClientMessage$json = {
  '1': 'ClientMessage',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {
      '1': 'cancel',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Cancel',
      '9': 0,
      '10': 'cancel'
    },
    {
      '1': 'server_info',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.ServerInfoRequest',
      '9': 0,
      '10': 'serverInfo'
    },
    {
      '1': 'subscribe',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.SubscribeRequest',
      '9': 0,
      '10': 'subscribe'
    },
    {
      '1': 'spectrum',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.SpectrumRequest',
      '9': 0,
      '10': 'spectrum'
    },
    {
      '1': 'snapshot',
      '3': 10,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.SnapshotRequest',
      '9': 0,
      '10': 'snapshot'
    },
    {
      '1': 'aircraft_history',
      '3': 11,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.AircraftHistoryRequest',
      '9': 0,
      '10': 'aircraftHistory'
    },
    {
      '1': 'activity',
      '3': 12,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.ActivityRequest',
      '9': 0,
      '10': 'activity'
    },
    {
      '1': 'heatmap',
      '3': 13,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.HeatmapRequest',
      '9': 0,
      '10': 'heatmap'
    },
    {
      '1': 'capture',
      '3': 14,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.CaptureRequest',
      '9': 0,
      '10': 'capture'
    },
  ],
  '8': [
    {'1': 'call'},
  ],
};

/// Descriptor for `ClientMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List clientMessageDescriptor = $convert.base64Decode(
    'Cg1DbGllbnRNZXNzYWdlEg4KAmlkGAEgASgNUgJpZBItCgZjYW5jZWwYAiABKAsyEy5hZHNiLm'
    'FwaS52MS5DYW5jZWxIAFIGY2FuY2VsEkEKC3NlcnZlcl9pbmZvGAMgASgLMh4uYWRzYi5hcGku'
    'djEuU2VydmVySW5mb1JlcXVlc3RIAFIKc2VydmVySW5mbxI9CglzdWJzY3JpYmUYBCABKAsyHS'
    '5hZHNiLmFwaS52MS5TdWJzY3JpYmVSZXF1ZXN0SABSCXN1YnNjcmliZRI6CghzcGVjdHJ1bRgF'
    'IAEoCzIcLmFkc2IuYXBpLnYxLlNwZWN0cnVtUmVxdWVzdEgAUghzcGVjdHJ1bRI6CghzbmFwc2'
    'hvdBgKIAEoCzIcLmFkc2IuYXBpLnYxLlNuYXBzaG90UmVxdWVzdEgAUghzbmFwc2hvdBJQChBh'
    'aXJjcmFmdF9oaXN0b3J5GAsgASgLMiMuYWRzYi5hcGkudjEuQWlyY3JhZnRIaXN0b3J5UmVxdW'
    'VzdEgAUg9haXJjcmFmdEhpc3RvcnkSOgoIYWN0aXZpdHkYDCABKAsyHC5hZHNiLmFwaS52MS5B'
    'Y3Rpdml0eVJlcXVlc3RIAFIIYWN0aXZpdHkSNwoHaGVhdG1hcBgNIAEoCzIbLmFkc2IuYXBpLn'
    'YxLkhlYXRtYXBSZXF1ZXN0SABSB2hlYXRtYXASNwoHY2FwdHVyZRgOIAEoCzIbLmFkc2IuYXBp'
    'LnYxLkNhcHR1cmVSZXF1ZXN0SABSB2NhcHR1cmVCBgoEY2FsbA==');

@$core.Deprecated('Use serverMessageDescriptor instead')
const ServerMessage$json = {
  '1': 'ServerMessage',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {'1': 'end', '3': 2, '4': 1, '5': 8, '10': 'end'},
    {
      '1': 'error',
      '3': 9,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Error',
      '9': 0,
      '10': 'error'
    },
    {
      '1': 'server_info',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.ServerInfo',
      '9': 0,
      '10': 'serverInfo'
    },
    {
      '1': 'update',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Update',
      '9': 0,
      '10': 'update'
    },
    {
      '1': 'spectrum',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.SpectrumRow',
      '9': 0,
      '10': 'spectrum'
    },
    {
      '1': 'snapshot',
      '3': 10,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Snapshot',
      '9': 0,
      '10': 'snapshot'
    },
    {
      '1': 'aircraft_history',
      '3': 11,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.AircraftHistory',
      '9': 0,
      '10': 'aircraftHistory'
    },
    {
      '1': 'activity',
      '3': 12,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Activity',
      '9': 0,
      '10': 'activity'
    },
    {
      '1': 'heatmap',
      '3': 13,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.Heatmap',
      '9': 0,
      '10': 'heatmap'
    },
    {
      '1': 'capture',
      '3': 14,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.CaptureResult',
      '9': 0,
      '10': 'capture'
    },
  ],
  '8': [
    {'1': 'body'},
  ],
};

/// Descriptor for `ServerMessage`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverMessageDescriptor = $convert.base64Decode(
    'Cg1TZXJ2ZXJNZXNzYWdlEg4KAmlkGAEgASgNUgJpZBIQCgNlbmQYAiABKAhSA2VuZBIqCgVlcn'
    'JvchgJIAEoCzISLmFkc2IuYXBpLnYxLkVycm9ySABSBWVycm9yEjoKC3NlcnZlcl9pbmZvGAMg'
    'ASgLMhcuYWRzYi5hcGkudjEuU2VydmVySW5mb0gAUgpzZXJ2ZXJJbmZvEi0KBnVwZGF0ZRgEIA'
    'EoCzITLmFkc2IuYXBpLnYxLlVwZGF0ZUgAUgZ1cGRhdGUSNgoIc3BlY3RydW0YBSABKAsyGC5h'
    'ZHNiLmFwaS52MS5TcGVjdHJ1bVJvd0gAUghzcGVjdHJ1bRIzCghzbmFwc2hvdBgKIAEoCzIVLm'
    'Fkc2IuYXBpLnYxLlNuYXBzaG90SABSCHNuYXBzaG90EkkKEGFpcmNyYWZ0X2hpc3RvcnkYCyAB'
    'KAsyHC5hZHNiLmFwaS52MS5BaXJjcmFmdEhpc3RvcnlIAFIPYWlyY3JhZnRIaXN0b3J5EjMKCG'
    'FjdGl2aXR5GAwgASgLMhUuYWRzYi5hcGkudjEuQWN0aXZpdHlIAFIIYWN0aXZpdHkSMAoHaGVh'
    'dG1hcBgNIAEoCzIULmFkc2IuYXBpLnYxLkhlYXRtYXBIAFIHaGVhdG1hcBI2CgdjYXB0dXJlGA'
    '4gASgLMhouYWRzYi5hcGkudjEuQ2FwdHVyZVJlc3VsdEgAUgdjYXB0dXJlQgYKBGJvZHk=');

@$core.Deprecated('Use cancelDescriptor instead')
const Cancel$json = {
  '1': 'Cancel',
};

/// Descriptor for `Cancel`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List cancelDescriptor =
    $convert.base64Decode('CgZDYW5jZWw=');

@$core.Deprecated('Use errorDescriptor instead')
const Error$json = {
  '1': 'Error',
  '2': [
    {
      '1': 'code',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.adsb.api.v1.Error.Code',
      '10': 'code'
    },
    {'1': 'message', '3': 2, '4': 1, '5': 9, '10': 'message'},
  ],
  '4': [Error_Code$json],
};

@$core.Deprecated('Use errorDescriptor instead')
const Error_Code$json = {
  '1': 'Code',
  '2': [
    {'1': 'CODE_UNSPECIFIED', '2': 0},
    {'1': 'UNIMPLEMENTED', '2': 1},
    {'1': 'INVALID_ARGUMENT', '2': 2},
    {'1': 'NO_HISTORY', '2': 3},
    {'1': 'INTERNAL', '2': 4},
    {'1': 'CANCELLED', '2': 5},
    {'1': 'RESOURCE_EXHAUSTED', '2': 6},
    {'1': 'NO_LIVE_INPUT', '2': 7},
    {'1': 'NO_CAPTURE', '2': 8},
  ],
};

/// Descriptor for `Error`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List errorDescriptor = $convert.base64Decode(
    'CgVFcnJvchIrCgRjb2RlGAEgASgOMhcuYWRzYi5hcGkudjEuRXJyb3IuQ29kZVIEY29kZRIYCg'
    'dtZXNzYWdlGAIgASgJUgdtZXNzYWdlIq0BCgRDb2RlEhQKEENPREVfVU5TUEVDSUZJRUQQABIR'
    'Cg1VTklNUExFTUVOVEVEEAESFAoQSU5WQUxJRF9BUkdVTUVOVBACEg4KCk5PX0hJU1RPUlkQAx'
    'IMCghJTlRFUk5BTBAEEg0KCUNBTkNFTExFRBAFEhYKElJFU09VUkNFX0VYSEFVU1RFRBAGEhEK'
    'DU5PX0xJVkVfSU5QVVQQBxIOCgpOT19DQVBUVVJFEAg=');

@$core.Deprecated('Use serverInfoRequestDescriptor instead')
const ServerInfoRequest$json = {
  '1': 'ServerInfoRequest',
};

/// Descriptor for `ServerInfoRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverInfoRequestDescriptor =
    $convert.base64Decode('ChFTZXJ2ZXJJbmZvUmVxdWVzdA==');

@$core.Deprecated('Use serverInfoDescriptor instead')
const ServerInfo$json = {
  '1': 'ServerInfo',
  '2': [
    {'1': 'history', '3': 1, '4': 1, '5': 8, '10': 'history'},
    {'1': 'spectrum', '3': 2, '4': 1, '5': 8, '10': 'spectrum'},
    {
      '1': 'default_interval_s',
      '3': 3,
      '4': 1,
      '5': 1,
      '10': 'defaultIntervalS'
    },
    {'1': 'capture', '3': 4, '4': 1, '5': 8, '10': 'capture'},
    {'1': 'capture_max_s', '3': 5, '4': 1, '5': 1, '10': 'captureMaxS'},
  ],
};

/// Descriptor for `ServerInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverInfoDescriptor = $convert.base64Decode(
    'CgpTZXJ2ZXJJbmZvEhgKB2hpc3RvcnkYASABKAhSB2hpc3RvcnkSGgoIc3BlY3RydW0YAiABKA'
    'hSCHNwZWN0cnVtEiwKEmRlZmF1bHRfaW50ZXJ2YWxfcxgDIAEoAVIQZGVmYXVsdEludGVydmFs'
    'UxIYCgdjYXB0dXJlGAQgASgIUgdjYXB0dXJlEiIKDWNhcHR1cmVfbWF4X3MYBSABKAFSC2NhcH'
    'R1cmVNYXhT');

@$core.Deprecated('Use subscribeRequestDescriptor instead')
const SubscribeRequest$json = {
  '1': 'SubscribeRequest',
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

/// Descriptor for `SubscribeRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List subscribeRequestDescriptor = $convert.base64Decode(
    'ChBTdWJzY3JpYmVSZXF1ZXN0EiIKCmludGVydmFsX3MYASABKAFIAFIJaW50ZXJ2YWxTiAEBQg'
    '0KC19pbnRlcnZhbF9z');

@$core.Deprecated('Use updateDescriptor instead')
const Update$json = {
  '1': 'Update',
  '2': [
    {
      '1': 'frames',
      '3': 1,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Frame',
      '10': 'frames'
    },
    {
      '1': 'aircraft',
      '3': 3,
      '4': 3,
      '5': 11,
      '6': '.adsb.api.v1.Aircraft',
      '10': 'aircraft'
    },
    {
      '1': 'input_status',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.adsb.api.v1.InputStatus',
      '10': 'inputStatus'
    },
  ],
};

/// Descriptor for `Update`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List updateDescriptor = $convert.base64Decode(
    'CgZVcGRhdGUSKgoGZnJhbWVzGAEgAygLMhIuYWRzYi5hcGkudjEuRnJhbWVSBmZyYW1lcxIxCg'
    'hhaXJjcmFmdBgDIAMoCzIVLmFkc2IuYXBpLnYxLkFpcmNyYWZ0UghhaXJjcmFmdBI7CgxpbnB1'
    'dF9zdGF0dXMYBCABKAsyGC5hZHNiLmFwaS52MS5JbnB1dFN0YXR1c1ILaW5wdXRTdGF0dXM=');

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

@$core.Deprecated('Use spectrumRequestDescriptor instead')
const SpectrumRequest$json = {
  '1': 'SpectrumRequest',
};

/// Descriptor for `SpectrumRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List spectrumRequestDescriptor =
    $convert.base64Decode('Cg9TcGVjdHJ1bVJlcXVlc3Q=');

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

@$core.Deprecated('Use captureRequestDescriptor instead')
const CaptureRequest$json = {
  '1': 'CaptureRequest',
  '2': [
    {
      '1': 'point',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.adsb.api.v1.CaptureRequest.Point',
      '10': 'point'
    },
    {'1': 'duration_s', '3': 2, '4': 1, '5': 1, '10': 'durationS'},
    {'1': 'name', '3': 3, '4': 1, '5': 9, '10': 'name'},
  ],
  '4': [CaptureRequest_Point$json],
};

@$core.Deprecated('Use captureRequestDescriptor instead')
const CaptureRequest_Point$json = {
  '1': 'Point',
  '2': [
    {'1': 'POINT_UNSPECIFIED', '2': 0},
    {'1': 'INPUT', '2': 1},
    {'1': 'ENVELOPE', '2': 2},
  ],
};

/// Descriptor for `CaptureRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List captureRequestDescriptor = $convert.base64Decode(
    'Cg5DYXB0dXJlUmVxdWVzdBI3CgVwb2ludBgBIAEoDjIhLmFkc2IuYXBpLnYxLkNhcHR1cmVSZX'
    'F1ZXN0LlBvaW50UgVwb2ludBIdCgpkdXJhdGlvbl9zGAIgASgBUglkdXJhdGlvblMSEgoEbmFt'
    'ZRgDIAEoCVIEbmFtZSI3CgVQb2ludBIVChFQT0lOVF9VTlNQRUNJRklFRBAAEgkKBUlOUFVUEA'
    'ESDAoIRU5WRUxPUEUQAg==');

@$core.Deprecated('Use captureResultDescriptor instead')
const CaptureResult$json = {
  '1': 'CaptureResult',
  '2': [
    {'1': 'data_path', '3': 1, '4': 1, '5': 9, '10': 'dataPath'},
    {'1': 'datatype', '3': 2, '4': 1, '5': 9, '10': 'datatype'},
    {'1': 'rate_hz', '3': 3, '4': 1, '5': 1, '10': 'rateHz'},
    {'1': 'start_index', '3': 4, '4': 1, '5': 4, '10': 'startIndex'},
    {'1': 'samples', '3': 5, '4': 1, '5': 4, '10': 'samples'},
    {'1': 'missing', '3': 6, '4': 1, '5': 4, '10': 'missing'},
  ],
};

/// Descriptor for `CaptureResult`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List captureResultDescriptor = $convert.base64Decode(
    'Cg1DYXB0dXJlUmVzdWx0EhsKCWRhdGFfcGF0aBgBIAEoCVIIZGF0YVBhdGgSGgoIZGF0YXR5cG'
    'UYAiABKAlSCGRhdGF0eXBlEhcKB3JhdGVfaHoYAyABKAFSBnJhdGVIehIfCgtzdGFydF9pbmRl'
    'eBgEIAEoBFIKc3RhcnRJbmRleBIYCgdzYW1wbGVzGAUgASgEUgdzYW1wbGVzEhgKB21pc3Npbm'
    'cYBiABKARSB21pc3Npbmc=');

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

@$core.Deprecated('Use snapshotRequestDescriptor instead')
const SnapshotRequest$json = {
  '1': 'SnapshotRequest',
  '2': [
    {'1': 't', '3': 1, '4': 1, '5': 1, '9': 0, '10': 't', '17': true},
  ],
  '8': [
    {'1': '_t'},
  ],
};

/// Descriptor for `SnapshotRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List snapshotRequestDescriptor = $convert
    .base64Decode('Cg9TbmFwc2hvdFJlcXVlc3QSEQoBdBgBIAEoAUgAUgF0iAEBQgQKAl90');

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

@$core.Deprecated('Use aircraftHistoryRequestDescriptor instead')
const AircraftHistoryRequest$json = {
  '1': 'AircraftHistoryRequest',
  '2': [
    {'1': 'icao', '3': 1, '4': 1, '5': 13, '10': 'icao'},
    {'1': 't', '3': 2, '4': 1, '5': 1, '9': 0, '10': 't', '17': true},
  ],
  '8': [
    {'1': '_t'},
  ],
};

/// Descriptor for `AircraftHistoryRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List aircraftHistoryRequestDescriptor =
    $convert.base64Decode(
        'ChZBaXJjcmFmdEhpc3RvcnlSZXF1ZXN0EhIKBGljYW8YASABKA1SBGljYW8SEQoBdBgCIAEoAU'
        'gAUgF0iAEBQgQKAl90');

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

@$core.Deprecated('Use activityRequestDescriptor instead')
const ActivityRequest$json = {
  '1': 'ActivityRequest',
  '2': [
    {'1': 'from', '3': 1, '4': 1, '5': 1, '9': 0, '10': 'from', '17': true},
  ],
  '8': [
    {'1': '_from'},
  ],
};

/// Descriptor for `ActivityRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List activityRequestDescriptor = $convert.base64Decode(
    'Cg9BY3Rpdml0eVJlcXVlc3QSFwoEZnJvbRgBIAEoAUgAUgRmcm9tiAEBQgcKBV9mcm9t');

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

@$core.Deprecated('Use heatmapRequestDescriptor instead')
const HeatmapRequest$json = {
  '1': 'HeatmapRequest',
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

/// Descriptor for `HeatmapRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List heatmapRequestDescriptor = $convert.base64Decode(
    'Cg5IZWF0bWFwUmVxdWVzdBISCgR3ZXN0GAEgASgBUgR3ZXN0EhQKBXNvdXRoGAIgASgBUgVzb3'
    'V0aBISCgRlYXN0GAMgASgBUgRlYXN0EhQKBW5vcnRoGAQgASgBUgVub3J0aBIUCgV3aWR0aBgF'
    'IAEoDVIFd2lkdGgSFgoGaGVpZ2h0GAYgASgNUgZoZWlnaHQSEwoCdDAYByABKAFIAFICdDCIAQ'
    'ESEwoCdDEYCCABKAFIAVICdDGIAQFCBQoDX3QwQgUKA190MQ==');

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
