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

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'api.pbenum.dart';

export 'package:protobuf/protobuf.dart' show GeneratedMessageGenericExtensions;

export 'api.pbenum.dart';

enum ClientMessage_Call {
  cancel,
  serverInfo,
  subscribe,
  spectrum,
  snapshot,
  aircraftHistory,
  activity,
  heatmap,
  notSet
}

class ClientMessage extends $pb.GeneratedMessage {
  factory ClientMessage({
    $core.int? id,
    Cancel? cancel,
    ServerInfoRequest? serverInfo,
    SubscribeRequest? subscribe,
    SpectrumRequest? spectrum,
    SnapshotRequest? snapshot,
    AircraftHistoryRequest? aircraftHistory,
    ActivityRequest? activity,
    HeatmapRequest? heatmap,
  }) {
    final result = create();
    if (id != null) result.id = id;
    if (cancel != null) result.cancel = cancel;
    if (serverInfo != null) result.serverInfo = serverInfo;
    if (subscribe != null) result.subscribe = subscribe;
    if (spectrum != null) result.spectrum = spectrum;
    if (snapshot != null) result.snapshot = snapshot;
    if (aircraftHistory != null) result.aircraftHistory = aircraftHistory;
    if (activity != null) result.activity = activity;
    if (heatmap != null) result.heatmap = heatmap;
    return result;
  }

  ClientMessage._();

  factory ClientMessage.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ClientMessage.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static const $core.Map<$core.int, ClientMessage_Call>
      _ClientMessage_CallByTag = {
    2: ClientMessage_Call.cancel,
    3: ClientMessage_Call.serverInfo,
    4: ClientMessage_Call.subscribe,
    5: ClientMessage_Call.spectrum,
    10: ClientMessage_Call.snapshot,
    11: ClientMessage_Call.aircraftHistory,
    12: ClientMessage_Call.activity,
    13: ClientMessage_Call.heatmap,
    0: ClientMessage_Call.notSet
  };
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ClientMessage',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..oo(0, [2, 3, 4, 5, 10, 11, 12, 13])
    ..aI(1, _omitFieldNames ? '' : 'id', fieldType: $pb.PbFieldType.OU3)
    ..aOM<Cancel>(2, _omitFieldNames ? '' : 'cancel', subBuilder: Cancel.create)
    ..aOM<ServerInfoRequest>(3, _omitFieldNames ? '' : 'serverInfo',
        subBuilder: ServerInfoRequest.create)
    ..aOM<SubscribeRequest>(4, _omitFieldNames ? '' : 'subscribe',
        subBuilder: SubscribeRequest.create)
    ..aOM<SpectrumRequest>(5, _omitFieldNames ? '' : 'spectrum',
        subBuilder: SpectrumRequest.create)
    ..aOM<SnapshotRequest>(10, _omitFieldNames ? '' : 'snapshot',
        subBuilder: SnapshotRequest.create)
    ..aOM<AircraftHistoryRequest>(11, _omitFieldNames ? '' : 'aircraftHistory',
        subBuilder: AircraftHistoryRequest.create)
    ..aOM<ActivityRequest>(12, _omitFieldNames ? '' : 'activity',
        subBuilder: ActivityRequest.create)
    ..aOM<HeatmapRequest>(13, _omitFieldNames ? '' : 'heatmap',
        subBuilder: HeatmapRequest.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ClientMessage clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ClientMessage copyWith(void Function(ClientMessage) updates) =>
      super.copyWith((message) => updates(message as ClientMessage))
          as ClientMessage;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ClientMessage create() => ClientMessage._();
  @$core.override
  ClientMessage createEmptyInstance() => create();
  static $pb.PbList<ClientMessage> createRepeated() =>
      $pb.PbList<ClientMessage>();
  @$core.pragma('dart2js:noInline')
  static ClientMessage getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ClientMessage>(create);
  static ClientMessage? _defaultInstance;

  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  @$pb.TagNumber(4)
  @$pb.TagNumber(5)
  @$pb.TagNumber(10)
  @$pb.TagNumber(11)
  @$pb.TagNumber(12)
  @$pb.TagNumber(13)
  ClientMessage_Call whichCall() => _ClientMessage_CallByTag[$_whichOneof(0)]!;
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  @$pb.TagNumber(4)
  @$pb.TagNumber(5)
  @$pb.TagNumber(10)
  @$pb.TagNumber(11)
  @$pb.TagNumber(12)
  @$pb.TagNumber(13)
  void clearCall() => $_clearField($_whichOneof(0));

  @$pb.TagNumber(1)
  $core.int get id => $_getIZ(0);
  @$pb.TagNumber(1)
  set id($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasId() => $_has(0);
  @$pb.TagNumber(1)
  void clearId() => $_clearField(1);

  @$pb.TagNumber(2)
  Cancel get cancel => $_getN(1);
  @$pb.TagNumber(2)
  set cancel(Cancel value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasCancel() => $_has(1);
  @$pb.TagNumber(2)
  void clearCancel() => $_clearField(2);
  @$pb.TagNumber(2)
  Cancel ensureCancel() => $_ensure(1);

  @$pb.TagNumber(3)
  ServerInfoRequest get serverInfo => $_getN(2);
  @$pb.TagNumber(3)
  set serverInfo(ServerInfoRequest value) => $_setField(3, value);
  @$pb.TagNumber(3)
  $core.bool hasServerInfo() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerInfo() => $_clearField(3);
  @$pb.TagNumber(3)
  ServerInfoRequest ensureServerInfo() => $_ensure(2);

  @$pb.TagNumber(4)
  SubscribeRequest get subscribe => $_getN(3);
  @$pb.TagNumber(4)
  set subscribe(SubscribeRequest value) => $_setField(4, value);
  @$pb.TagNumber(4)
  $core.bool hasSubscribe() => $_has(3);
  @$pb.TagNumber(4)
  void clearSubscribe() => $_clearField(4);
  @$pb.TagNumber(4)
  SubscribeRequest ensureSubscribe() => $_ensure(3);

  @$pb.TagNumber(5)
  SpectrumRequest get spectrum => $_getN(4);
  @$pb.TagNumber(5)
  set spectrum(SpectrumRequest value) => $_setField(5, value);
  @$pb.TagNumber(5)
  $core.bool hasSpectrum() => $_has(4);
  @$pb.TagNumber(5)
  void clearSpectrum() => $_clearField(5);
  @$pb.TagNumber(5)
  SpectrumRequest ensureSpectrum() => $_ensure(4);

  @$pb.TagNumber(10)
  SnapshotRequest get snapshot => $_getN(5);
  @$pb.TagNumber(10)
  set snapshot(SnapshotRequest value) => $_setField(10, value);
  @$pb.TagNumber(10)
  $core.bool hasSnapshot() => $_has(5);
  @$pb.TagNumber(10)
  void clearSnapshot() => $_clearField(10);
  @$pb.TagNumber(10)
  SnapshotRequest ensureSnapshot() => $_ensure(5);

  @$pb.TagNumber(11)
  AircraftHistoryRequest get aircraftHistory => $_getN(6);
  @$pb.TagNumber(11)
  set aircraftHistory(AircraftHistoryRequest value) => $_setField(11, value);
  @$pb.TagNumber(11)
  $core.bool hasAircraftHistory() => $_has(6);
  @$pb.TagNumber(11)
  void clearAircraftHistory() => $_clearField(11);
  @$pb.TagNumber(11)
  AircraftHistoryRequest ensureAircraftHistory() => $_ensure(6);

  @$pb.TagNumber(12)
  ActivityRequest get activity => $_getN(7);
  @$pb.TagNumber(12)
  set activity(ActivityRequest value) => $_setField(12, value);
  @$pb.TagNumber(12)
  $core.bool hasActivity() => $_has(7);
  @$pb.TagNumber(12)
  void clearActivity() => $_clearField(12);
  @$pb.TagNumber(12)
  ActivityRequest ensureActivity() => $_ensure(7);

  @$pb.TagNumber(13)
  HeatmapRequest get heatmap => $_getN(8);
  @$pb.TagNumber(13)
  set heatmap(HeatmapRequest value) => $_setField(13, value);
  @$pb.TagNumber(13)
  $core.bool hasHeatmap() => $_has(8);
  @$pb.TagNumber(13)
  void clearHeatmap() => $_clearField(13);
  @$pb.TagNumber(13)
  HeatmapRequest ensureHeatmap() => $_ensure(8);
}

enum ServerMessage_Body {
  serverInfo,
  update,
  spectrum,
  error,
  snapshot,
  aircraftHistory,
  activity,
  heatmap,
  notSet
}

class ServerMessage extends $pb.GeneratedMessage {
  factory ServerMessage({
    $core.int? id,
    $core.bool? end,
    ServerInfo? serverInfo,
    Update? update,
    SpectrumRow? spectrum,
    Error? error,
    Snapshot? snapshot,
    AircraftHistory? aircraftHistory,
    Activity? activity,
    Heatmap? heatmap,
  }) {
    final result = create();
    if (id != null) result.id = id;
    if (end != null) result.end = end;
    if (serverInfo != null) result.serverInfo = serverInfo;
    if (update != null) result.update = update;
    if (spectrum != null) result.spectrum = spectrum;
    if (error != null) result.error = error;
    if (snapshot != null) result.snapshot = snapshot;
    if (aircraftHistory != null) result.aircraftHistory = aircraftHistory;
    if (activity != null) result.activity = activity;
    if (heatmap != null) result.heatmap = heatmap;
    return result;
  }

  ServerMessage._();

  factory ServerMessage.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ServerMessage.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static const $core.Map<$core.int, ServerMessage_Body>
      _ServerMessage_BodyByTag = {
    3: ServerMessage_Body.serverInfo,
    4: ServerMessage_Body.update,
    5: ServerMessage_Body.spectrum,
    9: ServerMessage_Body.error,
    10: ServerMessage_Body.snapshot,
    11: ServerMessage_Body.aircraftHistory,
    12: ServerMessage_Body.activity,
    13: ServerMessage_Body.heatmap,
    0: ServerMessage_Body.notSet
  };
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ServerMessage',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..oo(0, [3, 4, 5, 9, 10, 11, 12, 13])
    ..aI(1, _omitFieldNames ? '' : 'id', fieldType: $pb.PbFieldType.OU3)
    ..aOB(2, _omitFieldNames ? '' : 'end')
    ..aOM<ServerInfo>(3, _omitFieldNames ? '' : 'serverInfo',
        subBuilder: ServerInfo.create)
    ..aOM<Update>(4, _omitFieldNames ? '' : 'update', subBuilder: Update.create)
    ..aOM<SpectrumRow>(5, _omitFieldNames ? '' : 'spectrum',
        subBuilder: SpectrumRow.create)
    ..aOM<Error>(9, _omitFieldNames ? '' : 'error', subBuilder: Error.create)
    ..aOM<Snapshot>(10, _omitFieldNames ? '' : 'snapshot',
        subBuilder: Snapshot.create)
    ..aOM<AircraftHistory>(11, _omitFieldNames ? '' : 'aircraftHistory',
        subBuilder: AircraftHistory.create)
    ..aOM<Activity>(12, _omitFieldNames ? '' : 'activity',
        subBuilder: Activity.create)
    ..aOM<Heatmap>(13, _omitFieldNames ? '' : 'heatmap',
        subBuilder: Heatmap.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerMessage clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerMessage copyWith(void Function(ServerMessage) updates) =>
      super.copyWith((message) => updates(message as ServerMessage))
          as ServerMessage;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerMessage create() => ServerMessage._();
  @$core.override
  ServerMessage createEmptyInstance() => create();
  static $pb.PbList<ServerMessage> createRepeated() =>
      $pb.PbList<ServerMessage>();
  @$core.pragma('dart2js:noInline')
  static ServerMessage getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ServerMessage>(create);
  static ServerMessage? _defaultInstance;

  @$pb.TagNumber(3)
  @$pb.TagNumber(4)
  @$pb.TagNumber(5)
  @$pb.TagNumber(9)
  @$pb.TagNumber(10)
  @$pb.TagNumber(11)
  @$pb.TagNumber(12)
  @$pb.TagNumber(13)
  ServerMessage_Body whichBody() => _ServerMessage_BodyByTag[$_whichOneof(0)]!;
  @$pb.TagNumber(3)
  @$pb.TagNumber(4)
  @$pb.TagNumber(5)
  @$pb.TagNumber(9)
  @$pb.TagNumber(10)
  @$pb.TagNumber(11)
  @$pb.TagNumber(12)
  @$pb.TagNumber(13)
  void clearBody() => $_clearField($_whichOneof(0));

  @$pb.TagNumber(1)
  $core.int get id => $_getIZ(0);
  @$pb.TagNumber(1)
  set id($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasId() => $_has(0);
  @$pb.TagNumber(1)
  void clearId() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.bool get end => $_getBF(1);
  @$pb.TagNumber(2)
  set end($core.bool value) => $_setBool(1, value);
  @$pb.TagNumber(2)
  $core.bool hasEnd() => $_has(1);
  @$pb.TagNumber(2)
  void clearEnd() => $_clearField(2);

  @$pb.TagNumber(3)
  ServerInfo get serverInfo => $_getN(2);
  @$pb.TagNumber(3)
  set serverInfo(ServerInfo value) => $_setField(3, value);
  @$pb.TagNumber(3)
  $core.bool hasServerInfo() => $_has(2);
  @$pb.TagNumber(3)
  void clearServerInfo() => $_clearField(3);
  @$pb.TagNumber(3)
  ServerInfo ensureServerInfo() => $_ensure(2);

  @$pb.TagNumber(4)
  Update get update => $_getN(3);
  @$pb.TagNumber(4)
  set update(Update value) => $_setField(4, value);
  @$pb.TagNumber(4)
  $core.bool hasUpdate() => $_has(3);
  @$pb.TagNumber(4)
  void clearUpdate() => $_clearField(4);
  @$pb.TagNumber(4)
  Update ensureUpdate() => $_ensure(3);

  @$pb.TagNumber(5)
  SpectrumRow get spectrum => $_getN(4);
  @$pb.TagNumber(5)
  set spectrum(SpectrumRow value) => $_setField(5, value);
  @$pb.TagNumber(5)
  $core.bool hasSpectrum() => $_has(4);
  @$pb.TagNumber(5)
  void clearSpectrum() => $_clearField(5);
  @$pb.TagNumber(5)
  SpectrumRow ensureSpectrum() => $_ensure(4);

  @$pb.TagNumber(9)
  Error get error => $_getN(5);
  @$pb.TagNumber(9)
  set error(Error value) => $_setField(9, value);
  @$pb.TagNumber(9)
  $core.bool hasError() => $_has(5);
  @$pb.TagNumber(9)
  void clearError() => $_clearField(9);
  @$pb.TagNumber(9)
  Error ensureError() => $_ensure(5);

  @$pb.TagNumber(10)
  Snapshot get snapshot => $_getN(6);
  @$pb.TagNumber(10)
  set snapshot(Snapshot value) => $_setField(10, value);
  @$pb.TagNumber(10)
  $core.bool hasSnapshot() => $_has(6);
  @$pb.TagNumber(10)
  void clearSnapshot() => $_clearField(10);
  @$pb.TagNumber(10)
  Snapshot ensureSnapshot() => $_ensure(6);

  @$pb.TagNumber(11)
  AircraftHistory get aircraftHistory => $_getN(7);
  @$pb.TagNumber(11)
  set aircraftHistory(AircraftHistory value) => $_setField(11, value);
  @$pb.TagNumber(11)
  $core.bool hasAircraftHistory() => $_has(7);
  @$pb.TagNumber(11)
  void clearAircraftHistory() => $_clearField(11);
  @$pb.TagNumber(11)
  AircraftHistory ensureAircraftHistory() => $_ensure(7);

  @$pb.TagNumber(12)
  Activity get activity => $_getN(8);
  @$pb.TagNumber(12)
  set activity(Activity value) => $_setField(12, value);
  @$pb.TagNumber(12)
  $core.bool hasActivity() => $_has(8);
  @$pb.TagNumber(12)
  void clearActivity() => $_clearField(12);
  @$pb.TagNumber(12)
  Activity ensureActivity() => $_ensure(8);

  @$pb.TagNumber(13)
  Heatmap get heatmap => $_getN(9);
  @$pb.TagNumber(13)
  set heatmap(Heatmap value) => $_setField(13, value);
  @$pb.TagNumber(13)
  $core.bool hasHeatmap() => $_has(9);
  @$pb.TagNumber(13)
  void clearHeatmap() => $_clearField(13);
  @$pb.TagNumber(13)
  Heatmap ensureHeatmap() => $_ensure(9);
}

class Cancel extends $pb.GeneratedMessage {
  factory Cancel() => create();

  Cancel._();

  factory Cancel.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Cancel.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Cancel',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Cancel clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Cancel copyWith(void Function(Cancel) updates) =>
      super.copyWith((message) => updates(message as Cancel)) as Cancel;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Cancel create() => Cancel._();
  @$core.override
  Cancel createEmptyInstance() => create();
  static $pb.PbList<Cancel> createRepeated() => $pb.PbList<Cancel>();
  @$core.pragma('dart2js:noInline')
  static Cancel getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Cancel>(create);
  static Cancel? _defaultInstance;
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
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
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

class ServerInfoRequest extends $pb.GeneratedMessage {
  factory ServerInfoRequest() => create();

  ServerInfoRequest._();

  factory ServerInfoRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ServerInfoRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ServerInfoRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerInfoRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerInfoRequest copyWith(void Function(ServerInfoRequest) updates) =>
      super.copyWith((message) => updates(message as ServerInfoRequest))
          as ServerInfoRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerInfoRequest create() => ServerInfoRequest._();
  @$core.override
  ServerInfoRequest createEmptyInstance() => create();
  static $pb.PbList<ServerInfoRequest> createRepeated() =>
      $pb.PbList<ServerInfoRequest>();
  @$core.pragma('dart2js:noInline')
  static ServerInfoRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ServerInfoRequest>(create);
  static ServerInfoRequest? _defaultInstance;
}

class ServerInfo extends $pb.GeneratedMessage {
  factory ServerInfo({
    $core.bool? history,
    $core.bool? spectrum,
    $core.double? defaultIntervalS,
  }) {
    final result = create();
    if (history != null) result.history = history;
    if (spectrum != null) result.spectrum = spectrum;
    if (defaultIntervalS != null) result.defaultIntervalS = defaultIntervalS;
    return result;
  }

  ServerInfo._();

  factory ServerInfo.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ServerInfo.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ServerInfo',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOB(1, _omitFieldNames ? '' : 'history')
    ..aOB(2, _omitFieldNames ? '' : 'spectrum')
    ..aD(3, _omitFieldNames ? '' : 'defaultIntervalS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerInfo clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ServerInfo copyWith(void Function(ServerInfo) updates) =>
      super.copyWith((message) => updates(message as ServerInfo)) as ServerInfo;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ServerInfo create() => ServerInfo._();
  @$core.override
  ServerInfo createEmptyInstance() => create();
  static $pb.PbList<ServerInfo> createRepeated() => $pb.PbList<ServerInfo>();
  @$core.pragma('dart2js:noInline')
  static ServerInfo getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ServerInfo>(create);
  static ServerInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.bool get history => $_getBF(0);
  @$pb.TagNumber(1)
  set history($core.bool value) => $_setBool(0, value);
  @$pb.TagNumber(1)
  $core.bool hasHistory() => $_has(0);
  @$pb.TagNumber(1)
  void clearHistory() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.bool get spectrum => $_getBF(1);
  @$pb.TagNumber(2)
  set spectrum($core.bool value) => $_setBool(1, value);
  @$pb.TagNumber(2)
  $core.bool hasSpectrum() => $_has(1);
  @$pb.TagNumber(2)
  void clearSpectrum() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get defaultIntervalS => $_getN(2);
  @$pb.TagNumber(3)
  set defaultIntervalS($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasDefaultIntervalS() => $_has(2);
  @$pb.TagNumber(3)
  void clearDefaultIntervalS() => $_clearField(3);
}

class SubscribeRequest extends $pb.GeneratedMessage {
  factory SubscribeRequest({
    $core.double? intervalS,
  }) {
    final result = create();
    if (intervalS != null) result.intervalS = intervalS;
    return result;
  }

  SubscribeRequest._();

  factory SubscribeRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory SubscribeRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'SubscribeRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'intervalS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SubscribeRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SubscribeRequest copyWith(void Function(SubscribeRequest) updates) =>
      super.copyWith((message) => updates(message as SubscribeRequest))
          as SubscribeRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SubscribeRequest create() => SubscribeRequest._();
  @$core.override
  SubscribeRequest createEmptyInstance() => create();
  static $pb.PbList<SubscribeRequest> createRepeated() =>
      $pb.PbList<SubscribeRequest>();
  @$core.pragma('dart2js:noInline')
  static SubscribeRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<SubscribeRequest>(create);
  static SubscribeRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get intervalS => $_getN(0);
  @$pb.TagNumber(1)
  set intervalS($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIntervalS() => $_has(0);
  @$pb.TagNumber(1)
  void clearIntervalS() => $_clearField(1);
}

/// One reply of a subscription: what was published since its last update. Frames are in the order they were
/// published; aircraft are those that changed since the last update, each as last published. The first update
/// has every aircraft heard in the last 600 s.
class Update extends $pb.GeneratedMessage {
  factory Update({
    $core.Iterable<Frame>? frames,
    $core.Iterable<Aircraft>? aircraft,
    InputStatus? inputStatus,
  }) {
    final result = create();
    if (frames != null) result.frames.addAll(frames);
    if (aircraft != null) result.aircraft.addAll(aircraft);
    if (inputStatus != null) result.inputStatus = inputStatus;
    return result;
  }

  Update._();

  factory Update.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Update.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Update',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..pPM<Frame>(1, _omitFieldNames ? '' : 'frames', subBuilder: Frame.create)
    ..pPM<Aircraft>(3, _omitFieldNames ? '' : 'aircraft',
        subBuilder: Aircraft.create)
    ..aOM<InputStatus>(4, _omitFieldNames ? '' : 'inputStatus',
        subBuilder: InputStatus.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Update clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Update copyWith(void Function(Update) updates) =>
      super.copyWith((message) => updates(message as Update)) as Update;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Update create() => Update._();
  @$core.override
  Update createEmptyInstance() => create();
  static $pb.PbList<Update> createRepeated() => $pb.PbList<Update>();
  @$core.pragma('dart2js:noInline')
  static Update getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Update>(create);
  static Update? _defaultInstance;

  @$pb.TagNumber(1)
  $pb.PbList<Frame> get frames => $_getList(0);

  @$pb.TagNumber(3)
  $pb.PbList<Aircraft> get aircraft => $_getList(1);

  @$pb.TagNumber(4)
  InputStatus get inputStatus => $_getN(2);
  @$pb.TagNumber(4)
  set inputStatus(InputStatus value) => $_setField(4, value);
  @$pb.TagNumber(4)
  $core.bool hasInputStatus() => $_has(2);
  @$pb.TagNumber(4)
  void clearInputStatus() => $_clearField(4);
  @$pb.TagNumber(4)
  InputStatus ensureInputStatus() => $_ensure(2);
}

class Frame extends $pb.GeneratedMessage {
  factory Frame({
    $fixnum.Int64? idx,
    $core.int? bits,
    $core.double? confidence,
    $core.int? df,
    $core.int? icao,
    $core.bool? crcOk,
    $core.bool? crcChecked,
    $core.int? crcFixedBit,
    $core.List<$core.int>? payload,
    $core.String? summary,
  }) {
    final result = create();
    if (idx != null) result.idx = idx;
    if (bits != null) result.bits = bits;
    if (confidence != null) result.confidence = confidence;
    if (df != null) result.df = df;
    if (icao != null) result.icao = icao;
    if (crcOk != null) result.crcOk = crcOk;
    if (crcChecked != null) result.crcChecked = crcChecked;
    if (crcFixedBit != null) result.crcFixedBit = crcFixedBit;
    if (payload != null) result.payload = payload;
    if (summary != null) result.summary = summary;
    return result;
  }

  Frame._();

  factory Frame.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Frame.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Frame',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..a<$fixnum.Int64>(1, _omitFieldNames ? '' : 'idx', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..aI(2, _omitFieldNames ? '' : 'bits', fieldType: $pb.PbFieldType.OU3)
    ..aD(3, _omitFieldNames ? '' : 'confidence', fieldType: $pb.PbFieldType.OF)
    ..aI(4, _omitFieldNames ? '' : 'df', fieldType: $pb.PbFieldType.OU3)
    ..aI(5, _omitFieldNames ? '' : 'icao', fieldType: $pb.PbFieldType.OU3)
    ..aOB(6, _omitFieldNames ? '' : 'crcOk')
    ..aOB(7, _omitFieldNames ? '' : 'crcChecked')
    ..aI(8, _omitFieldNames ? '' : 'crcFixedBit',
        fieldType: $pb.PbFieldType.OU3)
    ..a<$core.List<$core.int>>(
        9, _omitFieldNames ? '' : 'payload', $pb.PbFieldType.OY)
    ..aOS(10, _omitFieldNames ? '' : 'summary')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Frame clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Frame copyWith(void Function(Frame) updates) =>
      super.copyWith((message) => updates(message as Frame)) as Frame;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Frame create() => Frame._();
  @$core.override
  Frame createEmptyInstance() => create();
  static $pb.PbList<Frame> createRepeated() => $pb.PbList<Frame>();
  @$core.pragma('dart2js:noInline')
  static Frame getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Frame>(create);
  static Frame? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get idx => $_getI64(0);
  @$pb.TagNumber(1)
  set idx($fixnum.Int64 value) => $_setInt64(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIdx() => $_has(0);
  @$pb.TagNumber(1)
  void clearIdx() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.int get bits => $_getIZ(1);
  @$pb.TagNumber(2)
  set bits($core.int value) => $_setUnsignedInt32(1, value);
  @$pb.TagNumber(2)
  $core.bool hasBits() => $_has(1);
  @$pb.TagNumber(2)
  void clearBits() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get confidence => $_getN(2);
  @$pb.TagNumber(3)
  set confidence($core.double value) => $_setFloat(2, value);
  @$pb.TagNumber(3)
  $core.bool hasConfidence() => $_has(2);
  @$pb.TagNumber(3)
  void clearConfidence() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.int get df => $_getIZ(3);
  @$pb.TagNumber(4)
  set df($core.int value) => $_setUnsignedInt32(3, value);
  @$pb.TagNumber(4)
  $core.bool hasDf() => $_has(3);
  @$pb.TagNumber(4)
  void clearDf() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.int get icao => $_getIZ(4);
  @$pb.TagNumber(5)
  set icao($core.int value) => $_setUnsignedInt32(4, value);
  @$pb.TagNumber(5)
  $core.bool hasIcao() => $_has(4);
  @$pb.TagNumber(5)
  void clearIcao() => $_clearField(5);

  @$pb.TagNumber(6)
  $core.bool get crcOk => $_getBF(5);
  @$pb.TagNumber(6)
  set crcOk($core.bool value) => $_setBool(5, value);
  @$pb.TagNumber(6)
  $core.bool hasCrcOk() => $_has(5);
  @$pb.TagNumber(6)
  void clearCrcOk() => $_clearField(6);

  @$pb.TagNumber(7)
  $core.bool get crcChecked => $_getBF(6);
  @$pb.TagNumber(7)
  set crcChecked($core.bool value) => $_setBool(6, value);
  @$pb.TagNumber(7)
  $core.bool hasCrcChecked() => $_has(6);
  @$pb.TagNumber(7)
  void clearCrcChecked() => $_clearField(7);

  @$pb.TagNumber(8)
  $core.int get crcFixedBit => $_getIZ(7);
  @$pb.TagNumber(8)
  set crcFixedBit($core.int value) => $_setUnsignedInt32(7, value);
  @$pb.TagNumber(8)
  $core.bool hasCrcFixedBit() => $_has(7);
  @$pb.TagNumber(8)
  void clearCrcFixedBit() => $_clearField(8);

  @$pb.TagNumber(9)
  $core.List<$core.int> get payload => $_getN(8);
  @$pb.TagNumber(9)
  set payload($core.List<$core.int> value) => $_setBytes(8, value);
  @$pb.TagNumber(9)
  $core.bool hasPayload() => $_has(8);
  @$pb.TagNumber(9)
  void clearPayload() => $_clearField(9);

  @$pb.TagNumber(10)
  $core.String get summary => $_getSZ(9);
  @$pb.TagNumber(10)
  set summary($core.String value) => $_setString(9, value);
  @$pb.TagNumber(10)
  $core.bool hasSummary() => $_has(9);
  @$pb.TagNumber(10)
  void clearSummary() => $_clearField(10);
}

/// The spectrum's rows, one reply each, as they're computed. The server computes them only while a spectrum
/// call is in progress.
class SpectrumRequest extends $pb.GeneratedMessage {
  factory SpectrumRequest() => create();

  SpectrumRequest._();

  factory SpectrumRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory SpectrumRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'SpectrumRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SpectrumRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SpectrumRequest copyWith(void Function(SpectrumRequest) updates) =>
      super.copyWith((message) => updates(message as SpectrumRequest))
          as SpectrumRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SpectrumRequest create() => SpectrumRequest._();
  @$core.override
  SpectrumRequest createEmptyInstance() => create();
  static $pb.PbList<SpectrumRequest> createRepeated() =>
      $pb.PbList<SpectrumRequest>();
  @$core.pragma('dart2js:noInline')
  static SpectrumRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<SpectrumRequest>(create);
  static SpectrumRequest? _defaultInstance;
}

class SpectrumRow extends $pb.GeneratedMessage {
  factory SpectrumRow({
    $core.double? freqHz,
    $core.double? rateHz,
    $core.Iterable<$core.double>? binsDb,
  }) {
    final result = create();
    if (freqHz != null) result.freqHz = freqHz;
    if (rateHz != null) result.rateHz = rateHz;
    if (binsDb != null) result.binsDb.addAll(binsDb);
    return result;
  }

  SpectrumRow._();

  factory SpectrumRow.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory SpectrumRow.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'SpectrumRow',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'freqHz')
    ..aD(2, _omitFieldNames ? '' : 'rateHz')
    ..p<$core.double>(3, _omitFieldNames ? '' : 'binsDb', $pb.PbFieldType.KF)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SpectrumRow clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SpectrumRow copyWith(void Function(SpectrumRow) updates) =>
      super.copyWith((message) => updates(message as SpectrumRow))
          as SpectrumRow;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SpectrumRow create() => SpectrumRow._();
  @$core.override
  SpectrumRow createEmptyInstance() => create();
  static $pb.PbList<SpectrumRow> createRepeated() => $pb.PbList<SpectrumRow>();
  @$core.pragma('dart2js:noInline')
  static SpectrumRow getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<SpectrumRow>(create);
  static SpectrumRow? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get freqHz => $_getN(0);
  @$pb.TagNumber(1)
  set freqHz($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasFreqHz() => $_has(0);
  @$pb.TagNumber(1)
  void clearFreqHz() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get rateHz => $_getN(1);
  @$pb.TagNumber(2)
  set rateHz($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasRateHz() => $_has(1);
  @$pb.TagNumber(2)
  void clearRateHz() => $_clearField(2);

  @$pb.TagNumber(3)
  $pb.PbList<$core.double> get binsDb => $_getList(2);
}

class InputStatus extends $pb.GeneratedMessage {
  factory InputStatus({
    $core.double? windowS,
    $core.double? rateHz,
    $core.double? streamS,
    $core.double? lostS,
    $core.double? droppedS,
    $fixnum.Int64? kernelDrops,
    $core.bool? ok,
    $core.String? summary,
  }) {
    final result = create();
    if (windowS != null) result.windowS = windowS;
    if (rateHz != null) result.rateHz = rateHz;
    if (streamS != null) result.streamS = streamS;
    if (lostS != null) result.lostS = lostS;
    if (droppedS != null) result.droppedS = droppedS;
    if (kernelDrops != null) result.kernelDrops = kernelDrops;
    if (ok != null) result.ok = ok;
    if (summary != null) result.summary = summary;
    return result;
  }

  InputStatus._();

  factory InputStatus.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory InputStatus.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'InputStatus',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'windowS')
    ..aD(2, _omitFieldNames ? '' : 'rateHz')
    ..aD(3, _omitFieldNames ? '' : 'streamS')
    ..aD(4, _omitFieldNames ? '' : 'lostS')
    ..aD(5, _omitFieldNames ? '' : 'droppedS')
    ..a<$fixnum.Int64>(
        6, _omitFieldNames ? '' : 'kernelDrops', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..aOB(7, _omitFieldNames ? '' : 'ok')
    ..aOS(8, _omitFieldNames ? '' : 'summary')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  InputStatus clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  InputStatus copyWith(void Function(InputStatus) updates) =>
      super.copyWith((message) => updates(message as InputStatus))
          as InputStatus;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static InputStatus create() => InputStatus._();
  @$core.override
  InputStatus createEmptyInstance() => create();
  static $pb.PbList<InputStatus> createRepeated() => $pb.PbList<InputStatus>();
  @$core.pragma('dart2js:noInline')
  static InputStatus getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<InputStatus>(create);
  static InputStatus? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get windowS => $_getN(0);
  @$pb.TagNumber(1)
  set windowS($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasWindowS() => $_has(0);
  @$pb.TagNumber(1)
  void clearWindowS() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get rateHz => $_getN(1);
  @$pb.TagNumber(2)
  set rateHz($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasRateHz() => $_has(1);
  @$pb.TagNumber(2)
  void clearRateHz() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get streamS => $_getN(2);
  @$pb.TagNumber(3)
  set streamS($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasStreamS() => $_has(2);
  @$pb.TagNumber(3)
  void clearStreamS() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.double get lostS => $_getN(3);
  @$pb.TagNumber(4)
  set lostS($core.double value) => $_setDouble(3, value);
  @$pb.TagNumber(4)
  $core.bool hasLostS() => $_has(3);
  @$pb.TagNumber(4)
  void clearLostS() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.double get droppedS => $_getN(4);
  @$pb.TagNumber(5)
  set droppedS($core.double value) => $_setDouble(4, value);
  @$pb.TagNumber(5)
  $core.bool hasDroppedS() => $_has(4);
  @$pb.TagNumber(5)
  void clearDroppedS() => $_clearField(5);

  @$pb.TagNumber(6)
  $fixnum.Int64 get kernelDrops => $_getI64(5);
  @$pb.TagNumber(6)
  set kernelDrops($fixnum.Int64 value) => $_setInt64(5, value);
  @$pb.TagNumber(6)
  $core.bool hasKernelDrops() => $_has(5);
  @$pb.TagNumber(6)
  void clearKernelDrops() => $_clearField(6);

  @$pb.TagNumber(7)
  $core.bool get ok => $_getBF(6);
  @$pb.TagNumber(7)
  set ok($core.bool value) => $_setBool(6, value);
  @$pb.TagNumber(7)
  $core.bool hasOk() => $_has(6);
  @$pb.TagNumber(7)
  void clearOk() => $_clearField(7);

  @$pb.TagNumber(8)
  $core.String get summary => $_getSZ(7);
  @$pb.TagNumber(8)
  set summary($core.String value) => $_setString(7, value);
  @$pb.TagNumber(8)
  $core.bool hasSummary() => $_has(7);
  @$pb.TagNumber(8)
  void clearSummary() => $_clearField(8);
}

class Aircraft extends $pb.GeneratedMessage {
  factory Aircraft({
    $core.int? icao,
    $core.String? callsign,
    $core.String? category,
    $core.String? squawk,
    $core.int? altitudeFt,
    $core.double? lat,
    $core.double? lon,
    $core.double? groundSpeedKt,
    $core.double? trackDeg,
    $core.int? verticalRateFpm,
    $core.double? headingDeg,
    $core.int? iasKt,
    $core.int? tasKt,
    $core.double? mach,
    $core.double? rollDeg,
    $core.bool? onGround,
    $core.bool? alert,
    $core.bool? spi,
    $core.int? selectedAltitudeFt,
    Aircraft_AltitudeSource? selectedAltitudeSource,
    $core.double? selectedHeadingDeg,
    $core.double? baroSettingHpa,
    AutopilotModes? autopilotModes,
    $core.int? adsbVersion,
    $core.int? nacP,
    $core.int? sil,
    $fixnum.Int64? messages,
    $core.double? signalDb,
    $core.double? lastSeenS,
    $core.double? lastSeenUnixS,
  }) {
    final result = create();
    if (icao != null) result.icao = icao;
    if (callsign != null) result.callsign = callsign;
    if (category != null) result.category = category;
    if (squawk != null) result.squawk = squawk;
    if (altitudeFt != null) result.altitudeFt = altitudeFt;
    if (lat != null) result.lat = lat;
    if (lon != null) result.lon = lon;
    if (groundSpeedKt != null) result.groundSpeedKt = groundSpeedKt;
    if (trackDeg != null) result.trackDeg = trackDeg;
    if (verticalRateFpm != null) result.verticalRateFpm = verticalRateFpm;
    if (headingDeg != null) result.headingDeg = headingDeg;
    if (iasKt != null) result.iasKt = iasKt;
    if (tasKt != null) result.tasKt = tasKt;
    if (mach != null) result.mach = mach;
    if (rollDeg != null) result.rollDeg = rollDeg;
    if (onGround != null) result.onGround = onGround;
    if (alert != null) result.alert = alert;
    if (spi != null) result.spi = spi;
    if (selectedAltitudeFt != null)
      result.selectedAltitudeFt = selectedAltitudeFt;
    if (selectedAltitudeSource != null)
      result.selectedAltitudeSource = selectedAltitudeSource;
    if (selectedHeadingDeg != null)
      result.selectedHeadingDeg = selectedHeadingDeg;
    if (baroSettingHpa != null) result.baroSettingHpa = baroSettingHpa;
    if (autopilotModes != null) result.autopilotModes = autopilotModes;
    if (adsbVersion != null) result.adsbVersion = adsbVersion;
    if (nacP != null) result.nacP = nacP;
    if (sil != null) result.sil = sil;
    if (messages != null) result.messages = messages;
    if (signalDb != null) result.signalDb = signalDb;
    if (lastSeenS != null) result.lastSeenS = lastSeenS;
    if (lastSeenUnixS != null) result.lastSeenUnixS = lastSeenUnixS;
    return result;
  }

  Aircraft._();

  factory Aircraft.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Aircraft.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Aircraft',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'icao', fieldType: $pb.PbFieldType.OU3)
    ..aOS(2, _omitFieldNames ? '' : 'callsign')
    ..aOS(3, _omitFieldNames ? '' : 'category')
    ..aOS(4, _omitFieldNames ? '' : 'squawk')
    ..aI(5, _omitFieldNames ? '' : 'altitudeFt')
    ..aD(6, _omitFieldNames ? '' : 'lat')
    ..aD(7, _omitFieldNames ? '' : 'lon')
    ..aD(8, _omitFieldNames ? '' : 'groundSpeedKt')
    ..aD(9, _omitFieldNames ? '' : 'trackDeg')
    ..aI(10, _omitFieldNames ? '' : 'verticalRateFpm')
    ..aD(11, _omitFieldNames ? '' : 'headingDeg')
    ..aI(12, _omitFieldNames ? '' : 'iasKt')
    ..aI(13, _omitFieldNames ? '' : 'tasKt')
    ..aD(14, _omitFieldNames ? '' : 'mach')
    ..aD(15, _omitFieldNames ? '' : 'rollDeg')
    ..aOB(16, _omitFieldNames ? '' : 'onGround')
    ..aOB(17, _omitFieldNames ? '' : 'alert')
    ..aOB(18, _omitFieldNames ? '' : 'spi')
    ..aI(19, _omitFieldNames ? '' : 'selectedAltitudeFt')
    ..aE<Aircraft_AltitudeSource>(
        20, _omitFieldNames ? '' : 'selectedAltitudeSource',
        enumValues: Aircraft_AltitudeSource.values)
    ..aD(21, _omitFieldNames ? '' : 'selectedHeadingDeg')
    ..aD(22, _omitFieldNames ? '' : 'baroSettingHpa')
    ..aOM<AutopilotModes>(23, _omitFieldNames ? '' : 'autopilotModes',
        subBuilder: AutopilotModes.create)
    ..aI(24, _omitFieldNames ? '' : 'adsbVersion')
    ..aI(25, _omitFieldNames ? '' : 'nacP')
    ..aI(26, _omitFieldNames ? '' : 'sil')
    ..a<$fixnum.Int64>(
        27, _omitFieldNames ? '' : 'messages', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..aD(28, _omitFieldNames ? '' : 'signalDb')
    ..aD(29, _omitFieldNames ? '' : 'lastSeenS')
    ..aD(30, _omitFieldNames ? '' : 'lastSeenUnixS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Aircraft clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Aircraft copyWith(void Function(Aircraft) updates) =>
      super.copyWith((message) => updates(message as Aircraft)) as Aircraft;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Aircraft create() => Aircraft._();
  @$core.override
  Aircraft createEmptyInstance() => create();
  static $pb.PbList<Aircraft> createRepeated() => $pb.PbList<Aircraft>();
  @$core.pragma('dart2js:noInline')
  static Aircraft getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Aircraft>(create);
  static Aircraft? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get icao => $_getIZ(0);
  @$pb.TagNumber(1)
  set icao($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIcao() => $_has(0);
  @$pb.TagNumber(1)
  void clearIcao() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.String get callsign => $_getSZ(1);
  @$pb.TagNumber(2)
  set callsign($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasCallsign() => $_has(1);
  @$pb.TagNumber(2)
  void clearCallsign() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.String get category => $_getSZ(2);
  @$pb.TagNumber(3)
  set category($core.String value) => $_setString(2, value);
  @$pb.TagNumber(3)
  $core.bool hasCategory() => $_has(2);
  @$pb.TagNumber(3)
  void clearCategory() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.String get squawk => $_getSZ(3);
  @$pb.TagNumber(4)
  set squawk($core.String value) => $_setString(3, value);
  @$pb.TagNumber(4)
  $core.bool hasSquawk() => $_has(3);
  @$pb.TagNumber(4)
  void clearSquawk() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.int get altitudeFt => $_getIZ(4);
  @$pb.TagNumber(5)
  set altitudeFt($core.int value) => $_setSignedInt32(4, value);
  @$pb.TagNumber(5)
  $core.bool hasAltitudeFt() => $_has(4);
  @$pb.TagNumber(5)
  void clearAltitudeFt() => $_clearField(5);

  @$pb.TagNumber(6)
  $core.double get lat => $_getN(5);
  @$pb.TagNumber(6)
  set lat($core.double value) => $_setDouble(5, value);
  @$pb.TagNumber(6)
  $core.bool hasLat() => $_has(5);
  @$pb.TagNumber(6)
  void clearLat() => $_clearField(6);

  @$pb.TagNumber(7)
  $core.double get lon => $_getN(6);
  @$pb.TagNumber(7)
  set lon($core.double value) => $_setDouble(6, value);
  @$pb.TagNumber(7)
  $core.bool hasLon() => $_has(6);
  @$pb.TagNumber(7)
  void clearLon() => $_clearField(7);

  @$pb.TagNumber(8)
  $core.double get groundSpeedKt => $_getN(7);
  @$pb.TagNumber(8)
  set groundSpeedKt($core.double value) => $_setDouble(7, value);
  @$pb.TagNumber(8)
  $core.bool hasGroundSpeedKt() => $_has(7);
  @$pb.TagNumber(8)
  void clearGroundSpeedKt() => $_clearField(8);

  @$pb.TagNumber(9)
  $core.double get trackDeg => $_getN(8);
  @$pb.TagNumber(9)
  set trackDeg($core.double value) => $_setDouble(8, value);
  @$pb.TagNumber(9)
  $core.bool hasTrackDeg() => $_has(8);
  @$pb.TagNumber(9)
  void clearTrackDeg() => $_clearField(9);

  @$pb.TagNumber(10)
  $core.int get verticalRateFpm => $_getIZ(9);
  @$pb.TagNumber(10)
  set verticalRateFpm($core.int value) => $_setSignedInt32(9, value);
  @$pb.TagNumber(10)
  $core.bool hasVerticalRateFpm() => $_has(9);
  @$pb.TagNumber(10)
  void clearVerticalRateFpm() => $_clearField(10);

  @$pb.TagNumber(11)
  $core.double get headingDeg => $_getN(10);
  @$pb.TagNumber(11)
  set headingDeg($core.double value) => $_setDouble(10, value);
  @$pb.TagNumber(11)
  $core.bool hasHeadingDeg() => $_has(10);
  @$pb.TagNumber(11)
  void clearHeadingDeg() => $_clearField(11);

  @$pb.TagNumber(12)
  $core.int get iasKt => $_getIZ(11);
  @$pb.TagNumber(12)
  set iasKt($core.int value) => $_setSignedInt32(11, value);
  @$pb.TagNumber(12)
  $core.bool hasIasKt() => $_has(11);
  @$pb.TagNumber(12)
  void clearIasKt() => $_clearField(12);

  @$pb.TagNumber(13)
  $core.int get tasKt => $_getIZ(12);
  @$pb.TagNumber(13)
  set tasKt($core.int value) => $_setSignedInt32(12, value);
  @$pb.TagNumber(13)
  $core.bool hasTasKt() => $_has(12);
  @$pb.TagNumber(13)
  void clearTasKt() => $_clearField(13);

  @$pb.TagNumber(14)
  $core.double get mach => $_getN(13);
  @$pb.TagNumber(14)
  set mach($core.double value) => $_setDouble(13, value);
  @$pb.TagNumber(14)
  $core.bool hasMach() => $_has(13);
  @$pb.TagNumber(14)
  void clearMach() => $_clearField(14);

  @$pb.TagNumber(15)
  $core.double get rollDeg => $_getN(14);
  @$pb.TagNumber(15)
  set rollDeg($core.double value) => $_setDouble(14, value);
  @$pb.TagNumber(15)
  $core.bool hasRollDeg() => $_has(14);
  @$pb.TagNumber(15)
  void clearRollDeg() => $_clearField(15);

  @$pb.TagNumber(16)
  $core.bool get onGround => $_getBF(15);
  @$pb.TagNumber(16)
  set onGround($core.bool value) => $_setBool(15, value);
  @$pb.TagNumber(16)
  $core.bool hasOnGround() => $_has(15);
  @$pb.TagNumber(16)
  void clearOnGround() => $_clearField(16);

  @$pb.TagNumber(17)
  $core.bool get alert => $_getBF(16);
  @$pb.TagNumber(17)
  set alert($core.bool value) => $_setBool(16, value);
  @$pb.TagNumber(17)
  $core.bool hasAlert() => $_has(16);
  @$pb.TagNumber(17)
  void clearAlert() => $_clearField(17);

  @$pb.TagNumber(18)
  $core.bool get spi => $_getBF(17);
  @$pb.TagNumber(18)
  set spi($core.bool value) => $_setBool(17, value);
  @$pb.TagNumber(18)
  $core.bool hasSpi() => $_has(17);
  @$pb.TagNumber(18)
  void clearSpi() => $_clearField(18);

  @$pb.TagNumber(19)
  $core.int get selectedAltitudeFt => $_getIZ(18);
  @$pb.TagNumber(19)
  set selectedAltitudeFt($core.int value) => $_setSignedInt32(18, value);
  @$pb.TagNumber(19)
  $core.bool hasSelectedAltitudeFt() => $_has(18);
  @$pb.TagNumber(19)
  void clearSelectedAltitudeFt() => $_clearField(19);

  @$pb.TagNumber(20)
  Aircraft_AltitudeSource get selectedAltitudeSource => $_getN(19);
  @$pb.TagNumber(20)
  set selectedAltitudeSource(Aircraft_AltitudeSource value) =>
      $_setField(20, value);
  @$pb.TagNumber(20)
  $core.bool hasSelectedAltitudeSource() => $_has(19);
  @$pb.TagNumber(20)
  void clearSelectedAltitudeSource() => $_clearField(20);

  @$pb.TagNumber(21)
  $core.double get selectedHeadingDeg => $_getN(20);
  @$pb.TagNumber(21)
  set selectedHeadingDeg($core.double value) => $_setDouble(20, value);
  @$pb.TagNumber(21)
  $core.bool hasSelectedHeadingDeg() => $_has(20);
  @$pb.TagNumber(21)
  void clearSelectedHeadingDeg() => $_clearField(21);

  @$pb.TagNumber(22)
  $core.double get baroSettingHpa => $_getN(21);
  @$pb.TagNumber(22)
  set baroSettingHpa($core.double value) => $_setDouble(21, value);
  @$pb.TagNumber(22)
  $core.bool hasBaroSettingHpa() => $_has(21);
  @$pb.TagNumber(22)
  void clearBaroSettingHpa() => $_clearField(22);

  @$pb.TagNumber(23)
  AutopilotModes get autopilotModes => $_getN(22);
  @$pb.TagNumber(23)
  set autopilotModes(AutopilotModes value) => $_setField(23, value);
  @$pb.TagNumber(23)
  $core.bool hasAutopilotModes() => $_has(22);
  @$pb.TagNumber(23)
  void clearAutopilotModes() => $_clearField(23);
  @$pb.TagNumber(23)
  AutopilotModes ensureAutopilotModes() => $_ensure(22);

  @$pb.TagNumber(24)
  $core.int get adsbVersion => $_getIZ(23);
  @$pb.TagNumber(24)
  set adsbVersion($core.int value) => $_setSignedInt32(23, value);
  @$pb.TagNumber(24)
  $core.bool hasAdsbVersion() => $_has(23);
  @$pb.TagNumber(24)
  void clearAdsbVersion() => $_clearField(24);

  @$pb.TagNumber(25)
  $core.int get nacP => $_getIZ(24);
  @$pb.TagNumber(25)
  set nacP($core.int value) => $_setSignedInt32(24, value);
  @$pb.TagNumber(25)
  $core.bool hasNacP() => $_has(24);
  @$pb.TagNumber(25)
  void clearNacP() => $_clearField(25);

  @$pb.TagNumber(26)
  $core.int get sil => $_getIZ(25);
  @$pb.TagNumber(26)
  set sil($core.int value) => $_setSignedInt32(25, value);
  @$pb.TagNumber(26)
  $core.bool hasSil() => $_has(25);
  @$pb.TagNumber(26)
  void clearSil() => $_clearField(26);

  @$pb.TagNumber(27)
  $fixnum.Int64 get messages => $_getI64(26);
  @$pb.TagNumber(27)
  set messages($fixnum.Int64 value) => $_setInt64(26, value);
  @$pb.TagNumber(27)
  $core.bool hasMessages() => $_has(26);
  @$pb.TagNumber(27)
  void clearMessages() => $_clearField(27);

  @$pb.TagNumber(28)
  $core.double get signalDb => $_getN(27);
  @$pb.TagNumber(28)
  set signalDb($core.double value) => $_setDouble(27, value);
  @$pb.TagNumber(28)
  $core.bool hasSignalDb() => $_has(27);
  @$pb.TagNumber(28)
  void clearSignalDb() => $_clearField(28);

  @$pb.TagNumber(29)
  $core.double get lastSeenS => $_getN(28);
  @$pb.TagNumber(29)
  set lastSeenS($core.double value) => $_setDouble(28, value);
  @$pb.TagNumber(29)
  $core.bool hasLastSeenS() => $_has(28);
  @$pb.TagNumber(29)
  void clearLastSeenS() => $_clearField(29);

  @$pb.TagNumber(30)
  $core.double get lastSeenUnixS => $_getN(29);
  @$pb.TagNumber(30)
  set lastSeenUnixS($core.double value) => $_setDouble(29, value);
  @$pb.TagNumber(30)
  $core.bool hasLastSeenUnixS() => $_has(29);
  @$pb.TagNumber(30)
  void clearLastSeenUnixS() => $_clearField(30);
}

/// Wrapped so that "not known yet" (unset) differs from "none engaged" (empty).
class AutopilotModes extends $pb.GeneratedMessage {
  factory AutopilotModes({
    $core.Iterable<AutopilotModes_Mode>? modes,
  }) {
    final result = create();
    if (modes != null) result.modes.addAll(modes);
    return result;
  }

  AutopilotModes._();

  factory AutopilotModes.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory AutopilotModes.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'AutopilotModes',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..pc<AutopilotModes_Mode>(
        1, _omitFieldNames ? '' : 'modes', $pb.PbFieldType.KE,
        valueOf: AutopilotModes_Mode.valueOf,
        enumValues: AutopilotModes_Mode.values,
        defaultEnumValue: AutopilotModes_Mode.MODE_UNSPECIFIED)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AutopilotModes clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AutopilotModes copyWith(void Function(AutopilotModes) updates) =>
      super.copyWith((message) => updates(message as AutopilotModes))
          as AutopilotModes;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AutopilotModes create() => AutopilotModes._();
  @$core.override
  AutopilotModes createEmptyInstance() => create();
  static $pb.PbList<AutopilotModes> createRepeated() =>
      $pb.PbList<AutopilotModes>();
  @$core.pragma('dart2js:noInline')
  static AutopilotModes getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<AutopilotModes>(create);
  static AutopilotModes? _defaultInstance;

  @$pb.TagNumber(1)
  $pb.PbList<AutopilotModes_Mode> get modes => $_getList(0);
}

class SnapshotRequest extends $pb.GeneratedMessage {
  factory SnapshotRequest({
    $core.double? t,
  }) {
    final result = create();
    if (t != null) result.t = t;
    return result;
  }

  SnapshotRequest._();

  factory SnapshotRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory SnapshotRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'SnapshotRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 't')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SnapshotRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  SnapshotRequest copyWith(void Function(SnapshotRequest) updates) =>
      super.copyWith((message) => updates(message as SnapshotRequest))
          as SnapshotRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static SnapshotRequest create() => SnapshotRequest._();
  @$core.override
  SnapshotRequest createEmptyInstance() => create();
  static $pb.PbList<SnapshotRequest> createRepeated() =>
      $pb.PbList<SnapshotRequest>();
  @$core.pragma('dart2js:noInline')
  static SnapshotRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<SnapshotRequest>(create);
  static SnapshotRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get t => $_getN(0);
  @$pb.TagNumber(1)
  set t($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasT() => $_has(0);
  @$pb.TagNumber(1)
  void clearT() => $_clearField(1);
}

class Snapshot_Entry extends $pb.GeneratedMessage {
  factory Snapshot_Entry({
    Aircraft? state,
    $core.Iterable<TrackPoint>? track,
  }) {
    final result = create();
    if (state != null) result.state = state;
    if (track != null) result.track.addAll(track);
    return result;
  }

  Snapshot_Entry._();

  factory Snapshot_Entry.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Snapshot_Entry.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Snapshot.Entry',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOM<Aircraft>(1, _omitFieldNames ? '' : 'state',
        subBuilder: Aircraft.create)
    ..pPM<TrackPoint>(2, _omitFieldNames ? '' : 'track',
        subBuilder: TrackPoint.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Snapshot_Entry clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Snapshot_Entry copyWith(void Function(Snapshot_Entry) updates) =>
      super.copyWith((message) => updates(message as Snapshot_Entry))
          as Snapshot_Entry;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Snapshot_Entry create() => Snapshot_Entry._();
  @$core.override
  Snapshot_Entry createEmptyInstance() => create();
  static $pb.PbList<Snapshot_Entry> createRepeated() =>
      $pb.PbList<Snapshot_Entry>();
  @$core.pragma('dart2js:noInline')
  static Snapshot_Entry getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<Snapshot_Entry>(create);
  static Snapshot_Entry? _defaultInstance;

  @$pb.TagNumber(1)
  Aircraft get state => $_getN(0);
  @$pb.TagNumber(1)
  set state(Aircraft value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasState() => $_has(0);
  @$pb.TagNumber(1)
  void clearState() => $_clearField(1);
  @$pb.TagNumber(1)
  Aircraft ensureState() => $_ensure(0);

  @$pb.TagNumber(2)
  $pb.PbList<TrackPoint> get track => $_getList(1);
}

class Snapshot extends $pb.GeneratedMessage {
  factory Snapshot({
    $core.double? t,
    $core.double? tMin,
    $core.double? tMax,
    $core.Iterable<Snapshot_Entry>? aircraft,
  }) {
    final result = create();
    if (t != null) result.t = t;
    if (tMin != null) result.tMin = tMin;
    if (tMax != null) result.tMax = tMax;
    if (aircraft != null) result.aircraft.addAll(aircraft);
    return result;
  }

  Snapshot._();

  factory Snapshot.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Snapshot.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Snapshot',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 't')
    ..aD(2, _omitFieldNames ? '' : 'tMin')
    ..aD(3, _omitFieldNames ? '' : 'tMax')
    ..pPM<Snapshot_Entry>(4, _omitFieldNames ? '' : 'aircraft',
        subBuilder: Snapshot_Entry.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Snapshot clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Snapshot copyWith(void Function(Snapshot) updates) =>
      super.copyWith((message) => updates(message as Snapshot)) as Snapshot;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Snapshot create() => Snapshot._();
  @$core.override
  Snapshot createEmptyInstance() => create();
  static $pb.PbList<Snapshot> createRepeated() => $pb.PbList<Snapshot>();
  @$core.pragma('dart2js:noInline')
  static Snapshot getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Snapshot>(create);
  static Snapshot? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get t => $_getN(0);
  @$pb.TagNumber(1)
  set t($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasT() => $_has(0);
  @$pb.TagNumber(1)
  void clearT() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get tMin => $_getN(1);
  @$pb.TagNumber(2)
  set tMin($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasTMin() => $_has(1);
  @$pb.TagNumber(2)
  void clearTMin() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get tMax => $_getN(2);
  @$pb.TagNumber(3)
  set tMax($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasTMax() => $_has(2);
  @$pb.TagNumber(3)
  void clearTMax() => $_clearField(3);

  @$pb.TagNumber(4)
  $pb.PbList<Snapshot_Entry> get aircraft => $_getList(3);
}

class TrackPoint extends $pb.GeneratedMessage {
  factory TrackPoint({
    $core.double? lat,
    $core.double? lon,
    $core.int? altitudeFt,
  }) {
    final result = create();
    if (lat != null) result.lat = lat;
    if (lon != null) result.lon = lon;
    if (altitudeFt != null) result.altitudeFt = altitudeFt;
    return result;
  }

  TrackPoint._();

  factory TrackPoint.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory TrackPoint.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'TrackPoint',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'lat')
    ..aD(2, _omitFieldNames ? '' : 'lon')
    ..aI(3, _omitFieldNames ? '' : 'altitudeFt')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  TrackPoint clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  TrackPoint copyWith(void Function(TrackPoint) updates) =>
      super.copyWith((message) => updates(message as TrackPoint)) as TrackPoint;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static TrackPoint create() => TrackPoint._();
  @$core.override
  TrackPoint createEmptyInstance() => create();
  static $pb.PbList<TrackPoint> createRepeated() => $pb.PbList<TrackPoint>();
  @$core.pragma('dart2js:noInline')
  static TrackPoint getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<TrackPoint>(create);
  static TrackPoint? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get lat => $_getN(0);
  @$pb.TagNumber(1)
  set lat($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasLat() => $_has(0);
  @$pb.TagNumber(1)
  void clearLat() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get lon => $_getN(1);
  @$pb.TagNumber(2)
  set lon($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasLon() => $_has(1);
  @$pb.TagNumber(2)
  void clearLon() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.int get altitudeFt => $_getIZ(2);
  @$pb.TagNumber(3)
  set altitudeFt($core.int value) => $_setSignedInt32(2, value);
  @$pb.TagNumber(3)
  $core.bool hasAltitudeFt() => $_has(2);
  @$pb.TagNumber(3)
  void clearAltitudeFt() => $_clearField(3);
}

class AircraftHistoryRequest extends $pb.GeneratedMessage {
  factory AircraftHistoryRequest({
    $core.int? icao,
    $core.double? t,
  }) {
    final result = create();
    if (icao != null) result.icao = icao;
    if (t != null) result.t = t;
    return result;
  }

  AircraftHistoryRequest._();

  factory AircraftHistoryRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory AircraftHistoryRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'AircraftHistoryRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'icao', fieldType: $pb.PbFieldType.OU3)
    ..aD(2, _omitFieldNames ? '' : 't')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistoryRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistoryRequest copyWith(
          void Function(AircraftHistoryRequest) updates) =>
      super.copyWith((message) => updates(message as AircraftHistoryRequest))
          as AircraftHistoryRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AircraftHistoryRequest create() => AircraftHistoryRequest._();
  @$core.override
  AircraftHistoryRequest createEmptyInstance() => create();
  static $pb.PbList<AircraftHistoryRequest> createRepeated() =>
      $pb.PbList<AircraftHistoryRequest>();
  @$core.pragma('dart2js:noInline')
  static AircraftHistoryRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<AircraftHistoryRequest>(create);
  static AircraftHistoryRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get icao => $_getIZ(0);
  @$pb.TagNumber(1)
  set icao($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIcao() => $_has(0);
  @$pb.TagNumber(1)
  void clearIcao() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get t => $_getN(1);
  @$pb.TagNumber(2)
  set t($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasT() => $_has(1);
  @$pb.TagNumber(2)
  void clearT() => $_clearField(2);
}

class AircraftHistory_Point extends $pb.GeneratedMessage {
  factory AircraftHistory_Point({
    $core.double? t,
    $core.int? altitudeFt,
    $core.double? groundSpeedKt,
    $core.int? verticalRateFpm,
    $core.double? signalDb,
    $core.int? selectedAltitudeFt,
  }) {
    final result = create();
    if (t != null) result.t = t;
    if (altitudeFt != null) result.altitudeFt = altitudeFt;
    if (groundSpeedKt != null) result.groundSpeedKt = groundSpeedKt;
    if (verticalRateFpm != null) result.verticalRateFpm = verticalRateFpm;
    if (signalDb != null) result.signalDb = signalDb;
    if (selectedAltitudeFt != null)
      result.selectedAltitudeFt = selectedAltitudeFt;
    return result;
  }

  AircraftHistory_Point._();

  factory AircraftHistory_Point.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory AircraftHistory_Point.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'AircraftHistory.Point',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 't')
    ..aI(2, _omitFieldNames ? '' : 'altitudeFt')
    ..aD(3, _omitFieldNames ? '' : 'groundSpeedKt')
    ..aI(4, _omitFieldNames ? '' : 'verticalRateFpm')
    ..aD(5, _omitFieldNames ? '' : 'signalDb')
    ..aI(6, _omitFieldNames ? '' : 'selectedAltitudeFt')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistory_Point clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistory_Point copyWith(
          void Function(AircraftHistory_Point) updates) =>
      super.copyWith((message) => updates(message as AircraftHistory_Point))
          as AircraftHistory_Point;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AircraftHistory_Point create() => AircraftHistory_Point._();
  @$core.override
  AircraftHistory_Point createEmptyInstance() => create();
  static $pb.PbList<AircraftHistory_Point> createRepeated() =>
      $pb.PbList<AircraftHistory_Point>();
  @$core.pragma('dart2js:noInline')
  static AircraftHistory_Point getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<AircraftHistory_Point>(create);
  static AircraftHistory_Point? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get t => $_getN(0);
  @$pb.TagNumber(1)
  set t($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasT() => $_has(0);
  @$pb.TagNumber(1)
  void clearT() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.int get altitudeFt => $_getIZ(1);
  @$pb.TagNumber(2)
  set altitudeFt($core.int value) => $_setSignedInt32(1, value);
  @$pb.TagNumber(2)
  $core.bool hasAltitudeFt() => $_has(1);
  @$pb.TagNumber(2)
  void clearAltitudeFt() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get groundSpeedKt => $_getN(2);
  @$pb.TagNumber(3)
  set groundSpeedKt($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasGroundSpeedKt() => $_has(2);
  @$pb.TagNumber(3)
  void clearGroundSpeedKt() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.int get verticalRateFpm => $_getIZ(3);
  @$pb.TagNumber(4)
  set verticalRateFpm($core.int value) => $_setSignedInt32(3, value);
  @$pb.TagNumber(4)
  $core.bool hasVerticalRateFpm() => $_has(3);
  @$pb.TagNumber(4)
  void clearVerticalRateFpm() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.double get signalDb => $_getN(4);
  @$pb.TagNumber(5)
  set signalDb($core.double value) => $_setDouble(4, value);
  @$pb.TagNumber(5)
  $core.bool hasSignalDb() => $_has(4);
  @$pb.TagNumber(5)
  void clearSignalDb() => $_clearField(5);

  @$pb.TagNumber(6)
  $core.int get selectedAltitudeFt => $_getIZ(5);
  @$pb.TagNumber(6)
  set selectedAltitudeFt($core.int value) => $_setSignedInt32(5, value);
  @$pb.TagNumber(6)
  $core.bool hasSelectedAltitudeFt() => $_has(5);
  @$pb.TagNumber(6)
  void clearSelectedAltitudeFt() => $_clearField(6);
}

class AircraftHistory extends $pb.GeneratedMessage {
  factory AircraftHistory({
    $core.double? t,
    $core.Iterable<AircraftHistory_Point>? points,
  }) {
    final result = create();
    if (t != null) result.t = t;
    if (points != null) result.points.addAll(points);
    return result;
  }

  AircraftHistory._();

  factory AircraftHistory.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory AircraftHistory.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'AircraftHistory',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 't')
    ..pPM<AircraftHistory_Point>(2, _omitFieldNames ? '' : 'points',
        subBuilder: AircraftHistory_Point.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistory clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftHistory copyWith(void Function(AircraftHistory) updates) =>
      super.copyWith((message) => updates(message as AircraftHistory))
          as AircraftHistory;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AircraftHistory create() => AircraftHistory._();
  @$core.override
  AircraftHistory createEmptyInstance() => create();
  static $pb.PbList<AircraftHistory> createRepeated() =>
      $pb.PbList<AircraftHistory>();
  @$core.pragma('dart2js:noInline')
  static AircraftHistory getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<AircraftHistory>(create);
  static AircraftHistory? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get t => $_getN(0);
  @$pb.TagNumber(1)
  set t($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasT() => $_has(0);
  @$pb.TagNumber(1)
  void clearT() => $_clearField(1);

  @$pb.TagNumber(2)
  $pb.PbList<AircraftHistory_Point> get points => $_getList(1);
}

class ActivityRequest extends $pb.GeneratedMessage {
  factory ActivityRequest({
    $core.double? from,
  }) {
    final result = create();
    if (from != null) result.from = from;
    return result;
  }

  ActivityRequest._();

  factory ActivityRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ActivityRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ActivityRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'from')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ActivityRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ActivityRequest copyWith(void Function(ActivityRequest) updates) =>
      super.copyWith((message) => updates(message as ActivityRequest))
          as ActivityRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ActivityRequest create() => ActivityRequest._();
  @$core.override
  ActivityRequest createEmptyInstance() => create();
  static $pb.PbList<ActivityRequest> createRepeated() =>
      $pb.PbList<ActivityRequest>();
  @$core.pragma('dart2js:noInline')
  static ActivityRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ActivityRequest>(create);
  static ActivityRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get from => $_getN(0);
  @$pb.TagNumber(1)
  set from($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasFrom() => $_has(0);
  @$pb.TagNumber(1)
  void clearFrom() => $_clearField(1);
}

class Activity extends $pb.GeneratedMessage {
  factory Activity({
    $core.double? bucketS,
    $core.double? t0,
    $core.Iterable<$core.int>? counts,
  }) {
    final result = create();
    if (bucketS != null) result.bucketS = bucketS;
    if (t0 != null) result.t0 = t0;
    if (counts != null) result.counts.addAll(counts);
    return result;
  }

  Activity._();

  factory Activity.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Activity.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Activity',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'bucketS')
    ..aD(2, _omitFieldNames ? '' : 't0')
    ..p<$core.int>(3, _omitFieldNames ? '' : 'counts', $pb.PbFieldType.KU3)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Activity clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Activity copyWith(void Function(Activity) updates) =>
      super.copyWith((message) => updates(message as Activity)) as Activity;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Activity create() => Activity._();
  @$core.override
  Activity createEmptyInstance() => create();
  static $pb.PbList<Activity> createRepeated() => $pb.PbList<Activity>();
  @$core.pragma('dart2js:noInline')
  static Activity getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Activity>(create);
  static Activity? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get bucketS => $_getN(0);
  @$pb.TagNumber(1)
  set bucketS($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasBucketS() => $_has(0);
  @$pb.TagNumber(1)
  void clearBucketS() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get t0 => $_getN(1);
  @$pb.TagNumber(2)
  set t0($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasT0() => $_has(1);
  @$pb.TagNumber(2)
  void clearT0() => $_clearField(2);

  @$pb.TagNumber(3)
  $pb.PbList<$core.int> get counts => $_getList(2);
}

class HeatmapRequest extends $pb.GeneratedMessage {
  factory HeatmapRequest({
    $core.double? west,
    $core.double? south,
    $core.double? east,
    $core.double? north,
    $core.int? width,
    $core.int? height,
    $core.double? t0,
    $core.double? t1,
  }) {
    final result = create();
    if (west != null) result.west = west;
    if (south != null) result.south = south;
    if (east != null) result.east = east;
    if (north != null) result.north = north;
    if (width != null) result.width = width;
    if (height != null) result.height = height;
    if (t0 != null) result.t0 = t0;
    if (t1 != null) result.t1 = t1;
    return result;
  }

  HeatmapRequest._();

  factory HeatmapRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory HeatmapRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'HeatmapRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'west')
    ..aD(2, _omitFieldNames ? '' : 'south')
    ..aD(3, _omitFieldNames ? '' : 'east')
    ..aD(4, _omitFieldNames ? '' : 'north')
    ..aI(5, _omitFieldNames ? '' : 'width', fieldType: $pb.PbFieldType.OU3)
    ..aI(6, _omitFieldNames ? '' : 'height', fieldType: $pb.PbFieldType.OU3)
    ..aD(7, _omitFieldNames ? '' : 't0')
    ..aD(8, _omitFieldNames ? '' : 't1')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  HeatmapRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  HeatmapRequest copyWith(void Function(HeatmapRequest) updates) =>
      super.copyWith((message) => updates(message as HeatmapRequest))
          as HeatmapRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static HeatmapRequest create() => HeatmapRequest._();
  @$core.override
  HeatmapRequest createEmptyInstance() => create();
  static $pb.PbList<HeatmapRequest> createRepeated() =>
      $pb.PbList<HeatmapRequest>();
  @$core.pragma('dart2js:noInline')
  static HeatmapRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<HeatmapRequest>(create);
  static HeatmapRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get west => $_getN(0);
  @$pb.TagNumber(1)
  set west($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasWest() => $_has(0);
  @$pb.TagNumber(1)
  void clearWest() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get south => $_getN(1);
  @$pb.TagNumber(2)
  set south($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasSouth() => $_has(1);
  @$pb.TagNumber(2)
  void clearSouth() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get east => $_getN(2);
  @$pb.TagNumber(3)
  set east($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasEast() => $_has(2);
  @$pb.TagNumber(3)
  void clearEast() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.double get north => $_getN(3);
  @$pb.TagNumber(4)
  set north($core.double value) => $_setDouble(3, value);
  @$pb.TagNumber(4)
  $core.bool hasNorth() => $_has(3);
  @$pb.TagNumber(4)
  void clearNorth() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.int get width => $_getIZ(4);
  @$pb.TagNumber(5)
  set width($core.int value) => $_setUnsignedInt32(4, value);
  @$pb.TagNumber(5)
  $core.bool hasWidth() => $_has(4);
  @$pb.TagNumber(5)
  void clearWidth() => $_clearField(5);

  @$pb.TagNumber(6)
  $core.int get height => $_getIZ(5);
  @$pb.TagNumber(6)
  set height($core.int value) => $_setUnsignedInt32(5, value);
  @$pb.TagNumber(6)
  $core.bool hasHeight() => $_has(5);
  @$pb.TagNumber(6)
  void clearHeight() => $_clearField(6);

  @$pb.TagNumber(7)
  $core.double get t0 => $_getN(6);
  @$pb.TagNumber(7)
  set t0($core.double value) => $_setDouble(6, value);
  @$pb.TagNumber(7)
  $core.bool hasT0() => $_has(6);
  @$pb.TagNumber(7)
  void clearT0() => $_clearField(7);

  @$pb.TagNumber(8)
  $core.double get t1 => $_getN(7);
  @$pb.TagNumber(8)
  set t1($core.double value) => $_setDouble(7, value);
  @$pb.TagNumber(8)
  $core.bool hasT1() => $_has(7);
  @$pb.TagNumber(8)
  void clearT1() => $_clearField(8);
}

class Heatmap extends $pb.GeneratedMessage {
  factory Heatmap({
    $core.int? max,
    $core.Iterable<$core.int>? cellIndex,
    $core.Iterable<$core.int>? cellCount,
  }) {
    final result = create();
    if (max != null) result.max = max;
    if (cellIndex != null) result.cellIndex.addAll(cellIndex);
    if (cellCount != null) result.cellCount.addAll(cellCount);
    return result;
  }

  Heatmap._();

  factory Heatmap.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Heatmap.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Heatmap',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'max', fieldType: $pb.PbFieldType.OU3)
    ..p<$core.int>(2, _omitFieldNames ? '' : 'cellIndex', $pb.PbFieldType.KU3)
    ..p<$core.int>(3, _omitFieldNames ? '' : 'cellCount', $pb.PbFieldType.KU3)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Heatmap clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Heatmap copyWith(void Function(Heatmap) updates) =>
      super.copyWith((message) => updates(message as Heatmap)) as Heatmap;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Heatmap create() => Heatmap._();
  @$core.override
  Heatmap createEmptyInstance() => create();
  static $pb.PbList<Heatmap> createRepeated() => $pb.PbList<Heatmap>();
  @$core.pragma('dart2js:noInline')
  static Heatmap getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Heatmap>(create);
  static Heatmap? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get max => $_getIZ(0);
  @$pb.TagNumber(1)
  set max($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasMax() => $_has(0);
  @$pb.TagNumber(1)
  void clearMax() => $_clearField(1);

  /// The nonzero cells: row-major index (row 0 at the north) and flight count, one pair per position.
  @$pb.TagNumber(2)
  $pb.PbList<$core.int> get cellIndex => $_getList(1);

  @$pb.TagNumber(3)
  $pb.PbList<$core.int> get cellCount => $_getList(2);
}

const $core.bool _omitFieldNames =
    $core.bool.fromEnvironment('protobuf.omit_field_names');
const $core.bool _omitMessageNames =
    $core.bool.fromEnvironment('protobuf.omit_message_names');
