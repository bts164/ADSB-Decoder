part of 'frame_table_page.dart';

/// A capture call in progress, as the top bar shows it.
class _CaptureInProgress {
  _CaptureInProgress(this.request, this.subscription);

  final api.CaptureRequest request;
  final StreamSubscription<api.CaptureResult> subscription;
}

/// The capture call: a dialog that starts one, its progress in the top bar, and its result in a SnackBar.
extension _Capture on _FrameTablePageState {
  Widget _buildCaptureControl() {
    return ValueListenableBuilder(
      valueListenable: _capture,
      builder: (context, capture, _) {
        if (capture == null) {
          return IconButton(
            icon: const Icon(Icons.fiber_manual_record_outlined),
            tooltip: 'Capture the input or the envelope to a file on the server',
            onPressed: _showCaptureDialog,
          );
        }
        return Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            SizedBox.square(
              dimension: 16,
              // In wall-clock time, while the capture covers stream time: a file played faster than real time
              // finishes early.
              child: TweenAnimationBuilder<double>(
                key: ObjectKey(capture),
                tween: Tween(begin: 0, end: 1),
                duration: Duration(milliseconds: (capture.request.durationS * 1000).round()),
                builder: (context, done, _) => CircularProgressIndicator(value: done, strokeWidth: 2),
              ),
            ),
            const SizedBox(width: 6),
            Text('Capturing ${_capturePointLabel(capture.request.point)}'),
            IconButton(
              icon: const Icon(Icons.stop),
              tooltip: 'Stop the capture, keeping what it has written',
              onPressed: _stopCapture,
            ),
          ],
        );
      },
    );
  }

  String _capturePointLabel(api.CaptureRequest_Point point) =>
      point == api.CaptureRequest_Point.ENVELOPE ? 'envelope' : 'input IQ';

  Future<void> _showCaptureDialog() async {
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    // Not disposed: the dialog's closing animation still uses them after it returns.
    final name = TextEditingController(
      text: 'capture_${now.year}${two(now.month)}${two(now.day)}_${two(now.hour)}${two(now.minute)}${two(now.second)}',
    );
    final duration = TextEditingController(text: '1');
    var point = api.CaptureRequest_Point.INPUT;
    final maxS = _captureMaxS;
    final request = await showDialog<api.CaptureRequest>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setDialogState) {
          final durationS = double.tryParse(duration.text);
          final durationOk = durationS != null && durationS > 0 && durationS <= maxS;
          // The server's check: a bare file name, which it puts in its --capture-dir.
          final nameOk = name.text.isNotEmpty &&
              !name.text.startsWith('.') &&
              !name.text.contains('/') &&
              !name.text.contains('\u0000');
          return AlertDialog(
            title: const Text('Capture'),
            content: SizedBox(
              width: 360,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  SegmentedButton<api.CaptureRequest_Point>(
                    segments: const [
                      ButtonSegment(
                        value: api.CaptureRequest_Point.INPUT,
                        label: Text('Input IQ'),
                        tooltip: 'As the source delivers it, replayable with adsb file',
                      ),
                      ButtonSegment(
                        value: api.CaptureRequest_Point.ENVELOPE,
                        label: Text('Envelope'),
                        tooltip: "The demodulator's resampled magnitude, as rf32",
                      ),
                    ],
                    selected: {point},
                    onSelectionChanged: (selected) => setDialogState(() => point = selected.first),
                  ),
                  const SizedBox(height: 12),
                  TextField(
                    controller: duration,
                    keyboardType: const TextInputType.numberWithOptions(decimal: true),
                    decoration: InputDecoration(
                      labelText: 'Duration (s)',
                      errorText: durationOk ? null : 'more than 0, up to ${maxS.toStringAsFixed(0)}',
                    ),
                    onChanged: (_) => setDialogState(() {}),
                  ),
                  const SizedBox(height: 12),
                  TextField(
                    controller: name,
                    decoration: InputDecoration(
                      labelText: 'File name',
                      helperText: "<name>.sigmf-data and .sigmf-meta, in the server's --capture-dir",
                      errorText: nameOk ? null : 'a file name, not starting with "."',
                    ),
                    onChanged: (_) => setDialogState(() {}),
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
              FilledButton(
                onPressed: durationOk && nameOk
                    ? () => Navigator.pop(
                        context,
                        api.CaptureRequest(point: point, durationS: durationS, name: name.text),
                      )
                    : null,
                child: const Text('Capture'),
              ),
            ],
          );
        },
      ),
    );
    final client = _client;
    if (request == null || client == null || !mounted || _capture.value != null) return;
    late final _CaptureInProgress capture;
    void ended(String message) {
      if (!identical(_capture.value, capture)) return;
      _capture.value = null;
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
    }

    capture = _CaptureInProgress(
      request,
      client.capture(request).listen(
        (r) {
          final seconds = (r.samples.toDouble() / r.rateHz).toStringAsFixed(3);
          final missing = r.missing.isZero ? '' : ', ${r.missing} samples missing';
          ended('Captured ${r.samples} samples ($seconds s, ${r.datatype}) to ${r.dataPath}$missing');
        },
        onError: (Object e) => ended('Capture failed: ${e is ApiException ? e.message : e}'),
        cancelOnError: true,
      ),
    );
    _capture.value = capture;
  }

  /// Cancels the call, which stops the capture on the server; what it has written stays there.
  void _stopCapture() {
    final capture = _capture.value;
    if (capture == null) return;
    capture.subscription.cancel();
    _capture.value = null;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text("Capture stopped; what it wrote is in the server's --capture-dir")),
    );
  }

  /// Forgets the capture with the connection, which stops it on the server.
  void _dropCapture() {
    _capture.value?.subscription.cancel();
    _capture.value = null;
  }
}
