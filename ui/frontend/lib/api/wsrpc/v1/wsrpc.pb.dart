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

import 'wsrpc.pbenum.dart';

export 'package:protobuf/protobuf.dart' show GeneratedMessageGenericExtensions;

export 'wsrpc.pbenum.dart';

class ClientFrame extends $pb.GeneratedMessage {
  factory ClientFrame({
    $core.int? id,
    Hello? hello,
    $core.String? method,
    $core.List<$core.int>? request,
    $core.bool? end,
    $core.bool? cancel,
  }) {
    final result = create();
    if (id != null) result.id = id;
    if (hello != null) result.hello = hello;
    if (method != null) result.method = method;
    if (request != null) result.request = request;
    if (end != null) result.end = end;
    if (cancel != null) result.cancel = cancel;
    return result;
  }

  ClientFrame._();

  factory ClientFrame.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ClientFrame.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ClientFrame',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'wsrpc.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'id', fieldType: $pb.PbFieldType.OU3)
    ..aOM<Hello>(2, _omitFieldNames ? '' : 'hello', subBuilder: Hello.create)
    ..aOS(3, _omitFieldNames ? '' : 'method')
    ..a<$core.List<$core.int>>(
        4, _omitFieldNames ? '' : 'request', $pb.PbFieldType.OY)
    ..aOB(5, _omitFieldNames ? '' : 'end')
    ..aOB(6, _omitFieldNames ? '' : 'cancel')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ClientFrame clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ClientFrame copyWith(void Function(ClientFrame) updates) =>
      super.copyWith((message) => updates(message as ClientFrame))
          as ClientFrame;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ClientFrame create() => ClientFrame._();
  @$core.override
  ClientFrame createEmptyInstance() => create();
  static $pb.PbList<ClientFrame> createRepeated() => $pb.PbList<ClientFrame>();
  @$core.pragma('dart2js:noInline')
  static ClientFrame getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ClientFrame>(create);
  static ClientFrame? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get id => $_getIZ(0);
  @$pb.TagNumber(1)
  set id($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasId() => $_has(0);
  @$pb.TagNumber(1)
  void clearId() => $_clearField(1);

  @$pb.TagNumber(2)
  Hello get hello => $_getN(1);
  @$pb.TagNumber(2)
  set hello(Hello value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasHello() => $_has(1);
  @$pb.TagNumber(2)
  void clearHello() => $_clearField(2);
  @$pb.TagNumber(2)
  Hello ensureHello() => $_ensure(1);

  @$pb.TagNumber(3)
  $core.String get method => $_getSZ(2);
  @$pb.TagNumber(3)
  set method($core.String value) => $_setString(2, value);
  @$pb.TagNumber(3)
  $core.bool hasMethod() => $_has(2);
  @$pb.TagNumber(3)
  void clearMethod() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.List<$core.int> get request => $_getN(3);
  @$pb.TagNumber(4)
  set request($core.List<$core.int> value) => $_setBytes(3, value);
  @$pb.TagNumber(4)
  $core.bool hasRequest() => $_has(3);
  @$pb.TagNumber(4)
  void clearRequest() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.bool get end => $_getBF(4);
  @$pb.TagNumber(5)
  set end($core.bool value) => $_setBool(4, value);
  @$pb.TagNumber(5)
  $core.bool hasEnd() => $_has(4);
  @$pb.TagNumber(5)
  void clearEnd() => $_clearField(5);

  @$pb.TagNumber(6)
  $core.bool get cancel => $_getBF(5);
  @$pb.TagNumber(6)
  set cancel($core.bool value) => $_setBool(5, value);
  @$pb.TagNumber(6)
  $core.bool hasCancel() => $_has(5);
  @$pb.TagNumber(6)
  void clearCancel() => $_clearField(6);
}

class ServerFrame extends $pb.GeneratedMessage {
  factory ServerFrame({
    $core.int? id,
    Hello? hello,
    $core.List<$core.int>? reply,
    $core.bool? end,
    Error? error,
  }) {
    final result = create();
    if (id != null) result.id = id;
    if (hello != null) result.hello = hello;
    if (reply != null) result.reply = reply;
    if (end != null) result.end = end;
    if (error != null) result.error = error;
    return result;
  }

  ServerFrame._();

  factory ServerFrame.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ServerFrame.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ServerFrame',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'wsrpc.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'id', fieldType: $pb.PbFieldType.OU3)
    ..aOM<Hello>(2, _omitFieldNames ? '' : 'hello', subBuilder: Hello.create)
    ..a<$core.List<$core.int>>(
        3, _omitFieldNames ? '' : 'reply', $pb.PbFieldType.OY)
    ..aOB(4, _omitFieldNames ? '' : 'end')
    ..aOM<Error>(5, _omitFieldNames ? '' : 'error', subBuilder: Error.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerFrame clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerFrame copyWith(void Function(ServerFrame) updates) =>
      super.copyWith((message) => updates(message as ServerFrame))
          as ServerFrame;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerFrame create() => ServerFrame._();
  @$core.override
  ServerFrame createEmptyInstance() => create();
  static $pb.PbList<ServerFrame> createRepeated() => $pb.PbList<ServerFrame>();
  @$core.pragma('dart2js:noInline')
  static ServerFrame getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ServerFrame>(create);
  static ServerFrame? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get id => $_getIZ(0);
  @$pb.TagNumber(1)
  set id($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasId() => $_has(0);
  @$pb.TagNumber(1)
  void clearId() => $_clearField(1);

  @$pb.TagNumber(2)
  Hello get hello => $_getN(1);
  @$pb.TagNumber(2)
  set hello(Hello value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasHello() => $_has(1);
  @$pb.TagNumber(2)
  void clearHello() => $_clearField(2);
  @$pb.TagNumber(2)
  Hello ensureHello() => $_ensure(1);

  @$pb.TagNumber(3)
  $core.List<$core.int> get reply => $_getN(2);
  @$pb.TagNumber(3)
  set reply($core.List<$core.int> value) => $_setBytes(2, value);
  @$pb.TagNumber(3)
  $core.bool hasReply() => $_has(2);
  @$pb.TagNumber(3)
  void clearReply() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.bool get end => $_getBF(3);
  @$pb.TagNumber(4)
  set end($core.bool value) => $_setBool(3, value);
  @$pb.TagNumber(4)
  $core.bool hasEnd() => $_has(3);
  @$pb.TagNumber(4)
  void clearEnd() => $_clearField(4);

  @$pb.TagNumber(5)
  Error get error => $_getN(4);
  @$pb.TagNumber(5)
  set error(Error value) => $_setField(5, value);
  @$pb.TagNumber(5)
  $core.bool hasError() => $_has(4);
  @$pb.TagNumber(5)
  void clearError() => $_clearField(5);
  @$pb.TagNumber(5)
  Error ensureError() => $_ensure(4);
}

class Hello extends $pb.GeneratedMessage {
  factory Hello({
    $core.Iterable<$core.String>? services,
  }) {
    final result = create();
    if (services != null) result.services.addAll(services);
    return result;
  }

  Hello._();

  factory Hello.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Hello.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Hello',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'wsrpc.v1'),
      createEmptyInstance: create)
    ..pPS(1, _omitFieldNames ? '' : 'services')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Hello clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Hello copyWith(void Function(Hello) updates) =>
      super.copyWith((message) => updates(message as Hello)) as Hello;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Hello create() => Hello._();
  @$core.override
  Hello createEmptyInstance() => create();
  static $pb.PbList<Hello> createRepeated() => $pb.PbList<Hello>();
  @$core.pragma('dart2js:noInline')
  static Hello getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Hello>(create);
  static Hello? _defaultInstance;

  /// Full names, e.g. "adsb.api.v1.Receiver". The client's: those it knows; the server's: those it serves.
  @$pb.TagNumber(1)
  $pb.PbList<$core.String> get services => $_getList(0);
}

class Error extends $pb.GeneratedMessage {
  factory Error({
    Error_Code? code,
    $core.String? message,
  }) {
    final result = create();
    if (code != null) result.code = code;
    if (message != null) result.message = message;
    return result;
  }

  Error._();

  factory Error.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Error.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Error',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'wsrpc.v1'),
      createEmptyInstance: create)
    ..aE<Error_Code>(1, _omitFieldNames ? '' : 'code',
        enumValues: Error_Code.values)
    ..aOS(2, _omitFieldNames ? '' : 'message')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Error clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Error copyWith(void Function(Error) updates) =>
      super.copyWith((message) => updates(message as Error)) as Error;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Error create() => Error._();
  @$core.override
  Error createEmptyInstance() => create();
  static $pb.PbList<Error> createRepeated() => $pb.PbList<Error>();
  @$core.pragma('dart2js:noInline')
  static Error getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Error>(create);
  static Error? _defaultInstance;

  @$pb.TagNumber(1)
  Error_Code get code => $_getN(0);
  @$pb.TagNumber(1)
  set code(Error_Code value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.String get message => $_getSZ(1);
  @$pb.TagNumber(2)
  set message($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasMessage() => $_has(1);
  @$pb.TagNumber(2)
  void clearMessage() => $_clearField(2);
}

const $core.bool _omitFieldNames =
    $core.bool.fromEnvironment('protobuf.omit_field_names');
const $core.bool _omitMessageNames =
    $core.bool.fromEnvironment('protobuf.omit_message_names');
