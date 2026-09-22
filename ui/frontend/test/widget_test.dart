import 'package:flutter_test/flutter_test.dart';

import 'package:adsb_ui/main.dart';

void main() {
  testWidgets('shows the frame table page with a connect button', (WidgetTester tester) async {
    await tester.pumpWidget(const AdsbApp());

    expect(find.text('ADS-B Frames'), findsWidgets);
    expect(find.text('Connect'), findsOneWidget);
    expect(find.text('Disconnected'), findsOneWidget);
  });
}
