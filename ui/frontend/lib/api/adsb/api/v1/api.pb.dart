// This is a generated file - do not edit.
//
// Generated from adsb/api/v1/api.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names

import 'dart:async' as $async;
import 'dart:core' as $core;

import 'package:fixnum/fixnum.dart' as $fixnum;
import 'package:protobuf/protobuf.dart' as $pb;

import 'api.pbenum.dart';

export 'package:protobuf/protobuf.dart' show GeneratedMessageGenericExtensions;

export 'api.pbenum.dart';

class GetReceiverInfoRequest extends $pb.GeneratedMessage {
  factory GetReceiverInfoRequest() => create();

  GetReceiverInfoRequest._();

  factory GetReceiverInfoRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory GetReceiverInfoRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'GetReceiverInfoRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetReceiverInfoRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetReceiverInfoRequest copyWith(
          void Function(GetReceiverInfoRequest) updates) =>
      super.copyWith((message) => updates(message as GetReceiverInfoRequest))
          as GetReceiverInfoRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetReceiverInfoRequest create() => GetReceiverInfoRequest._();
  @$core.override
  GetReceiverInfoRequest createEmptyInstance() => create();
  static $pb.PbList<GetReceiverInfoRequest> createRepeated() =>
      $pb.PbList<GetReceiverInfoRequest>();
  @$core.pragma('dart2js:noInline')
  static GetReceiverInfoRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<GetReceiverInfoRequest>(create);
  static GetReceiverInfoRequest? _defaultInstance;
}

/// Fixed for the run.
class ReceiverInfo extends $pb.GeneratedMessage {
  factory ReceiverInfo({
    $core.String? source,
    $core.double? defaultIntervalS,
    $core.double? maxCaptureS,
  }) {
    final result = create();
    if (source != null) result.source = source;
    if (defaultIntervalS != null) result.defaultIntervalS = defaultIntervalS;
    if (maxCaptureS != null) result.maxCaptureS = maxCaptureS;
    return result;
  }

  ReceiverInfo._();

  factory ReceiverInfo.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ReceiverInfo.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ReceiverInfo',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'source')
    ..aD(2, _omitFieldNames ? '' : 'defaultIntervalS')
    ..aD(3, _omitFieldNames ? '' : 'maxCaptureS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ReceiverInfo clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ReceiverInfo copyWith(void Function(ReceiverInfo) updates) =>
      super.copyWith((message) => updates(message as ReceiverInfo))
          as ReceiverInfo;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ReceiverInfo create() => ReceiverInfo._();
  @$core.override
  ReceiverInfo createEmptyInstance() => create();
  static $pb.PbList<ReceiverInfo> createRepeated() =>
      $pb.PbList<ReceiverInfo>();
  @$core.pragma('dart2js:noInline')
  static ReceiverInfo getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ReceiverInfo>(create);
  static ReceiverInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get source => $_getSZ(0);
  @$pb.TagNumber(1)
  set source($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasSource() => $_has(0);
  @$pb.TagNumber(1)
  void clearSource() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get defaultIntervalS => $_getN(1);
  @$pb.TagNumber(2)
  set defaultIntervalS($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasDefaultIntervalS() => $_has(1);
  @$pb.TagNumber(2)
  void clearDefaultIntervalS() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get maxCaptureS => $_getN(2);
  @$pb.TagNumber(3)
  set maxCaptureS($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasMaxCaptureS() => $_has(2);
  @$pb.TagNumber(3)
  void clearMaxCaptureS() => $_clearField(3);
}

class WatchFramesRequest extends $pb.GeneratedMessage {
  factory WatchFramesRequest({
    $core.double? intervalS,
  }) {
    final result = create();
    if (intervalS != null) result.intervalS = intervalS;
    return result;
  }

  WatchFramesRequest._();

  factory WatchFramesRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory WatchFramesRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'WatchFramesRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'intervalS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchFramesRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchFramesRequest copyWith(void Function(WatchFramesRequest) updates) =>
      super.copyWith((message) => updates(message as WatchFramesRequest))
          as WatchFramesRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static WatchFramesRequest create() => WatchFramesRequest._();
  @$core.override
  WatchFramesRequest createEmptyInstance() => create();
  static $pb.PbList<WatchFramesRequest> createRepeated() =>
      $pb.PbList<WatchFramesRequest>();
  @$core.pragma('dart2js:noInline')
  static WatchFramesRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<WatchFramesRequest>(create);
  static WatchFramesRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get intervalS => $_getN(0);
  @$pb.TagNumber(1)
  set intervalS($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIntervalS() => $_has(0);
  @$pb.TagNumber(1)
  void clearIntervalS() => $_clearField(1);
}

class FrameBatch extends $pb.GeneratedMessage {
  factory FrameBatch({
    $core.Iterable<Frame>? frames,
    $core.bool? framesDropped,
  }) {
    final result = create();
    if (frames != null) result.frames.addAll(frames);
    if (framesDropped != null) result.framesDropped = framesDropped;
    return result;
  }

  FrameBatch._();

  factory FrameBatch.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory FrameBatch.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'FrameBatch',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..pPM<Frame>(1, _omitFieldNames ? '' : 'frames', subBuilder: Frame.create)
    ..aOB(2, _omitFieldNames ? '' : 'framesDropped')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  FrameBatch clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  FrameBatch copyWith(void Function(FrameBatch) updates) =>
      super.copyWith((message) => updates(message as FrameBatch)) as FrameBatch;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static FrameBatch create() => FrameBatch._();
  @$core.override
  FrameBatch createEmptyInstance() => create();
  static $pb.PbList<FrameBatch> createRepeated() => $pb.PbList<FrameBatch>();
  @$core.pragma('dart2js:noInline')
  static FrameBatch getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<FrameBatch>(create);
  static FrameBatch? _defaultInstance;

  @$pb.TagNumber(1)
  $pb.PbList<Frame> get frames => $_getList(0);

  @$pb.TagNumber(2)
  $core.bool get framesDropped => $_getBF(1);
  @$pb.TagNumber(2)
  set framesDropped($core.bool value) => $_setBool(1, value);
  @$pb.TagNumber(2)
  $core.bool hasFramesDropped() => $_has(1);
  @$pb.TagNumber(2)
  void clearFramesDropped() => $_clearField(2);
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

class WatchAircraftRequest extends $pb.GeneratedMessage {
  factory WatchAircraftRequest({
    $core.double? intervalS,
  }) {
    final result = create();
    if (intervalS != null) result.intervalS = intervalS;
    return result;
  }

  WatchAircraftRequest._();

  factory WatchAircraftRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory WatchAircraftRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'WatchAircraftRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'intervalS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchAircraftRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchAircraftRequest copyWith(void Function(WatchAircraftRequest) updates) =>
      super.copyWith((message) => updates(message as WatchAircraftRequest))
          as WatchAircraftRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static WatchAircraftRequest create() => WatchAircraftRequest._();
  @$core.override
  WatchAircraftRequest createEmptyInstance() => create();
  static $pb.PbList<WatchAircraftRequest> createRepeated() =>
      $pb.PbList<WatchAircraftRequest>();
  @$core.pragma('dart2js:noInline')
  static WatchAircraftRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<WatchAircraftRequest>(create);
  static WatchAircraftRequest? _defaultInstance;

  @$pb.TagNumber(1)
  $core.double get intervalS => $_getN(0);
  @$pb.TagNumber(1)
  set intervalS($core.double value) => $_setDouble(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIntervalS() => $_has(0);
  @$pb.TagNumber(1)
  void clearIntervalS() => $_clearField(1);
}

class AircraftChanges extends $pb.GeneratedMessage {
  factory AircraftChanges({
    $core.Iterable<Aircraft>? aircraft,
  }) {
    final result = create();
    if (aircraft != null) result.aircraft.addAll(aircraft);
    return result;
  }

  AircraftChanges._();

  factory AircraftChanges.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory AircraftChanges.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'AircraftChanges',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..pPM<Aircraft>(1, _omitFieldNames ? '' : 'aircraft',
        subBuilder: Aircraft.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftChanges clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  AircraftChanges copyWith(void Function(AircraftChanges) updates) =>
      super.copyWith((message) => updates(message as AircraftChanges))
          as AircraftChanges;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static AircraftChanges create() => AircraftChanges._();
  @$core.override
  AircraftChanges createEmptyInstance() => create();
  static $pb.PbList<AircraftChanges> createRepeated() =>
      $pb.PbList<AircraftChanges>();
  @$core.pragma('dart2js:noInline')
  static AircraftChanges getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<AircraftChanges>(create);
  static AircraftChanges? _defaultInstance;

  @$pb.TagNumber(1)
  $pb.PbList<Aircraft> get aircraft => $_getList(0);
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

class WatchInputStatusRequest extends $pb.GeneratedMessage {
  factory WatchInputStatusRequest() => create();

  WatchInputStatusRequest._();

  factory WatchInputStatusRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory WatchInputStatusRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'WatchInputStatusRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchInputStatusRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchInputStatusRequest copyWith(
          void Function(WatchInputStatusRequest) updates) =>
      super.copyWith((message) => updates(message as WatchInputStatusRequest))
          as WatchInputStatusRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static WatchInputStatusRequest create() => WatchInputStatusRequest._();
  @$core.override
  WatchInputStatusRequest createEmptyInstance() => create();
  static $pb.PbList<WatchInputStatusRequest> createRepeated() =>
      $pb.PbList<WatchInputStatusRequest>();
  @$core.pragma('dart2js:noInline')
  static WatchInputStatusRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<WatchInputStatusRequest>(create);
  static WatchInputStatusRequest? _defaultInstance;
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

class WatchSpectrumRequest extends $pb.GeneratedMessage {
  factory WatchSpectrumRequest() => create();

  WatchSpectrumRequest._();

  factory WatchSpectrumRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory WatchSpectrumRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'WatchSpectrumRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchSpectrumRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchSpectrumRequest copyWith(void Function(WatchSpectrumRequest) updates) =>
      super.copyWith((message) => updates(message as WatchSpectrumRequest))
          as WatchSpectrumRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static WatchSpectrumRequest create() => WatchSpectrumRequest._();
  @$core.override
  WatchSpectrumRequest createEmptyInstance() => create();
  static $pb.PbList<WatchSpectrumRequest> createRepeated() =>
      $pb.PbList<WatchSpectrumRequest>();
  @$core.pragma('dart2js:noInline')
  static WatchSpectrumRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<WatchSpectrumRequest>(create);
  static WatchSpectrumRequest? _defaultInstance;
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

class WatchSettingsRequest extends $pb.GeneratedMessage {
  factory WatchSettingsRequest() => create();

  WatchSettingsRequest._();

  factory WatchSettingsRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory WatchSettingsRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'WatchSettingsRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchSettingsRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  WatchSettingsRequest copyWith(void Function(WatchSettingsRequest) updates) =>
      super.copyWith((message) => updates(message as WatchSettingsRequest))
          as WatchSettingsRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static WatchSettingsRequest create() => WatchSettingsRequest._();
  @$core.override
  WatchSettingsRequest createEmptyInstance() => create();
  static $pb.PbList<WatchSettingsRequest> createRepeated() =>
      $pb.PbList<WatchSettingsRequest>();
  @$core.pragma('dart2js:noInline')
  static WatchSettingsRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<WatchSettingsRequest>(create);
  static WatchSettingsRequest? _defaultInstance;
}

/// Each reply of WatchSettings is the whole of it.
class Settings extends $pb.GeneratedMessage {
  factory Settings({
    $core.String? captureDir,
  }) {
    final result = create();
    if (captureDir != null) result.captureDir = captureDir;
    return result;
  }

  Settings._();

  factory Settings.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Settings.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Settings',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'captureDir')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Settings clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Settings copyWith(void Function(Settings) updates) =>
      super.copyWith((message) => updates(message as Settings)) as Settings;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Settings create() => Settings._();
  @$core.override
  Settings createEmptyInstance() => create();
  static $pb.PbList<Settings> createRepeated() => $pb.PbList<Settings>();
  @$core.pragma('dart2js:noInline')
  static Settings getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Settings>(create);
  static Settings? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get captureDir => $_getSZ(0);
  @$pb.TagNumber(1)
  set captureDir($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasCaptureDir() => $_has(0);
  @$pb.TagNumber(1)
  void clearCaptureDir() => $_clearField(1);
}

/// Each field set is changed; each unset is left as it is.
class UpdateSettingsRequest extends $pb.GeneratedMessage {
  factory UpdateSettingsRequest({
    $core.String? captureDir,
  }) {
    final result = create();
    if (captureDir != null) result.captureDir = captureDir;
    return result;
  }

  UpdateSettingsRequest._();

  factory UpdateSettingsRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory UpdateSettingsRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'UpdateSettingsRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'captureDir')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  UpdateSettingsRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  UpdateSettingsRequest copyWith(
          void Function(UpdateSettingsRequest) updates) =>
      super.copyWith((message) => updates(message as UpdateSettingsRequest))
          as UpdateSettingsRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static UpdateSettingsRequest create() => UpdateSettingsRequest._();
  @$core.override
  UpdateSettingsRequest createEmptyInstance() => create();
  static $pb.PbList<UpdateSettingsRequest> createRepeated() =>
      $pb.PbList<UpdateSettingsRequest>();
  @$core.pragma('dart2js:noInline')
  static UpdateSettingsRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<UpdateSettingsRequest>(create);
  static UpdateSettingsRequest? _defaultInstance;

  /// An existing, writable directory on the server, relative to its working directory or absolute; empty turns
  /// captures off. A capture in progress carries on in the directory it started in.
  @$pb.TagNumber(1)
  $core.String get captureDir => $_getSZ(0);
  @$pb.TagNumber(1)
  set captureDir($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasCaptureDir() => $_has(0);
  @$pb.TagNumber(1)
  void clearCaptureDir() => $_clearField(1);
}

class RecordCaptureRequest extends $pb.GeneratedMessage {
  factory RecordCaptureRequest({
    CapturePoint? point,
    $core.double? durationS,
    $core.String? name,
  }) {
    final result = create();
    if (point != null) result.point = point;
    if (durationS != null) result.durationS = durationS;
    if (name != null) result.name = name;
    return result;
  }

  RecordCaptureRequest._();

  factory RecordCaptureRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory RecordCaptureRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'RecordCaptureRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aE<CapturePoint>(1, _omitFieldNames ? '' : 'point',
        enumValues: CapturePoint.values)
    ..aD(2, _omitFieldNames ? '' : 'durationS')
    ..aOS(3, _omitFieldNames ? '' : 'name')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  RecordCaptureRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  RecordCaptureRequest copyWith(void Function(RecordCaptureRequest) updates) =>
      super.copyWith((message) => updates(message as RecordCaptureRequest))
          as RecordCaptureRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RecordCaptureRequest create() => RecordCaptureRequest._();
  @$core.override
  RecordCaptureRequest createEmptyInstance() => create();
  static $pb.PbList<RecordCaptureRequest> createRepeated() =>
      $pb.PbList<RecordCaptureRequest>();
  @$core.pragma('dart2js:noInline')
  static RecordCaptureRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<RecordCaptureRequest>(create);
  static RecordCaptureRequest? _defaultInstance;

  @$pb.TagNumber(1)
  CapturePoint get point => $_getN(0);
  @$pb.TagNumber(1)
  set point(CapturePoint value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasPoint() => $_has(0);
  @$pb.TagNumber(1)
  void clearPoint() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get durationS => $_getN(1);
  @$pb.TagNumber(2)
  set durationS($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasDurationS() => $_has(1);
  @$pb.TagNumber(2)
  void clearDurationS() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String value) => $_setString(2, value);
  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => $_clearField(3);
}

/// About twice a second while the capture runs, and a last one when it has covered its duration or the input
/// ended first.
class RecordCaptureProgress extends $pb.GeneratedMessage {
  factory RecordCaptureProgress({
    $fixnum.Int64? startIndex,
    $fixnum.Int64? samples,
    $fixnum.Int64? missing,
    Capture? capture,
  }) {
    final result = create();
    if (startIndex != null) result.startIndex = startIndex;
    if (samples != null) result.samples = samples;
    if (missing != null) result.missing = missing;
    if (capture != null) result.capture = capture;
    return result;
  }

  RecordCaptureProgress._();

  factory RecordCaptureProgress.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory RecordCaptureProgress.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'RecordCaptureProgress',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..a<$fixnum.Int64>(
        1, _omitFieldNames ? '' : 'startIndex', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..a<$fixnum.Int64>(2, _omitFieldNames ? '' : 'samples', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..a<$fixnum.Int64>(3, _omitFieldNames ? '' : 'missing', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..aOM<Capture>(4, _omitFieldNames ? '' : 'capture',
        subBuilder: Capture.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  RecordCaptureProgress clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  RecordCaptureProgress copyWith(
          void Function(RecordCaptureProgress) updates) =>
      super.copyWith((message) => updates(message as RecordCaptureProgress))
          as RecordCaptureProgress;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static RecordCaptureProgress create() => RecordCaptureProgress._();
  @$core.override
  RecordCaptureProgress createEmptyInstance() => create();
  static $pb.PbList<RecordCaptureProgress> createRepeated() =>
      $pb.PbList<RecordCaptureProgress>();
  @$core.pragma('dart2js:noInline')
  static RecordCaptureProgress getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<RecordCaptureProgress>(create);
  static RecordCaptureProgress? _defaultInstance;

  @$pb.TagNumber(1)
  $fixnum.Int64 get startIndex => $_getI64(0);
  @$pb.TagNumber(1)
  set startIndex($fixnum.Int64 value) => $_setInt64(0, value);
  @$pb.TagNumber(1)
  $core.bool hasStartIndex() => $_has(0);
  @$pb.TagNumber(1)
  void clearStartIndex() => $_clearField(1);

  @$pb.TagNumber(2)
  $fixnum.Int64 get samples => $_getI64(1);
  @$pb.TagNumber(2)
  set samples($fixnum.Int64 value) => $_setInt64(1, value);
  @$pb.TagNumber(2)
  $core.bool hasSamples() => $_has(1);
  @$pb.TagNumber(2)
  void clearSamples() => $_clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get missing => $_getI64(2);
  @$pb.TagNumber(3)
  set missing($fixnum.Int64 value) => $_setInt64(2, value);
  @$pb.TagNumber(3)
  $core.bool hasMissing() => $_has(2);
  @$pb.TagNumber(3)
  void clearMissing() => $_clearField(3);

  /// gap starts a new SigMF capture segment
  @$pb.TagNumber(4)
  Capture get capture => $_getN(3);
  @$pb.TagNumber(4)
  set capture(Capture value) => $_setField(4, value);
  @$pb.TagNumber(4)
  $core.bool hasCapture() => $_has(3);
  @$pb.TagNumber(4)
  void clearCapture() => $_clearField(4);
  @$pb.TagNumber(4)
  Capture ensureCapture() => $_ensure(3);
}

class ListCapturesRequest extends $pb.GeneratedMessage {
  factory ListCapturesRequest() => create();

  ListCapturesRequest._();

  factory ListCapturesRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ListCapturesRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ListCapturesRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ListCapturesRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ListCapturesRequest copyWith(void Function(ListCapturesRequest) updates) =>
      super.copyWith((message) => updates(message as ListCapturesRequest))
          as ListCapturesRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ListCapturesRequest create() => ListCapturesRequest._();
  @$core.override
  ListCapturesRequest createEmptyInstance() => create();
  static $pb.PbList<ListCapturesRequest> createRepeated() =>
      $pb.PbList<ListCapturesRequest>();
  @$core.pragma('dart2js:noInline')
  static ListCapturesRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ListCapturesRequest>(create);
  static ListCapturesRequest? _defaultInstance;
}

class ListCapturesResponse extends $pb.GeneratedMessage {
  factory ListCapturesResponse({
    $core.String? dir,
    $core.Iterable<Capture>? captures,
    $core.int? notListed,
  }) {
    final result = create();
    if (dir != null) result.dir = dir;
    if (captures != null) result.captures.addAll(captures);
    if (notListed != null) result.notListed = notListed;
    return result;
  }

  ListCapturesResponse._();

  factory ListCapturesResponse.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory ListCapturesResponse.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'ListCapturesResponse',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'dir')
    ..pPM<Capture>(2, _omitFieldNames ? '' : 'captures',
        subBuilder: Capture.create)
    ..aI(3, _omitFieldNames ? '' : 'notListed', fieldType: $pb.PbFieldType.OU3)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ListCapturesResponse clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  ListCapturesResponse copyWith(void Function(ListCapturesResponse) updates) =>
      super.copyWith((message) => updates(message as ListCapturesResponse))
          as ListCapturesResponse;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static ListCapturesResponse create() => ListCapturesResponse._();
  @$core.override
  ListCapturesResponse createEmptyInstance() => create();
  static $pb.PbList<ListCapturesResponse> createRepeated() =>
      $pb.PbList<ListCapturesResponse>();
  @$core.pragma('dart2js:noInline')
  static ListCapturesResponse getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<ListCapturesResponse>(create);
  static ListCapturesResponse? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get dir => $_getSZ(0);
  @$pb.TagNumber(1)
  set dir($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasDir() => $_has(0);
  @$pb.TagNumber(1)
  void clearDir() => $_clearField(1);

  @$pb.TagNumber(2)
  $pb.PbList<Capture> get captures => $_getList(1);

  @$pb.TagNumber(3)
  $core.int get notListed => $_getIZ(2);
  @$pb.TagNumber(3)
  set notListed($core.int value) => $_setUnsignedInt32(2, value);
  @$pb.TagNumber(3)
  $core.bool hasNotListed() => $_has(2);
  @$pb.TagNumber(3)
  void clearNotListed() => $_clearField(3);
}

/// One recording in the capture directory.
class Capture extends $pb.GeneratedMessage {
  factory Capture({
    $core.String? name,
    $core.String? dataPath,
    $fixnum.Int64? dataBytes,
    $core.double? modifiedUnixS,
    CaptureMeta? meta,
  }) {
    final result = create();
    if (name != null) result.name = name;
    if (dataPath != null) result.dataPath = dataPath;
    if (dataBytes != null) result.dataBytes = dataBytes;
    if (modifiedUnixS != null) result.modifiedUnixS = modifiedUnixS;
    if (meta != null) result.meta = meta;
    return result;
  }

  Capture._();

  factory Capture.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Capture.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Capture',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'name')
    ..aOS(2, _omitFieldNames ? '' : 'dataPath')
    ..a<$fixnum.Int64>(
        3, _omitFieldNames ? '' : 'dataBytes', $pb.PbFieldType.OU6,
        defaultOrMaker: $fixnum.Int64.ZERO)
    ..aD(4, _omitFieldNames ? '' : 'modifiedUnixS')
    ..aOM<CaptureMeta>(5, _omitFieldNames ? '' : 'meta',
        subBuilder: CaptureMeta.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Capture clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Capture copyWith(void Function(Capture) updates) =>
      super.copyWith((message) => updates(message as Capture)) as Capture;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Capture create() => Capture._();
  @$core.override
  Capture createEmptyInstance() => create();
  static $pb.PbList<Capture> createRepeated() => $pb.PbList<Capture>();
  @$core.pragma('dart2js:noInline')
  static Capture getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Capture>(create);
  static Capture? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get name => $_getSZ(0);
  @$pb.TagNumber(1)
  set name($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasName() => $_has(0);
  @$pb.TagNumber(1)
  void clearName() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.String get dataPath => $_getSZ(1);
  @$pb.TagNumber(2)
  set dataPath($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasDataPath() => $_has(1);
  @$pb.TagNumber(2)
  void clearDataPath() => $_clearField(2);

  @$pb.TagNumber(3)
  $fixnum.Int64 get dataBytes => $_getI64(2);
  @$pb.TagNumber(3)
  set dataBytes($fixnum.Int64 value) => $_setInt64(2, value);
  @$pb.TagNumber(3)
  $core.bool hasDataBytes() => $_has(2);
  @$pb.TagNumber(3)
  void clearDataBytes() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.double get modifiedUnixS => $_getN(3);
  @$pb.TagNumber(4)
  set modifiedUnixS($core.double value) => $_setDouble(3, value);
  @$pb.TagNumber(4)
  $core.bool hasModifiedUnixS() => $_has(3);
  @$pb.TagNumber(4)
  void clearModifiedUnixS() => $_clearField(4);

  @$pb.TagNumber(5)
  CaptureMeta get meta => $_getN(4);
  @$pb.TagNumber(5)
  set meta(CaptureMeta value) => $_setField(5, value);
  @$pb.TagNumber(5)
  $core.bool hasMeta() => $_has(4);
  @$pb.TagNumber(5)
  void clearMeta() => $_clearField(5);
  @$pb.TagNumber(5)
  CaptureMeta ensureMeta() => $_ensure(4);
}

/// What a recording's .sigmf-meta says.
class CaptureMeta extends $pb.GeneratedMessage {
  factory CaptureMeta({
    $core.String? datatype,
    $core.double? rateHz,
    $core.double? freqHz,
    $core.String? description,
    $core.double? durationS,
  }) {
    final result = create();
    if (datatype != null) result.datatype = datatype;
    if (rateHz != null) result.rateHz = rateHz;
    if (freqHz != null) result.freqHz = freqHz;
    if (description != null) result.description = description;
    if (durationS != null) result.durationS = durationS;
    return result;
  }

  CaptureMeta._();

  factory CaptureMeta.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory CaptureMeta.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'CaptureMeta',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aOS(1, _omitFieldNames ? '' : 'datatype')
    ..aD(2, _omitFieldNames ? '' : 'rateHz')
    ..aD(3, _omitFieldNames ? '' : 'freqHz')
    ..aOS(4, _omitFieldNames ? '' : 'description')
    ..aD(5, _omitFieldNames ? '' : 'durationS')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  CaptureMeta clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  CaptureMeta copyWith(void Function(CaptureMeta) updates) =>
      super.copyWith((message) => updates(message as CaptureMeta))
          as CaptureMeta;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CaptureMeta create() => CaptureMeta._();
  @$core.override
  CaptureMeta createEmptyInstance() => create();
  static $pb.PbList<CaptureMeta> createRepeated() => $pb.PbList<CaptureMeta>();
  @$core.pragma('dart2js:noInline')
  static CaptureMeta getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<CaptureMeta>(create);
  static CaptureMeta? _defaultInstance;

  @$pb.TagNumber(1)
  $core.String get datatype => $_getSZ(0);
  @$pb.TagNumber(1)
  set datatype($core.String value) => $_setString(0, value);
  @$pb.TagNumber(1)
  $core.bool hasDatatype() => $_has(0);
  @$pb.TagNumber(1)
  void clearDatatype() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.double get rateHz => $_getN(1);
  @$pb.TagNumber(2)
  set rateHz($core.double value) => $_setDouble(1, value);
  @$pb.TagNumber(2)
  $core.bool hasRateHz() => $_has(1);
  @$pb.TagNumber(2)
  void clearRateHz() => $_clearField(2);

  @$pb.TagNumber(3)
  $core.double get freqHz => $_getN(2);
  @$pb.TagNumber(3)
  set freqHz($core.double value) => $_setDouble(2, value);
  @$pb.TagNumber(3)
  $core.bool hasFreqHz() => $_has(2);
  @$pb.TagNumber(3)
  void clearFreqHz() => $_clearField(3);

  @$pb.TagNumber(4)
  $core.String get description => $_getSZ(3);
  @$pb.TagNumber(4)
  set description($core.String value) => $_setString(3, value);
  @$pb.TagNumber(4)
  $core.bool hasDescription() => $_has(3);
  @$pb.TagNumber(4)
  void clearDescription() => $_clearField(4);

  @$pb.TagNumber(5)
  $core.double get durationS => $_getN(4);
  @$pb.TagNumber(5)
  set durationS($core.double value) => $_setDouble(4, value);
  @$pb.TagNumber(5)
  $core.bool hasDurationS() => $_has(4);
  @$pb.TagNumber(5)
  void clearDurationS() => $_clearField(5);
}

class GetSnapshotRequest extends $pb.GeneratedMessage {
  factory GetSnapshotRequest({
    $core.double? t,
  }) {
    final result = create();
    if (t != null) result.t = t;
    return result;
  }

  GetSnapshotRequest._();

  factory GetSnapshotRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory GetSnapshotRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'GetSnapshotRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 't')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetSnapshotRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetSnapshotRequest copyWith(void Function(GetSnapshotRequest) updates) =>
      super.copyWith((message) => updates(message as GetSnapshotRequest))
          as GetSnapshotRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetSnapshotRequest create() => GetSnapshotRequest._();
  @$core.override
  GetSnapshotRequest createEmptyInstance() => create();
  static $pb.PbList<GetSnapshotRequest> createRepeated() =>
      $pb.PbList<GetSnapshotRequest>();
  @$core.pragma('dart2js:noInline')
  static GetSnapshotRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<GetSnapshotRequest>(create);
  static GetSnapshotRequest? _defaultInstance;

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

class GetAircraftHistoryRequest extends $pb.GeneratedMessage {
  factory GetAircraftHistoryRequest({
    $core.int? icao,
    $core.double? t,
  }) {
    final result = create();
    if (icao != null) result.icao = icao;
    if (t != null) result.t = t;
    return result;
  }

  GetAircraftHistoryRequest._();

  factory GetAircraftHistoryRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory GetAircraftHistoryRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'GetAircraftHistoryRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'icao', fieldType: $pb.PbFieldType.OU3)
    ..aD(2, _omitFieldNames ? '' : 't')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetAircraftHistoryRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetAircraftHistoryRequest copyWith(
          void Function(GetAircraftHistoryRequest) updates) =>
      super.copyWith((message) => updates(message as GetAircraftHistoryRequest))
          as GetAircraftHistoryRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetAircraftHistoryRequest create() => GetAircraftHistoryRequest._();
  @$core.override
  GetAircraftHistoryRequest createEmptyInstance() => create();
  static $pb.PbList<GetAircraftHistoryRequest> createRepeated() =>
      $pb.PbList<GetAircraftHistoryRequest>();
  @$core.pragma('dart2js:noInline')
  static GetAircraftHistoryRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<GetAircraftHistoryRequest>(create);
  static GetAircraftHistoryRequest? _defaultInstance;

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

class GetActivityRequest extends $pb.GeneratedMessage {
  factory GetActivityRequest({
    $core.double? from,
  }) {
    final result = create();
    if (from != null) result.from = from;
    return result;
  }

  GetActivityRequest._();

  factory GetActivityRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory GetActivityRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'GetActivityRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'adsb.api.v1'),
      createEmptyInstance: create)
    ..aD(1, _omitFieldNames ? '' : 'from')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetActivityRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetActivityRequest copyWith(void Function(GetActivityRequest) updates) =>
      super.copyWith((message) => updates(message as GetActivityRequest))
          as GetActivityRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetActivityRequest create() => GetActivityRequest._();
  @$core.override
  GetActivityRequest createEmptyInstance() => create();
  static $pb.PbList<GetActivityRequest> createRepeated() =>
      $pb.PbList<GetActivityRequest>();
  @$core.pragma('dart2js:noInline')
  static GetActivityRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<GetActivityRequest>(create);
  static GetActivityRequest? _defaultInstance;

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

class GetHeatmapRequest extends $pb.GeneratedMessage {
  factory GetHeatmapRequest({
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

  GetHeatmapRequest._();

  factory GetHeatmapRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory GetHeatmapRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'GetHeatmapRequest',
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
  GetHeatmapRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  GetHeatmapRequest copyWith(void Function(GetHeatmapRequest) updates) =>
      super.copyWith((message) => updates(message as GetHeatmapRequest))
          as GetHeatmapRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static GetHeatmapRequest create() => GetHeatmapRequest._();
  @$core.override
  GetHeatmapRequest createEmptyInstance() => create();
  static $pb.PbList<GetHeatmapRequest> createRepeated() =>
      $pb.PbList<GetHeatmapRequest>();
  @$core.pragma('dart2js:noInline')
  static GetHeatmapRequest getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<GetHeatmapRequest>(create);
  static GetHeatmapRequest? _defaultInstance;

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

/// The live pipeline. Served with a live input, not by `serve`.
class ReceiverApi {
  final $pb.RpcClient _client;

  ReceiverApi(this._client);

  $async.Future<ReceiverInfo> getReceiverInfo(
          $pb.ClientContext? ctx, GetReceiverInfoRequest request) =>
      _client.invoke<ReceiverInfo>(
          ctx, 'Receiver', 'GetReceiverInfo', request, ReceiverInfo());

  /// The frames decoded, a batch per interval that has any.
  $async.Future<FrameBatch> watchFrames(
          $pb.ClientContext? ctx, WatchFramesRequest request) =>
      _client.invoke<FrameBatch>(
          ctx, 'Receiver', 'WatchFrames', request, FrameBatch());

  /// The aircraft: every one heard in the last 600 s at once, then those that changed, per interval that has any.
  $async.Future<AircraftChanges> watchAircraft(
          $pb.ClientContext? ctx, WatchAircraftRequest request) =>
      _client.invoke<AircraftChanges>(
          ctx, 'Receiver', 'WatchAircraft', request, AircraftChanges());

  /// The input's status, once there is one, then at each change.
  $async.Future<InputStatus> watchInputStatus(
          $pb.ClientContext? ctx, WatchInputStatusRequest request) =>
      _client.invoke<InputStatus>(
          ctx, 'Receiver', 'WatchInputStatus', request, InputStatus());

  /// The spectrum's rows as they're computed. The server computes them only while one of these calls is open.
  $async.Future<SpectrumRow> watchSpectrum(
          $pb.ClientContext? ctx, WatchSpectrumRequest request) =>
      _client.invoke<SpectrumRow>(
          ctx, 'Receiver', 'WatchSpectrum', request, SpectrumRow());

  /// The settings clients set, now and at each change, from any client.
  $async.Future<Settings> watchSettings(
          $pb.ClientContext? ctx, WatchSettingsRequest request) =>
      _client.invoke<Settings>(
          ctx, 'Receiver', 'WatchSettings', request, Settings());

  /// Changes the settings for every client; replies with them as they are after the change.
  $async.Future<Settings> updateSettings(
          $pb.ClientContext? ctx, UpdateSettingsRequest request) =>
      _client.invoke<Settings>(
          ctx, 'Receiver', 'UpdateSettings', request, Settings());

  /// Records one point of the pipeline to a SigMF recording in Settings.capture_dir, from the next block on,
  /// reporting its progress. One capture per point at a time; cancelling stops it, keeping what's written.
  $async.Future<RecordCaptureProgress> recordCapture(
          $pb.ClientContext? ctx, RecordCaptureRequest request) =>
      _client.invoke<RecordCaptureProgress>(
          ctx, 'Receiver', 'RecordCapture', request, RecordCaptureProgress());

  /// The recordings in the capture directory: every .sigmf-data there, whoever wrote it.
  $async.Future<ListCapturesResponse> listCaptures(
          $pb.ClientContext? ctx, ListCapturesRequest request) =>
      _client.invoke<ListCapturesResponse>(
          ctx, 'Receiver', 'ListCaptures', request, ListCapturesResponse());
}

/// The history database. Served with --history-db.
class HistoryApi {
  final $pb.RpcClient _client;

  HistoryApi(this._client);

  /// Every aircraft as it was at a time, with its track up to then.
  $async.Future<Snapshot> getSnapshot(
          $pb.ClientContext? ctx, GetSnapshotRequest request) =>
      _client.invoke<Snapshot>(
          ctx, 'History', 'GetSnapshot', request, Snapshot());

  /// One aircraft's values over time, up to a time.
  $async.Future<AircraftHistory> getAircraftHistory(
          $pb.ClientContext? ctx, GetAircraftHistoryRequest request) =>
      _client.invoke<AircraftHistory>(
          ctx, 'History', 'GetAircraftHistory', request, AircraftHistory());

  /// How many aircraft were heard, per time bucket.
  $async.Future<Activity> getActivity(
          $pb.ClientContext? ctx, GetActivityRequest request) =>
      _client.invoke<Activity>(
          ctx, 'History', 'GetActivity', request, Activity());

  /// How many flights crossed each cell of an area.
  $async.Future<Heatmap> getHeatmap(
          $pb.ClientContext? ctx, GetHeatmapRequest request) =>
      _client.invoke<Heatmap>(ctx, 'History', 'GetHeatmap', request, Heatmap());
}

const $core.bool _omitFieldNames =
    $core.bool.fromEnvironment('protobuf.omit_field_names');
const $core.bool _omitMessageNames =
    $core.bool.fromEnvironment('protobuf.omit_message_names');
