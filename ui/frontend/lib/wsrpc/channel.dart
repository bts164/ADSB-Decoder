import 'dart:async';
import 'dart:typed_data';

import 'package:protobuf/protobuf.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

// Prefixed: the generated Error would hide dart:core's.
import '../api/wsrpc/v1/wsrpc.pb.dart' as wsrpc;

/// A call that failed: the server's Error, a reply that didn't parse, or the connection closing first.
class RpcException implements Exception {
  RpcException(this.code, this.message);

  /// The server's code, or null when the connection closed first.
  final wsrpc.Error_Code? code;
  final String message;

  @override
  String toString() => 'RpcException(${code?.name ?? 'disconnected'}): $message';
}

/// A unary call in progress: a [Future] of its reply, which [cancel] fails with `CANCELLED`.
class UnaryCall<T> implements Future<T> {
  UnaryCall._(this._future, this._cancel);

  final Future<T> _future;
  final void Function() _cancel;

  /// Cancels the call on the server, if it hasn't ended yet.
  void cancel() => _cancel();

  @override
  Stream<T> asStream() => _future.asStream();

  @override
  Future<T> catchError(Function onError, {bool Function(Object error)? test}) =>
      _future.catchError(onError, test: test);

  @override
  Future<R> then<R>(FutureOr<R> Function(T value) onValue, {Function? onError}) =>
      _future.then(onValue, onError: onError);

  @override
  Future<T> timeout(Duration timeLimit, {FutureOr<T> Function()? onTimeout}) =>
      _future.timeout(timeLimit, onTimeout: onTimeout);

  @override
  Future<T> whenComplete(FutureOr<void> Function() action) => _future.whenComplete(action);
}

/// What waits for a call: each reply as it arrives, then its end, with the error it ended with, if any.
class _Call {
  _Call(this.reply, this.end);

  final void Function(List<int> bytes) reply;
  final void Function(RpcException? error) end;
}

/// The wsrpc runtime (doc/design/ui_api.md): one WebSocket that carries calls to any of the services the server
/// serves. The generated clients make their calls through it. A disconnect fails every call in progress, and
/// every later one.
class WsRpcChannel {
  WsRpcChannel._(this._socket) {
    _sub = _socket.stream.listen(
      _onData,
      onError: (Object e) => _fail('$e'),
      onDone: () => _fail('the server closed the connection'),
    );
  }

  /// An older server, or something else on the port, may never answer the hello.
  static const _helloTimeout = Duration(seconds: 5);

  /// Connects to the server at [uri] and says hello with the [services] the caller knows, by their full names
  /// (the clients' `service`). Throws if it can't connect or the server doesn't answer.
  static Future<WsRpcChannel> connect(Uri uri, List<String> services) async {
    final socket = WebSocketChannel.connect(uri);
    await socket.ready;
    final channel = WsRpcChannel._(socket);
    channel._send(wsrpc.ClientFrame(hello: wsrpc.Hello(services: services)));
    try {
      channel.services = await channel._hello.future.timeout(
        _helloTimeout,
        onTimeout: () => throw RpcException(null, 'the server answered no hello in ${_helloTimeout.inSeconds} s'),
      );
    } catch (_) {
      await channel.close();
      rethrow;
    }
    return channel;
  }

  final WebSocketChannel _socket;
  late final StreamSubscription<dynamic> _sub;
  final _hello = Completer<Set<String>>();
  int _lastId = 0;
  final Map<int, _Call> _calls = {};
  bool _closed = false;
  final Completer<void> _done = Completer();

  /// The services the server serves, by their full names, from its hello. A call to any other is
  /// `UNIMPLEMENTED`.
  late final Set<String> services;

  /// Completes when the connection closes, from either end.
  Future<void> get done => _done.future;

  /// Closes the connection, failing the calls in progress.
  Future<void> close() async {
    _fail('closed by the client');
    await _sub.cancel();
    await _socket.sink.close();
  }

  UnaryCall<R> unary<R>(String method, GeneratedMessage request, R Function(List<int>) parse) {
    final completer = Completer<R>();
    late R reply;
    var replied = false;
    final id = _start(
      method,
      request,
      _Call(
        (bytes) {
          reply = parse(bytes);
          replied = true;
        },
        (error) => _complete(completer, error, replied, () => reply),
      ),
    );
    return UnaryCall._(completer.future, () => _cancelUnary(id, completer));
  }

  Stream<R> serverStreaming<R>(String method, GeneratedMessage request, R Function(List<int>) parse) {
    late final StreamController<R> controller;
    int? id;
    controller = StreamController(
      onListen: () => id = _start(method, request, _streamCall(controller, parse)),
      onCancel: () => _cancel(id),
    );
    return controller.stream;
  }

  UnaryCall<R> clientStreaming<R>(String method, Stream<GeneratedMessage> requests, R Function(List<int>) parse) {
    final completer = Completer<R>();
    late R reply;
    var replied = false;
    StreamSubscription<GeneratedMessage>? sending;
    final id = _start(
      method,
      null,
      _Call(
        (bytes) {
          reply = parse(bytes);
          replied = true;
        },
        (error) {
          sending?.cancel();
          _complete(completer, error, replied, () => reply);
        },
      ),
    );
    if (_calls.containsKey(id)) sending = _sendRequests(id, requests);
    return UnaryCall._(completer.future, () {
      sending?.cancel();
      _cancelUnary(id, completer);
    });
  }

  Stream<R> bidi<R>(String method, Stream<GeneratedMessage> requests, R Function(List<int>) parse) {
    late final StreamController<R> controller;
    int? id;
    StreamSubscription<GeneratedMessage>? sending;
    controller = StreamController(
      onListen: () {
        final call = _streamCall(controller, parse);
        id = _start(
          method,
          null,
          _Call(call.reply, (error) {
            sending?.cancel();
            call.end(error);
          }),
        );
        if (_calls.containsKey(id)) sending = _sendRequests(id!, requests);
      },
      onCancel: () {
        sending?.cancel();
        _cancel(id);
      },
    );
    return controller.stream;
  }

  /// Sends a call's first frame, with [request] and `end` unless the client streams, and returns its id. A call
  /// on a closed connection ends at once.
  int _start(String method, GeneratedMessage? request, _Call call) {
    final id = ++_lastId;
    if (_closed) {
      call.end(RpcException(null, 'not connected'));
      return id;
    }
    _calls[id] = call;
    final frame = wsrpc.ClientFrame(id: id, method: method);
    if (request != null) {
      frame
        ..request = request.writeToBuffer()
        ..end = true;
    }
    _send(frame);
    return id;
  }

  _Call _streamCall<R>(StreamController<R> controller, R Function(List<int>) parse) => _Call(
        (bytes) => controller.add(parse(bytes)),
        (error) {
          if (error != null) controller.addError(error);
          controller.close();
        },
      );

  /// Sends each of a client stream's requests while its call is in progress, then `end`. Requests that fail
  /// cancel the call.
  StreamSubscription<GeneratedMessage> _sendRequests(int id, Stream<GeneratedMessage> requests) => requests.listen(
        (request) {
          if (_calls.containsKey(id)) _send(wsrpc.ClientFrame(id: id, request: request.writeToBuffer()));
        },
        onError: (Object e) {
          final call = _calls[id];
          _cancel(id);
          call?.end(RpcException(wsrpc.Error_Code.CANCELLED, 'the requests failed: $e'));
        },
        onDone: () {
          if (_calls.containsKey(id)) _send(wsrpc.ClientFrame(id: id, end: true));
        },
        cancelOnError: true,
      );

  void _complete<R>(Completer<R> completer, RpcException? error, bool replied, R Function() reply) {
    if (completer.isCompleted) return;
    if (error != null) {
      completer.completeError(error);
    } else if (replied) {
      completer.complete(reply());
    } else {
      completer.completeError(RpcException(wsrpc.Error_Code.INTERNAL, 'the call ended without its reply'));
    }
  }

  /// Forgets the call, so a frame of its that's already on its way is dropped, and tells the server.
  void _cancel(int? id) {
    if (id != null && _calls.remove(id) != null && !_closed) _send(wsrpc.ClientFrame(id: id, cancel: true));
  }

  void _cancelUnary<R>(int id, Completer<R> completer) {
    if (!_calls.containsKey(id)) return;
    _cancel(id);
    // A caller that cancels may no longer be awaiting the call: this error mustn't be reported as unhandled.
    completer.future.ignore();
    completer.completeError(RpcException(wsrpc.Error_Code.CANCELLED, 'cancelled by the client'));
  }

  void _send(wsrpc.ClientFrame frame) => _socket.sink.add(frame.writeToBuffer());

  void _onData(dynamic event) {
    // Binary messages arrive as a Uint8List natively, and may be a ByteBuffer on the web.
    final Uint8List bytes;
    if (event is Uint8List) {
      bytes = event;
    } else if (event is ByteBuffer) {
      bytes = event.asUint8List();
    } else {
      _fail('the server sent a text message');
      close();
      return;
    }
    final wsrpc.ServerFrame frame;
    try {
      frame = wsrpc.ServerFrame.fromBuffer(bytes);
    } on InvalidProtocolBufferException catch (e) {
      _fail("the server sent a message that isn't a ServerFrame: $e");
      close();
      return;
    }
    if (frame.id == 0) {
      if (frame.hasHello() && !_hello.isCompleted) _hello.complete({...frame.hello.services});
      return;
    }
    // Not there once it has ended, or was cancelled here.
    final call = _calls[frame.id];
    if (call == null) return;
    if (frame.hasReply()) {
      try {
        call.reply(frame.reply);
      } on InvalidProtocolBufferException catch (e) {
        _cancel(frame.id);
        call.end(RpcException(wsrpc.Error_Code.INTERNAL, "a reply that doesn't parse: $e"));
        return;
      }
    }
    if (frame.end) {
      _calls.remove(frame.id);
      final error = frame.hasError() ? frame.error : null;
      call.end(error == null || error.code == wsrpc.Error_Code.CODE_UNSPECIFIED
          ? null
          : RpcException(error.code, error.message));
    }
  }

  void _fail(String reason) {
    if (_closed) return;
    _closed = true;
    final e = RpcException(null, reason);
    if (!_hello.isCompleted) _hello.completeError(e);
    final calls = [..._calls.values];
    _calls.clear();
    for (final call in calls) {
      call.end(e);
    }
    _done.complete();
  }
}
