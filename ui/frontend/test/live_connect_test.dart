import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:adsb_ui/main.dart';

// Connects to a real WebSocket server on localhost:18765 (see
// scripts/fake_ws_server.py — a stand-in for the actual adsb backend's
// --ws-port, emitting the same {"type":"frame"|"aircraft", ...} schema) to
// exercise the golden path: connect -> receive frames/aircraft -> render
// rows in both tabs -> sort by column.
//
// Uses 18765 rather than the backend's usual 8765 so this doesn't collide
// with a real adsb backend that may already be running on this machine.
void main() {
  testWidgets('connects, renders incoming frames and aircraft, and sorts by column', (WidgetTester tester) async {
    await tester.pumpWidget(const AdsbApp());

    expect(find.text('Disconnected'), findsOneWidget);

    await tester.enterText(find.byType(TextField), 'ws://127.0.0.1:18765');
    await tester.pump();

    // The app batches incoming frames/aircraft and only applies them (via
    // setState) once a second -- see _flushUpdates in lib/page/frame_table_page.dart -- so poll
    // with real-time pumps rather than a single delay+pump, since a lone
    // pump() after the wait isn't guaranteed to observe a real Timer that
    // fired inside runAsync's zone.
    await tester.runAsync(() async {
      await tester.tap(find.text('Connect'));
      for (var i = 0; i < 20; i++) {
        await Future.delayed(const Duration(milliseconds: 200));
        await tester.pump();
      }
    });
    expect(find.text('Connected'), findsOneWidget);

    // Frames tab (default) shows raw frame rows.
    int rowCount() => tester.widget<DataTable>(find.byType(DataTable)).rows.length;
    expect(find.text('a60008'), findsWidgets);
    final beforeRows = rowCount();
    expect(beforeRows, greaterThan(0));

    await tester.tap(find.text('icao'));
    await tester.pump();
    expect(rowCount(), beforeRows);

    // Aircraft tab shows tracked aircraft state.
    await tester.tap(find.textContaining('Aircraft'));
    await tester.pumpAndSettle();
    expect(find.text('N486MT'), findsOneWidget);
    final aircraftRows = rowCount();
    expect(aircraftRows, greaterThan(0));

    await tester.tap(find.text('callsign'));
    await tester.pump();
    expect(rowCount(), aircraftRows);

    await tester.tap(find.text('Disconnect'));
    await tester.pump();
    expect(find.text('Disconnected'), findsOneWidget);
  });
}
