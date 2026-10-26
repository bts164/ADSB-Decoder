part of 'frame_table_page.dart';

/// A capture call in progress, as the top bar shows it.
class _CaptureInProgress {
  _CaptureInProgress(this.request);

  final api.RecordCaptureRequest request;
  late final StreamSubscription<api.RecordCaptureProgress> subscription;
  // The server's last report, null before the first.
  final progress = ValueNotifier<api.RecordCaptureProgress?>(null);
}

/// The capture call: a dialog that starts one, its progress in the top bar, and its result in a SnackBar. Beside
/// it, the Captures dialog, which lists the capture directory's files and sets the directory.
extension _Capture on _FrameTablePageState {
  /// Nothing for a server with no live input to capture: one that doesn't serve Receiver.
  Widget _buildCaptureControl() {
    return ValueListenableBuilder(
      valueListenable: _settings,
      builder: (context, settings, _) {
        if (settings == null || _receiver == null) return const SizedBox.shrink();
        final on = settings.captureDir.isNotEmpty;
        return Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            const SizedBox(width: 6),
            IconButton(
              icon: Icon(on ? Icons.folder_open_outlined : Icons.folder_off_outlined),
              tooltip: on
                  ? 'Captures, in ${settings.captureDir} on the server'
                  : 'Captures are off: set a directory on the server to turn them on',
              onPressed: _showCapturesDialog,
            ),
            _buildCaptureCall(on),
          ],
        );
      },
    );
  }

  Widget _buildCaptureCall(bool on) {
    return ValueListenableBuilder(
      valueListenable: _capture,
      builder: (context, capture, _) {
        if (capture == null) {
          return IconButton(
            icon: const Icon(Icons.fiber_manual_record_outlined),
            tooltip: on
                ? 'Capture the input or the envelope to a file on the server'
                : 'Captures are off: set a capture directory first',
            onPressed: on ? _showCaptureDialog : null,
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
            ValueListenableBuilder(
              valueListenable: capture.progress,
              builder: (context, progress, _) {
                final label = 'Capturing ${_capturePointLabel(capture.request.point)}';
                if (progress == null) return Text(label);
                final missing = progress.missing.isZero ? '' : ', ${progress.missing} missing';
                return Text('$label: ${progress.samples} samples$missing');
              },
            ),
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

  String _capturePointLabel(api.CapturePoint point) => point == api.CapturePoint.ENVELOPE ? 'envelope' : 'input IQ';

  Future<void> _showCaptureDialog() async {
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    // Not disposed: the dialog's closing animation still uses them after it returns.
    final name = TextEditingController(
      text: 'capture_${now.year}${two(now.month)}${two(now.day)}_${two(now.hour)}${two(now.minute)}${two(now.second)}',
    );
    final duration = TextEditingController(text: '1');
    var point = api.CapturePoint.INPUT;
    final info = _receiverInfo;
    final settings = _settings.value;
    if (info == null || settings == null) return;
    final maxS = info.maxCaptureS;
    final request = await showDialog<api.RecordCaptureRequest>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setDialogState) {
          final durationS = double.tryParse(duration.text);
          final durationOk = durationS != null && durationS > 0 && durationS <= maxS;
          // The server's check: a bare file name, which it puts in its capture directory.
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
                  SegmentedButton<api.CapturePoint>(
                    segments: const [
                      ButtonSegment(
                        value: api.CapturePoint.INPUT,
                        label: Text('Input IQ'),
                        tooltip: 'As the source delivers it, replayable with adsb file',
                      ),
                      ButtonSegment(
                        value: api.CapturePoint.ENVELOPE,
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
                      helperText: '<name>.sigmf-data and .sigmf-meta, in ${settings.captureDir}',
                      helperMaxLines: 2,
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
                        api.RecordCaptureRequest(point: point, durationS: durationS, name: name.text),
                      )
                    : null,
                child: const Text('Capture'),
              ),
            ],
          );
        },
      ),
    );
    final receiver = _receiver;
    if (request == null || receiver == null || !mounted || _capture.value != null) return;
    final capture = _CaptureInProgress(request);
    void ended(String message) {
      if (!identical(_capture.value, capture)) return;
      _capture.value = null;
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
    }

    capture.subscription = receiver.recordCapture(request).listen(
      (p) {
        // Only the last report carries the recording.
        if (!p.hasCapture()) {
          capture.progress.value = p;
          return;
        }
        final c = p.capture;
        final how = c.hasMeta() ? ' (${c.meta.durationS.toStringAsFixed(3)} s, ${c.meta.datatype})' : '';
        final missing = p.missing.isZero ? '' : ', ${p.missing} samples missing';
        ended('Captured ${p.samples} samples$how to ${c.dataPath}$missing');
      },
      onError: (Object e) => ended('Capture failed: ${e is RpcException ? e.message : e}'),
      onDone: () => ended('The capture ended without its result'),
      cancelOnError: true,
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
      const SnackBar(content: Text('Capture stopped; what it wrote is in the capture directory')),
    );
  }

  Future<void> _showCapturesDialog() async {
    final receiver = _receiver;
    if (receiver == null) return;
    await showDialog<void>(
      context: context,
      builder: (context) => _CapturesDialog(client: receiver, settings: _settings),
    );
  }

  /// Forgets the capture with the connection, which stops it on the server.
  void _dropCapture() {
    _capture.value?.subscription.cancel();
    _capture.value = null;
  }
}

/// The capture directory's files, newest first, and the directory itself, which this sets for every client. It
/// follows the settings watch, so another client's change shows here too.
class _CapturesDialog extends StatefulWidget {
  const _CapturesDialog({required this.client, required this.settings});

  final ReceiverClient client;
  final ValueListenable<api.Settings?> settings;

  @override
  State<_CapturesDialog> createState() => _CapturesDialogState();
}

class _CapturesDialogState extends State<_CapturesDialog> {
  final _dir = TextEditingController();
  // Whether the field holds the user's edit, which a change from the server mustn't overwrite.
  bool _dirEdited = false;
  String? _setError;
  bool _setting = false;
  // The listing, and the directory it is of; a newer listing supersedes one still in flight.
  api.ListCapturesResponse? _files;
  String? _listedDir;
  String? _listError;
  int _listGeneration = 0;

  @override
  void initState() {
    super.initState();
    _dir.text = widget.settings.value?.captureDir ?? '';
    widget.settings.addListener(_onSettings);
    _refresh();
  }

  @override
  void dispose() {
    widget.settings.removeListener(_onSettings);
    _dir.dispose();
    super.dispose();
  }

  void _onSettings() {
    final settings = widget.settings.value;
    // Disconnected: the client's calls would fail.
    if (settings == null) {
      Navigator.of(context).pop();
      return;
    }
    if (!_dirEdited) _dir.text = settings.captureDir;
    if (settings.captureDir != _listedDir) _refresh();
    setState(() {});
  }

  Future<void> _refresh() async {
    final generation = ++_listGeneration;
    final dir = widget.settings.value?.captureDir ?? '';
    if (dir.isEmpty) {
      setState(() {
        _files = null;
        _listedDir = '';
        _listError = null;
      });
      return;
    }
    setState(() => _listedDir = dir);
    try {
      final files = await widget.client.listCaptures(api.ListCapturesRequest());
      if (!mounted || generation != _listGeneration) return;
      setState(() {
        _files = files;
        _listError = null;
      });
    } on RpcException catch (e) {
      if (!mounted || generation != _listGeneration) return;
      setState(() {
        _files = null;
        _listError = e.message;
      });
    }
  }

  /// [dir] empty turns captures off. The settings that follow refresh the listing.
  Future<void> _set(String dir) async {
    setState(() {
      _setting = true;
      _setError = null;
    });
    try {
      final settings = await widget.client.updateSettings(api.UpdateSettingsRequest(captureDir: dir));
      if (!mounted) return;
      _dirEdited = false;
      _dir.text = settings.captureDir;
    } on RpcException catch (e) {
      if (!mounted) return;
      _setError = e.message;
    }
    setState(() => _setting = false);
  }

  @override
  Widget build(BuildContext context) {
    final current = widget.settings.value?.captureDir ?? '';
    final typed = _dir.text.trim();
    return AlertDialog(
      title: const Text('Captures'),
      content: SizedBox(
        width: 640,
        height: 480,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: TextField(
                    controller: _dir,
                    decoration: InputDecoration(
                      labelText: 'Capture directory on the server',
                      helperText: current.isEmpty ? 'Captures are off' : 'Shared by every client',
                      errorText: _setError,
                      errorMaxLines: 2,
                    ),
                    onChanged: (_) => setState(() => _dirEdited = true),
                    onSubmitted: (_) {
                      if (typed.isNotEmpty && !_setting) _set(typed);
                    },
                  ),
                ),
                const SizedBox(width: 8),
                Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: FilledButton(
                    onPressed: typed.isNotEmpty && typed != current && !_setting ? () => _set(typed) : null,
                    child: Text(current.isEmpty ? 'Turn on' : 'Change'),
                  ),
                ),
                const SizedBox(width: 8),
                Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: OutlinedButton(
                    onPressed: current.isNotEmpty && !_setting ? () => _set('') : null,
                    child: const Text('Turn off'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(child: Text(_summary(current), style: Theme.of(context).textTheme.titleSmall)),
                IconButton(
                  icon: const Icon(Icons.refresh),
                  tooltip: 'List the files again',
                  onPressed: current.isEmpty ? null : _refresh,
                ),
              ],
            ),
            const Divider(height: 1),
            Expanded(child: _buildList()),
          ],
        ),
      ),
      actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Close'))],
    );
  }

  String _summary(String current) {
    if (current.isEmpty) return 'No captures listed while captures are off';
    if (_listError != null) return "Can't list the files: $_listError";
    final files = _files;
    if (files == null || files.dir != current) return 'Listing…';
    final n = files.captures.length;
    final older = files.notListed > 0 ? ', and ${files.notListed} older not listed' : '';
    return '$n capture${n == 1 ? '' : 's'}$older';
  }

  Widget _buildList() {
    final files = _files;
    if (files == null || files.dir != _listedDir) return const SizedBox.shrink();
    return ListView.builder(
      itemCount: files.captures.length,
      itemBuilder: (context, i) {
        final f = files.captures[i];
        final path = f.dataPath;
        return ListTile(
          dense: true,
          title: Text(f.name),
          subtitle: Text(_describe(f)),
          trailing: IconButton(
            icon: const Icon(Icons.copy, size: 18),
            tooltip: 'Copy $path',
            onPressed: () {
              Clipboard.setData(ClipboardData(text: path));
              ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Copied $path')));
            },
          ),
        );
      },
    );
  }

  static String _describe(api.Capture f) {
    String two(int n) => n.toString().padLeft(2, '0');
    final t = DateTime.fromMillisecondsSinceEpoch((f.modifiedUnixS * 1000).round());
    final parts = [
      '${t.year}-${two(t.month)}-${two(t.day)} ${two(t.hour)}:${two(t.minute)}:${two(t.second)}',
      _bytes(f.dataBytes.toInt()),
    ];
    if (f.hasMeta()) {
      final meta = f.meta;
      parts.addAll([
        '${meta.durationS.toStringAsFixed(3)} s',
        meta.datatype,
        'at ${_hz(meta.rateHz)}',
        if (meta.hasFreqHz()) 'tuned to ${_hz(meta.freqHz)}',
      ]);
    } else {
      parts.add('no .sigmf-meta');
    }
    return parts.join(' · ');
  }

  static String _bytes(int n) {
    if (n < 1024) return '$n B';
    if (n < 1024 * 1024) return '${(n / 1024).toStringAsFixed(1)} KiB';
    if (n < 1024 * 1024 * 1024) return '${(n / (1024 * 1024)).toStringAsFixed(1)} MiB';
    return '${(n / (1024 * 1024 * 1024)).toStringAsFixed(2)} GiB';
  }

  static String _hz(double hz) {
    if (hz >= 1e6) return '${(hz / 1e6).toStringAsFixed(3)} MHz';
    if (hz >= 1e3) return '${(hz / 1e3).toStringAsFixed(1)} kHz';
    return '${hz.toStringAsFixed(0)} Hz';
  }
}
