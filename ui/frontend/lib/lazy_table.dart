import 'dart:math' as math;

import 'package:flutter/material.dart';

/// One column of a LazyTable.
class TableColumn<T> {
  final String label;
  final double width;
  final bool numeric;
  /// Sort order, or null if the column isn't sortable.
  final Comparator<T>? compare;
  final Widget Function(T row) cell;

  const TableColumn(this.label, this.width, {this.numeric = false, this.compare, required this.cell});
}

/// A sortable table that builds only the rows on screen. DataTable builds
/// every row and measures every cell to size its columns on each rebuild,
/// which froze the UI once the frame log filled up; here columns are
/// fixed-width and rows fixed-height, so the ListView lays out only what's
/// visible.
class LazyTable<T> extends StatelessWidget {
  const LazyTable({
    super.key,
    required this.columns,
    required this.rows,
    required this.sortColumn,
    required this.sortAscending,
    required this.onSort,
    this.onTapRow,
    this.isSelected,
    this.rowStyle,
  });

  final List<TableColumn<T>> columns;
  final List<T> rows;
  final int sortColumn;
  final bool sortAscending;
  final void Function(int column, bool ascending) onSort;
  final void Function(T row)? onTapRow;
  final bool Function(T row)? isSelected;
  final TextStyle? Function(T row)? rowStyle;

  static const double _headerHeight = 40;
  static const double _rowHeight = 32;

  Widget _cell(TableColumn<T> c, double scale, Widget child) {
    return SizedBox(
      width: c.width * scale,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Align(alignment: c.numeric ? Alignment.centerRight : Alignment.centerLeft, child: child),
      ),
    );
  }

  Widget _header(int i, double scale) {
    final c = columns[i];
    final sorted = i == sortColumn;
    final label = _cell(
      c,
      scale,
      Row(
        mainAxisAlignment: c.numeric ? MainAxisAlignment.end : MainAxisAlignment.start,
        children: [
          Flexible(
            child: Text(
              c.label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontWeight: FontWeight.bold),
            ),
          ),
          if (sorted) Icon(sortAscending ? Icons.arrow_upward : Icons.arrow_downward, size: 14),
        ],
      ),
    );
    // Like DataTable: a new column sorts ascending, the current one toggles.
    return c.compare == null ? label : InkWell(onTap: () => onSort(i, sorted ? !sortAscending : true), child: label);
  }

  Widget _row(BuildContext context, T row, double scale) {
    final theme = Theme.of(context);
    final selected = isSelected?.call(row) ?? false;
    return DecoratedBox(
      position: DecorationPosition.foreground,
      decoration: BoxDecoration(border: Border(bottom: BorderSide(color: theme.dividerColor, width: 0.5))),
      child: Material(
        color: selected ? theme.colorScheme.primaryContainer : Colors.transparent,
        child: InkWell(
          onTap: onTapRow == null ? null : () => onTapRow!(row),
          child: DefaultTextStyle.merge(
            style: rowStyle?.call(row),
            maxLines: 1,
            softWrap: false,
            overflow: TextOverflow.ellipsis,
            child: Row(children: [for (final c in columns) _cell(c, scale, c.cell(row))]),
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final width = columns.fold<double>(0, (w, c) => w + c.width);
    return LayoutBuilder(
      builder: (context, constraints) {
        // Columns stretch in proportion to fill spare width; narrower than
        // their total, the table scrolls horizontally instead.
        final scale = math.max(1.0, constraints.maxWidth / width);
        return SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: SizedBox(
            width: width * scale,
            child: Column(
              children: [
                SizedBox(
                  height: _headerHeight,
                  child: Row(children: [for (var i = 0; i < columns.length; i++) _header(i, scale)]),
                ),
                const Divider(height: 1),
                Expanded(
                  child: ListView.builder(
                    itemCount: rows.length,
                    itemExtent: _rowHeight,
                    itemBuilder: (context, i) => _row(context, rows[i], scale),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}
