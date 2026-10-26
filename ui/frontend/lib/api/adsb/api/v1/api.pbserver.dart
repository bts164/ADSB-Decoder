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

import 'package:protobuf/protobuf.dart' as $pb;

import 'api.pb.dart' as $0;
import 'api.pbjson.dart';

export 'api.pb.dart';

abstract class ReceiverServiceBase extends $pb.GeneratedService {
  $async.Future<$0.ReceiverInfo> getReceiverInfo(
      $pb.ServerContext ctx, $0.GetReceiverInfoRequest request);
  $async.Future<$0.FrameBatch> watchFrames(
      $pb.ServerContext ctx, $0.WatchFramesRequest request);
  $async.Future<$0.AircraftChanges> watchAircraft(
      $pb.ServerContext ctx, $0.WatchAircraftRequest request);
  $async.Future<$0.InputStatus> watchInputStatus(
      $pb.ServerContext ctx, $0.WatchInputStatusRequest request);
  $async.Future<$0.SpectrumRow> watchSpectrum(
      $pb.ServerContext ctx, $0.WatchSpectrumRequest request);
  $async.Future<$0.Settings> watchSettings(
      $pb.ServerContext ctx, $0.WatchSettingsRequest request);
  $async.Future<$0.Settings> updateSettings(
      $pb.ServerContext ctx, $0.UpdateSettingsRequest request);
  $async.Future<$0.RecordCaptureProgress> recordCapture(
      $pb.ServerContext ctx, $0.RecordCaptureRequest request);
  $async.Future<$0.ListCapturesResponse> listCaptures(
      $pb.ServerContext ctx, $0.ListCapturesRequest request);

  $pb.GeneratedMessage createRequest($core.String methodName) {
    switch (methodName) {
      case 'GetReceiverInfo':
        return $0.GetReceiverInfoRequest();
      case 'WatchFrames':
        return $0.WatchFramesRequest();
      case 'WatchAircraft':
        return $0.WatchAircraftRequest();
      case 'WatchInputStatus':
        return $0.WatchInputStatusRequest();
      case 'WatchSpectrum':
        return $0.WatchSpectrumRequest();
      case 'WatchSettings':
        return $0.WatchSettingsRequest();
      case 'UpdateSettings':
        return $0.UpdateSettingsRequest();
      case 'RecordCapture':
        return $0.RecordCaptureRequest();
      case 'ListCaptures':
        return $0.ListCapturesRequest();
      default:
        throw $core.ArgumentError('Unknown method: $methodName');
    }
  }

  $async.Future<$pb.GeneratedMessage> handleCall($pb.ServerContext ctx,
      $core.String methodName, $pb.GeneratedMessage request) {
    switch (methodName) {
      case 'GetReceiverInfo':
        return getReceiverInfo(ctx, request as $0.GetReceiverInfoRequest);
      case 'WatchFrames':
        return watchFrames(ctx, request as $0.WatchFramesRequest);
      case 'WatchAircraft':
        return watchAircraft(ctx, request as $0.WatchAircraftRequest);
      case 'WatchInputStatus':
        return watchInputStatus(ctx, request as $0.WatchInputStatusRequest);
      case 'WatchSpectrum':
        return watchSpectrum(ctx, request as $0.WatchSpectrumRequest);
      case 'WatchSettings':
        return watchSettings(ctx, request as $0.WatchSettingsRequest);
      case 'UpdateSettings':
        return updateSettings(ctx, request as $0.UpdateSettingsRequest);
      case 'RecordCapture':
        return recordCapture(ctx, request as $0.RecordCaptureRequest);
      case 'ListCaptures':
        return listCaptures(ctx, request as $0.ListCapturesRequest);
      default:
        throw $core.ArgumentError('Unknown method: $methodName');
    }
  }

  $core.Map<$core.String, $core.dynamic> get $json => ReceiverServiceBase$json;
  $core.Map<$core.String, $core.Map<$core.String, $core.dynamic>>
      get $messageJson => ReceiverServiceBase$messageJson;
}

abstract class HistoryServiceBase extends $pb.GeneratedService {
  $async.Future<$0.Snapshot> getSnapshot(
      $pb.ServerContext ctx, $0.GetSnapshotRequest request);
  $async.Future<$0.AircraftHistory> getAircraftHistory(
      $pb.ServerContext ctx, $0.GetAircraftHistoryRequest request);
  $async.Future<$0.Activity> getActivity(
      $pb.ServerContext ctx, $0.GetActivityRequest request);
  $async.Future<$0.Heatmap> getHeatmap(
      $pb.ServerContext ctx, $0.GetHeatmapRequest request);

  $pb.GeneratedMessage createRequest($core.String methodName) {
    switch (methodName) {
      case 'GetSnapshot':
        return $0.GetSnapshotRequest();
      case 'GetAircraftHistory':
        return $0.GetAircraftHistoryRequest();
      case 'GetActivity':
        return $0.GetActivityRequest();
      case 'GetHeatmap':
        return $0.GetHeatmapRequest();
      default:
        throw $core.ArgumentError('Unknown method: $methodName');
    }
  }

  $async.Future<$pb.GeneratedMessage> handleCall($pb.ServerContext ctx,
      $core.String methodName, $pb.GeneratedMessage request) {
    switch (methodName) {
      case 'GetSnapshot':
        return getSnapshot(ctx, request as $0.GetSnapshotRequest);
      case 'GetAircraftHistory':
        return getAircraftHistory(ctx, request as $0.GetAircraftHistoryRequest);
      case 'GetActivity':
        return getActivity(ctx, request as $0.GetActivityRequest);
      case 'GetHeatmap':
        return getHeatmap(ctx, request as $0.GetHeatmapRequest);
      default:
        throw $core.ArgumentError('Unknown method: $methodName');
    }
  }

  $core.Map<$core.String, $core.dynamic> get $json => HistoryServiceBase$json;
  $core.Map<$core.String, $core.Map<$core.String, $core.dynamic>>
      get $messageJson => HistoryServiceBase$messageJson;
}
