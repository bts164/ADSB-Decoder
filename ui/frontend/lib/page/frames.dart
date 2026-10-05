part of 'frame_table_page.dart';

final List<TableColumn<FrameRecord>> _frameColumns = [
  TableColumn('idx', 110, numeric: true, compare: (a, b) => a.idx.compareTo(b.idx), cell: (f) => Text('${f.idx}')),
  TableColumn('df', 50, numeric: true, compare: (a, b) => a.df.compareTo(b.df), cell: (f) => Text('${f.df}')),
  TableColumn('icao', 80,
      compare: (a, b) => (a.icao ?? '').compareTo(b.icao ?? ''), cell: (f) => Text(f.icao ?? '??????')),
  TableColumn('confidence', 100,
      numeric: true,
      compare: (a, b) => a.confidence.compareTo(b.confidence),
      cell: (f) => Text(f.confidence.toStringAsFixed(1))),
  TableColumn('crc', 60, compare: (a, b) => crcRank(a).compareTo(crcRank(b)), cell: (f) {
    if (f.crcFixedBit != null) {
      return Tooltip(
        message: 'bit ${f.crcFixedBit} corrected',
        child: Text('fixed', style: TextStyle(color: Colors.amber.shade800)),
      );
    }
    return Text(
      f.crcOk ? 'ok' : f.crcChecked ? 'fail' : 'n/a',
      style: TextStyle(color: f.crcOk ? Colors.green : f.crcChecked ? Colors.red : Colors.grey),
    );
  }),
  TableColumn('decoded', 300,
      compare: (a, b) => (a.summary ?? '').compareTo(b.summary ?? ''),
      cell: (f) => Tooltip(
            message: f.summary ?? '',
            child: Text(f.summary ?? '', maxLines: 1, overflow: TextOverflow.ellipsis),
          )),
  TableColumn('bits', 50, numeric: true, cell: (f) => Text('${f.bits}')),
  TableColumn('payload', 270, cell: (f) => Text(f.payload, style: const TextStyle(fontFamily: 'monospace'))),
];

/// The frame log in the dock.
extension _FramesPane on _FrameTablePageState {
  Widget _buildFramesPane() {
    final paused = _pausedFrames != null;
    return Column(
      children: [
        Expanded(child: _buildFrameTable()),
        const Divider(height: 1),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          child: Row(
            children: [
              FilledButton.tonalIcon(
                onPressed: _toggleFramesPaused,
                icon: Icon(paused ? Icons.play_arrow : Icons.pause),
                label: Text(paused ? 'Resume' : 'Pause'),
              ),
              const SizedBox(width: 12),
              if (paused)
                Expanded(
                  child: Text(
                    'Paused, $_framesSincePause new frames since'
                    '${_framesSincePause > _FrameTablePageState._maxFrames ? ' (only the newest ${_FrameTablePageState._maxFrames} kept)' : ''}',
                    style: TextStyle(color: Colors.grey[600]),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildFrameTable() {
    return LazyTable<FrameRecord>(
      columns: _frameColumns,
      rows: _sortedFrames,
      sortColumn: _frameSortColumn,
      sortAscending: _frameSortAscending,
      onSort: _onFrameSort,
    );
  }
}
