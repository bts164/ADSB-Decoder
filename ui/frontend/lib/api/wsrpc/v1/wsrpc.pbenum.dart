// This is a generated file - do not edit.
//
// Generated from wsrpc/v1/wsrpc.proto.

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
  static const Error_Code CANCELLED =
      Error_Code._(1, _omitEnumNames ? '' : 'CANCELLED');
  static const Error_Code INVALID_ARGUMENT =
      Error_Code._(3, _omitEnumNames ? '' : 'INVALID_ARGUMENT');
  static const Error_Code RESOURCE_EXHAUSTED =
      Error_Code._(8, _omitEnumNames ? '' : 'RESOURCE_EXHAUSTED');
  static const Error_Code FAILED_PRECONDITION =
      Error_Code._(9, _omitEnumNames ? '' : 'FAILED_PRECONDITION');
  static const Error_Code UNIMPLEMENTED =
      Error_Code._(12, _omitEnumNames ? '' : 'UNIMPLEMENTED');
  static const Error_Code INTERNAL =
      Error_Code._(13, _omitEnumNames ? '' : 'INTERNAL');

  static const $core.List<Error_Code> values = <Error_Code>[
    CODE_UNSPECIFIED,
    CANCELLED,
    INVALID_ARGUMENT,
    RESOURCE_EXHAUSTED,
    FAILED_PRECONDITION,
    UNIMPLEMENTED,
    INTERNAL,
  ];

  static final $core.Map<$core.int, Error_Code> _byValue =
      $pb.ProtobufEnum.initByValue(values);
  static Error_Code? valueOf($core.int value) => _byValue[value];

  const Error_Code._(super.value, super.name);
}

const $core.bool _omitEnumNames =
    $core.bool.fromEnvironment('protobuf.omit_enum_names');
