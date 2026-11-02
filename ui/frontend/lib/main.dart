import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'page/frame_table_page.dart';
import 'settings.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // The right button is the app's: it pans the plots and the map. Text fields show Flutter's own menu instead.
  if (kIsWeb) await BrowserContextMenu.disableContextMenu();
  final settings = await DisplaySettings.load();
  runApp(AdsbApp(settings: settings));
}

class AdsbApp extends StatelessWidget {
  /// Without `settings`, the page uses unsaved defaults (as in the tests,
  /// where there's no preferences store).
  const AdsbApp({super.key, this.settings});

  final DisplaySettings? settings;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ADS-B Frames',
      theme: ThemeData(colorSchemeSeed: Colors.blue, useMaterial3: true),
      home: FrameTablePage(settings: settings),
    );
  }
}
