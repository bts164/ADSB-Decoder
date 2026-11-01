part of 'frame_table_page.dart';

/// A 3D fix with gpsd's error estimate above this shows amber.
const _largeGpsErrorM = 50.0;

/// Input and frame-health indicators in the top bar, and their details dialog.
extension _Stats on _FrameTablePageState {
  /// Whether the live input keeps up: quiet when it does, a red warning with
  /// how much of the signal got through when it doesn't. The tooltip has the
  /// backend's account of where the rest went.
  Widget _buildInputStatus(InputStatus status) {
    final theme = Theme.of(context);
    final rate = '${(status.rateHz / 1e6).toStringAsFixed(1)} Msps';
    if (status.ok) {
      return Tooltip(
        message: status.summary,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.check_circle, size: 16, color: Colors.green),
            const SizedBox(width: 4),
            Text('input $rate', style: theme.textTheme.bodySmall),
          ],
        ),
      );
    }
    return Tooltip(
      message: status.summary,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        decoration: BoxDecoration(color: theme.colorScheme.error, borderRadius: BorderRadius.circular(12)),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.warning_amber, size: 16, color: theme.colorScheme.onError),
            const SizedBox(width: 4),
            Text(
              'input: only ${(status.delivered * 100).toStringAsFixed(1)}% of $rate demodulated',
              style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onError, fontWeight: FontWeight.bold),
            ),
          ],
        ),
      ),
    );
  }

  /// The GPS's state, in the input status's style: green with a 3D fix, amber with a 2D one or a large error,
  /// red without a fix or without gpsd, grey for an input with no GPS. The tooltip has the position, the age of
  /// the report and the backend's summary.
  Widget _buildGpsStatus(ReceiverPosition p) {
    final theme = Theme.of(context);
    final sats = p.satellitesUsed == null ? '' : ' · ${p.satellitesUsed} sats';
    final ageS = DateTime.now().millisecondsSinceEpoch / 1000 - p.timeS;
    final position = p.position;
    final tooltip = [
      if (p.hasFix && position != null)
        '${position.latitude.toStringAsFixed(5)}, ${position.longitude.toStringAsFixed(5)}',
      if (p.altM != null) 'altitude ${_units.formatAltitude((p.altM! / 0.3048).round())}',
      if (p.errorM != null) 'error ± ${_units.formatDistanceM(p.errorM!)}',
      if (p.satellitesUsed != null || p.satellitesSeen != null)
        'satellites: ${p.satellitesUsed ?? '?'} used of ${p.satellitesSeen ?? '?'} seen',
      if (p.state != api.GpsState.NO_GPS) 'reported ${ageS.clamp(0, double.infinity).toStringAsFixed(0)} s ago',
      if (p.summary.isNotEmpty) p.summary,
    ].join('\n');

    final IconData icon;
    final Color color;
    final String label;
    if (p.state == api.GpsState.FIX_3D || p.state == api.GpsState.FIX_2D) {
      final good = p.state == api.GpsState.FIX_3D && (p.errorM ?? 0) <= _largeGpsErrorM;
      icon = good ? Icons.gps_fixed : Icons.gps_not_fixed;
      color = good ? Colors.green : Colors.orange.shade800;
      label = '${p.state == api.GpsState.FIX_3D ? 'GPS 3D' : 'GPS 2D'}$sats';
    } else if (p.state == api.GpsState.NO_FIX || p.state == api.GpsState.GPSD_UNREACHABLE) {
      return Tooltip(
        message: tooltip,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          decoration: BoxDecoration(color: theme.colorScheme.error, borderRadius: BorderRadius.circular(12)),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.gps_off, size: 16, color: theme.colorScheme.onError),
              const SizedBox(width: 4),
              Text(
                p.state == api.GpsState.NO_FIX ? 'GPS: no fix' : 'GPS: gpsd unreachable',
                style:
                    theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onError, fontWeight: FontWeight.bold),
              ),
            ],
          ),
        ),
      );
    } else {
      icon = Icons.gps_off;
      color = Colors.grey;
      label = 'no GPS';
    }
    return Tooltip(
      message: tooltip,
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 16, color: color),
          const SizedBox(width: 4),
          Text(label, style: theme.textTheme.bodySmall),
        ],
      ),
    );
  }

  /// Summary of incoming-frame health: throughput and how much of what's
  /// being demodulated looks like a real transponder reply vs. noise. In the
  /// top bar since it's about the link, not either table.
  /// Clicking it opens _showStatsDetails.
  Widget _buildStatsSummary() {
    final total = _statsTotalFrames;
    String pct(int n, int d) => d > 0 ? '${(n / d * 100).toStringAsFixed(1)}%' : '–';
    final unknownDfPct = _statsUnknownDf / total * 100;
    return Align(
      alignment: Alignment.centerRight,
      child: TextButton(
        onPressed: _showStatsDetails,
        child: Text.rich(
          TextSpan(
            children: [
              TextSpan(text: '${_statsFramesPerSec.toStringAsFixed(1)} frames/s  ·  '),
              TextSpan(text: 'crc ok ${pct(_statsSelfCheckOk, _statsSelfCheck)}  ·  '),
              TextSpan(text: 'fixed $_statsCrcFixed  ·  '),
              TextSpan(text: 'addr ${pct(_statsAddrParityOk, _statsAddrParity)}  ·  '),
              TextSpan(
                text: 'unknown df ${unknownDfPct.toStringAsFixed(1)}%',
                style: _statsUnknownDf > 0 ? TextStyle(color: Colors.orange[800]) : null,
              ),
            ],
          ),
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
      ),
    );
  }

  void _showStatsDetails() {
    showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Frame statistics'),
        content: ListenableBuilder(
          listenable: _framesChanged,
          builder: (context, _) {
            final total = _statsTotalFrames;
            String pct(int n, int d) => d > 0 ? '${(n / d * 100).toStringAsFixed(1)}%' : '–';
            final unknownDfPct = total > 0 ? _statsUnknownDf / total * 100 : 0.0;
            return SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Wrap(
                    spacing: 20,
                    runSpacing: 8,
                    children: [
                      _statChip('$total', 'frames'),
                      _statChip(_statsFramesPerSec.toStringAsFixed(1), 'frames/s'),
                      _statChip(pct(_statsSelfCheckOk, _statsSelfCheck), 'crc ok (df 11/17/18)'),
                      _statChip(
                        '$_statsCrcFixed (${pct(_statsCrcFixed, _statsSelfCheckOk)})',
                        'crc ok after 1-bit fix (df 17/18)',
                      ),
                      _statChip(pct(_statsAddrParityOk, _statsAddrParity), 'address match (df 0/4/5/16/20/21/24)'),
                      _statChip('$_statsCrcUnchecked', 'crc unchecked (df 19/22)'),
                      _statChip(
                        '${unknownDfPct.toStringAsFixed(1)}%',
                        'unrecognized df',
                        warn: _statsUnknownDf > 0,
                      ),
                    ],
                  ),
                  const SizedBox(height: 16),
                  _buildDfBreakdown(),
                ],
              ),
            );
          },
        ),
        actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Close'))],
      ),
    );
  }

  Widget _statChip(String value, String label, {bool warn = false}) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          value,
          style: TextStyle(
            fontWeight: FontWeight.bold,
            color: warn ? Colors.orange[800] : null,
          ),
        ),
        Text(label, style: TextStyle(fontSize: 11, color: Colors.grey[600])),
      ],
    );
  }

  /// Per-downlink-format counts, sorted by frequency. DFs outside
  /// knownDfValues are flagged -- they're the frames most likely to be
  /// false-positive preamble detections on noise rather than real replies.
  Widget _buildDfBreakdown() {
    final total = _statsTotalFrames;
    final entries = _statsByDf.entries.toList()..sort((a, b) => b.value.compareTo(a.value));
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        for (final e in entries)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 1),
            child: Text(
              'df ${e.key.toString().padLeft(2)}: ${e.value.toString().padLeft(6)} '
              '(${(e.value / total * 100).toStringAsFixed(1)}%)'
              '${knownDfValues.contains(e.key) ? '' : '  — unrecognized, likely noise'}',
              style: TextStyle(
                fontFamily: 'monospace',
                color: knownDfValues.contains(e.key) ? null : Colors.orange[800],
              ),
            ),
          ),
      ],
    );
  }
}
