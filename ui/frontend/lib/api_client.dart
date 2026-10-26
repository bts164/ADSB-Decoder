import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

// Prefixed: the generated Error would hide dart:core's.
import 'api/adsb/api/v1/api.pb.dart' as api;

/// A call that failed: the server's Error, or the connection closing first.
class ApiException implements Exception {
  /// The server's code, or null when the connection closed first.
  final api.Error_Code? code;
  final String message;

  ApiException(this.code, this.message);

  @override
  String toString() => 'ApiException(${code?.name ?? 'disconnected'}): $message';
}

/// The UI API's client (doc/design/ui_api.md): one WebSocket, and a method per call, like a gRPC stub.
/// A disconnect fails every call in progress, and every later one.
class ApiClient {
  ApiClient._(this._channel) {
    _sub = _channel.stream.listen(
      _onData,
      onError: (Object e) => _fail('$e'),
      onDone: () => _fail('the server closed the connection'),
    );
  }

  /// Connects to the server at [uri], throwing if it can't.
  static Future<ApiClient> connect(Uri uri) async {
    final channel = WebSocketChannel.connect(uri);
    await channel.ready;
    return ApiClient._(channel);
  }

  final WebSocketChannel _channel;
  late final StreamSubscription<dynamic> _sub;
  int _lastId = 0;
  final Map<int, Completer<api.ServerMessage>> _unary = {};
  final Map<int, StreamController<api.ServerMessage>> _streams = {};
  bool _closed = false;
  final Completer<void> _done = Completer();

  /// Completes when the connection closes, from either end.
  Future<void> get done => _done.future;

  /// The server's info now, and again each time it changes, such as when a client sets the capture directory.
  /// Starts when listened to; cancelling the subscription cancels the call.
  Stream<api.ServerInfo> serverInfo() =>
      _stream(api.ClientMessage(serverInfo: api.ServerInfoRequest())).map((m) => m.serverInfo);

  /// Sets the capture directory for every client, or turns captures off with an empty [dir]. Replies with the
  /// directory as the server resolved it.
  Future<String> setCaptureDir(String dir) async =>
      (await _call(api.ClientMessage(captureDir: api.CaptureDirRequest(dir: dir)))).captureDir.dir;

  /// The captures in the capture directory, newest first.
  Future<api.CaptureFiles> captureFiles() async =>
      (await _call(api.ClientMessage(captureFiles: api.CaptureFilesRequest()))).captureFiles;

  /// Starts when listened to; cancelling the subscription cancels the call.
  Stream<api.Update> subscribe(api.SubscribeRequest request) =>
      _stream(api.ClientMessage(subscribe: request)).map((m) => m.update);

  /// The spectrum's rows as the server computes them, which it does only while a spectrum call is in progress.
  /// Starts when listened to; cancelling the subscription cancels the call.
  Stream<api.SpectrumRow> spectrum(api.SpectrumRequest request) =>
      _stream(api.ClientMessage(spectrum: request)).map((m) => m.spectrum);

  /// A capture of one point to a file on the server, which replies when it is done: one result, then the end.
  /// Starts when listened to; cancelling the subscription stops the capture, keeping what it has written.
  Stream<api.CaptureResult> capture(api.CaptureRequest request) =>
      _stream(api.ClientMessage(capture: request)).map((m) => m.capture);

  Future<api.Snapshot> snapshot(api.SnapshotRequest request) async =>
      (await _call(api.ClientMessage(snapshot: request))).snapshot;

  Future<api.AircraftHistory> aircraftHistory(api.AircraftHistoryRequest request) async =>
      (await _call(api.ClientMessage(aircraftHistory: request))).aircraftHistory;

  Future<api.Activity> activity(api.ActivityRequest request) async =>
      (await _call(api.ClientMessage(activity: request))).activity;

  Future<api.Heatmap> heatmap(api.HeatmapRequest request) async =>
      (await _call(api.ClientMessage(heatmap: request))).heatmap;

  /// Closes the connection, failing the calls in progress.
  Future<void> close() async {
    _fail('closed by the client');
    await _sub.cancel();
    await _channel.sink.close();
  }

  Future<api.ServerMessage> _call(api.ClientMessage msg) {
    if (_closed) return Future.error(ApiException(null, 'not connected'));
    final id = ++_lastId;
    final completer = Completer<api.ServerMessage>();
    _unary[id] = completer;
    _send(msg..id = id);
    return completer.future;
  }

  Stream<api.ServerMessage> _stream(api.ClientMessage msg) {
    late final StreamController<api.ServerMessage> controller;
    int? id;
    controller = StreamController(
      onListen: () {
        if (_closed) {
          controller.addError(ApiException(null, 'not connected'));
          controller.close();
          return;
        }
        id = ++_lastId;
        _streams[id!] = controller;
        _send(msg..id = id!);
      },
      onCancel: () {
        // Not in _streams once it has ended: there's nothing to cancel then.
        if (id != null && _streams.remove(id) != null && !_closed) {
          _send(api.ClientMessage(id: id, cancel: api.Cancel()));
        }
      },
    );
    return controller.stream;
  }

  void _send(api.ClientMessage msg) => _channel.sink.add(msg.writeToBuffer());

  void _onData(dynamic event) {
    // Binary messages arrive as a Uint8List natively, and may be a ByteBuffer on the web.
    final Uint8List bytes;
    if (event is Uint8List) {
      bytes = event;
    } else if (event is ByteBuffer) {
      bytes = event.asUint8List();
    } else {
      debugPrint('ignoring a websocket message that isn\'t binary: ${event.runtimeType}');
      return;
    }
    final api.ServerMessage msg;
    try {
      msg = api.ServerMessage.fromBuffer(bytes);
    } catch (e) {
      debugPrint('ignoring a websocket message that isn\'t a ServerMessage: $e');
      return;
    }
    final error = msg.hasError() ? ApiException(msg.error.code, msg.error.message) : null;
    final completer = _unary.remove(msg.id);
    if (completer != null) {
      error != null ? completer.completeError(error) : completer.complete(msg);
      return;
    }
    // A stream cancelled here isn't in _streams, and its end is ignored.
    final controller = _streams[msg.id];
    if (controller == null) return;
    if (error != null) {
      controller.addError(error);
    } else if (msg.whichBody() != api.ServerMessage_Body.notSet) {
      controller.add(msg);
    }
    if (msg.end) {
      _streams.remove(msg.id);
      controller.close();
    }
  }

  void _fail(String reason) {
    if (_closed) return;
    _closed = true;
    final e = ApiException(null, reason);
    final unary = [..._unary.values];
    final streams = [..._streams.values];
    _unary.clear();
    _streams.clear();
    for (final c in unary) {
      c.completeError(e);
    }
    for (final c in streams) {
      c.addError(e);
      c.close();
    }
    _done.complete();
  }
}
