// This is a generated file - do not edit.
//
// Generated from adsb/api/v1/api.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

class Error_Code extends $pb.ProtobufEnum {
  static const Error_Code CODE_UNSPECIFIED =
      Error_Code._(0, _omitEnumNames ? '' : 'CODE_UNSPECIFIED');
  static const Error_Code UNIMPLEMENTED =
      Error_Code._(1, _omitEnumNames ? '' : 'UNIMPLEMENTED');
  static const Error_Code INVALID_ARGUMENT =
      Error_Code._(2, _omitEnumNames ? '' : 'INVALID_ARGUMENT');
  static const Error_Code NO_HISTORY =
      Error_Code._(3, _omitEnumNames ? '' : 'NO_HISTORY');
  static const Error_Code INTERNAL =
      Error_Code._(4, _omitEnumNames ? '' : 'INTERNAL');
  static const Error_Code CANCELLED =
      Error_Code._(5, _omitEnumNames ? '' : 'CANCELLED');
  static const Error_Code RESOURCE_EXHAUSTED =
      Error_Code._(6, _omitEnumNames ? '' : 'RESOURCE_EXHAUSTED');
  static const Error_Code NO_LIVE_INPUT =
      Error_Code._(7, _omitEnumNames ? '' : 'NO_LIVE_INPUT');
  static const Error_Code NO_CAPTURE =
      Error_Code._(8, _omitEnumNames ? '' : 'NO_CAPTURE');

  static const $core.List<Error_Code> values = <Error_Code>[
    CODE_UNSPECIFIED,
    UNIMPLEMENTED,
    INVALID_ARGUMENT,
    NO_HISTORY,
    INTERNAL,
    CANCELLED,
    RESOURCE_EXHAUSTED,
    NO_LIVE_INPUT,
    NO_CAPTURE,
  ];

  static final $core.List<Error_Code?> _byValue =
      $pb.ProtobufEnum.$_initByValueList(values, 8);
  static Error_Code? valueOf($core.int value) =>
      value < 0 || value >= _byValue.length ? null : _byValue[value];

  const Error_Code._(super.value, super.name);
}

class CaptureRequest_Point extends $pb.ProtobufEnum {
  static const CaptureRequest_Point POINT_UNSPECIFIED =
      CaptureRequest_Point._(0, _omitEnumNames ? '' : 'POINT_UNSPECIFIED');
  static const CaptureRequest_Point INPUT =
      CaptureRequest_Point._(1, _omitEnumNames ? '' : 'INPUT');
  static const CaptureRequest_Point ENVELOPE =
      CaptureRequest_Point._(2, _omitEnumNames ? '' : 'ENVELOPE');

  static const $core.List<CaptureRequest_Point> values = <CaptureRequest_Point>[
    POINT_UNSPECIFIED,
    INPUT,
    ENVELOPE,
  ];

  static final $core.List<CaptureRequest_Point?> _byValue =
      $pb.ProtobufEnum.$_initByValueList(values, 2);
  static CaptureRequest_Point? valueOf($core.int value) =>
      value < 0 || value >= _byValue.length ? null : _byValue[value];

  const CaptureRequest_Point._(super.value, super.name);
}

class Aircraft_AltitudeSource extends $pb.ProtobufEnum {
  static const Aircraft_AltitudeSource ALTITUDE_SOURCE_UNSPECIFIED =
      Aircraft_AltitudeSource._(
          0, _omitEnumNames ? '' : 'ALTITUDE_SOURCE_UNSPECIFIED');
  static const Aircraft_AltitudeSource MCP =
      Aircraft_AltitudeSource._(1, _omitEnumNames ? '' : 'MCP');
  static const Aircraft_AltitudeSource FMS =
      Aircraft_AltitudeSource._(2, _omitEnumNames ? '' : 'FMS');

  static const $core.List<Aircraft_AltitudeSource> values =
      <Aircraft_AltitudeSource>[
    ALTITUDE_SOURCE_UNSPECIFIED,
    MCP,
    FMS,
  ];

  static final $core.List<Aircraft_AltitudeSource?> _byValue =
      $pb.ProtobufEnum.$_initByValueList(values, 2);
  static Aircraft_AltitudeSource? valueOf($core.int value) =>
      value < 0 || value >= _byValue.length ? null : _byValue[value];

  const Aircraft_AltitudeSource._(super.value, super.name);
}

class AutopilotModes_Mode extends $pb.ProtobufEnum {
  static const AutopilotModes_Mode MODE_UNSPECIFIED =
      AutopilotModes_Mode._(0, _omitEnumNames ? '' : 'MODE_UNSPECIFIED');
  static const AutopilotModes_Mode AP =
      AutopilotModes_Mode._(1, _omitEnumNames ? '' : 'AP');
  static const AutopilotModes_Mode VNAV =
      AutopilotModes_Mode._(2, _omitEnumNames ? '' : 'VNAV');
  static const AutopilotModes_Mode ALT =
      AutopilotModes_Mode._(3, _omitEnumNames ? '' : 'ALT');
  static const AutopilotModes_Mode APP =
      AutopilotModes_Mode._(4, _omitEnumNames ? '' : 'APP');
  static const AutopilotModes_Mode LNAV =
      AutopilotModes_Mode._(5, _omitEnumNames ? '' : 'LNAV');

  static const $core.List<AutopilotModes_Mode> values = <AutopilotModes_Mode>[
    MODE_UNSPECIFIED,
    AP,
    VNAV,
    ALT,
    APP,
    LNAV,
  ];

  static final $core.List<AutopilotModes_Mode?> _byValue =
      $pb.ProtobufEnum.$_initByValueList(values, 5);
  static AutopilotModes_Mode? valueOf($core.int value) =>
      value < 0 || value >= _byValue.length ? null : _byValue[value];

  const AutopilotModes_Mode._(super.value, super.name);
}

const $core.bool _omitEnumNames =
    $core.bool.fromEnvironment('protobuf.omit_enum_names');
