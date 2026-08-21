// Widget smoke test: the root RetroSaturnApp builds without throwing.
// Uses FakeYmirCore so no libymircore.so is needed in the test env.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:retro_saturn/main.dart';

void main() {
  testWidgets('RetroSaturnApp builds without crashing', (tester) async {
    // The app reads AppPrefs during initState, and AppPrefs throws rather
    // than silently answering defaults when it has not been loaded -- so the
    // widget cannot be pumped until it has. main() awaits load() before
    // runApp; a test that skips it is testing a state the app never reaches.
    SharedPreferences.setMockInitialValues(<String, Object>{});
    await AppPrefs.load();
    await tester.pumpWidget(const RetroSaturnApp());
    // We don't pumpAndSettle because the real FFI load will hang on
    // the host. Instead just assert something renders.
    expect(find.byType(MaterialApp), findsOneWidget);
  });
}