// This is a generated file - do not edit.
//
// Generated from wsrpc/v1/wsrpc.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names, unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use clientFrameDescriptor instead')
const ClientFrame$json = {
  '1': 'ClientFrame',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {
      '1': 'hello',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.wsrpc.v1.Hello',
      '10': 'hello'
    },
    {'1': 'method', '3': 3, '4': 1, '5': 9, '10': 'method'},
    {
      '1': 'request',
      '3': 4,
      '4': 1,
      '5': 12,
      '9': 0,
      '10': 'request',
      '17': true
    },
    {'1': 'end', '3': 5, '4': 1, '5': 8, '10': 'end'},
    {'1': 'cancel', '3': 6, '4': 1, '5': 8, '10': 'cancel'},
  ],
  '8': [
    {'1': '_request'},
  ],
};

/// Descriptor for `ClientFrame`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List clientFrameDescriptor = $convert.base64Decode(
    'CgtDbGllbnRGcmFtZRIOCgJpZBgBIAEoDVICaWQSJQoFaGVsbG8YAiABKAsyDy53c3JwYy52MS'
    '5IZWxsb1IFaGVsbG8SFgoGbWV0aG9kGAMgASgJUgZtZXRob2QSHQoHcmVxdWVzdBgEIAEoDEgA'
    'UgdyZXF1ZXN0iAEBEhAKA2VuZBgFIAEoCFIDZW5kEhYKBmNhbmNlbBgGIAEoCFIGY2FuY2VsQg'
    'oKCF9yZXF1ZXN0');

@$core.Deprecated('Use serverFrameDescriptor instead')
const ServerFrame$json = {
  '1': 'ServerFrame',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {
      '1': 'hello',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.wsrpc.v1.Hello',
      '10': 'hello'
    },
    {'1': 'reply', '3': 3, '4': 1, '5': 12, '9': 0, '10': 'reply', '17': true},
    {'1': 'end', '3': 4, '4': 1, '5': 8, '10': 'end'},
    {
      '1': 'error',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.wsrpc.v1.Error',
      '10': 'error'
    },
  ],
  '8': [
    {'1': '_reply'},
  ],
};

/// Descriptor for `ServerFrame`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List serverFrameDescriptor = $convert.base64Decode(
    'CgtTZXJ2ZXJGcmFtZRIOCgJpZBgBIAEoDVICaWQSJQoFaGVsbG8YAiABKAsyDy53c3JwYy52MS'
    '5IZWxsb1IFaGVsbG8SGQoFcmVwbHkYAyABKAxIAFIFcmVwbHmIAQESEAoDZW5kGAQgASgIUgNl'
    'bmQSJQoFZXJyb3IYBSABKAsyDy53c3JwYy52MS5FcnJvclIFZXJyb3JCCAoGX3JlcGx5');

@$core.Deprecated('Use helloDescriptor instead')
const Hello$json = {
  '1': 'Hello',
  '2': [
    {'1': 'services', '3': 1, '4': 3, '5': 9, '10': 'services'},
  ],
};

/// Descriptor for `Hello`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List helloDescriptor =
    $convert.base64Decode('CgVIZWxsbxIaCghzZXJ2aWNlcxgBIAMoCVIIc2VydmljZXM=');

@$core.Deprecated('Use errorDescriptor instead')
const Error$json = {
  '1': 'Error',
  '2': [
    {
      '1': 'code',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.wsrpc.v1.Error.Code',
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
    {'1': 'CANCELLED', '2': 1},
    {'1': 'INVALID_ARGUMENT', '2': 3},
    {'1': 'RESOURCE_EXHAUSTED', '2': 8},
    {'1': 'FAILED_PRECONDITION', '2': 9},
    {'1': 'UNIMPLEMENTED', '2': 12},
    {'1': 'INTERNAL', '2': 13},
  ],
};

/// Descriptor for `Error`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List errorDescriptor = $convert.base64Decode(
    'CgVFcnJvchIoCgRjb2RlGAEgASgOMhQud3NycGMudjEuRXJyb3IuQ29kZVIEY29kZRIYCgdtZX'
    'NzYWdlGAIgASgJUgdtZXNzYWdlIpMBCgRDb2RlEhQKEENPREVfVU5TUEVDSUZJRUQQABINCglD'
    'QU5DRUxMRUQQARIUChBJTlZBTElEX0FSR1VNRU5UEAMSFgoSUkVTT1VSQ0VfRVhIQVVTVEVEEA'
    'gSFwoTRkFJTEVEX1BSRUNPTkRJVElPThAJEhEKDVVOSU1QTEVNRU5URUQQDBIMCghJTlRFUk5B'
    'TBAN');
