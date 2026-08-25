// How long does the app survive after a disc is launched, and what is it doing
// when it dies?
//
// The process disappears with no Dart exception and no crash report, and the
// app's own log stops at "emulator running ... @ 63fps" -- so the core is
// healthy and something after that kills it. debugPrint reaches the console
// live, unlike AppLog which writes to a file, so a heartbeat here shows the
// last moment the isolate was alive.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:retro_saturn/main.dart' as app;

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  Finder byText(String s) => find.textContaining(s, findRichText: true);

  testWidgets('heartbeat around a disc launch', (tester) async {
    app.main();
    await tester.pumpAndSettle(const Duration(seconds: 3));

    // Past the wizard if it is up.
    final finish = byText('Finish');
    if (finish.evaluate().isNotEmpty) {
      Finder t = finish;
      for (final ty in <Type>[ElevatedButton, FilledButton, OutlinedButton, TextButton]) {
        final f = find.ancestor(of: finish, matching: find.byType(ty));
        if (f.evaluate().isNotEmpty) { t = f; break; }
      }
      await tester.tap(t.first, warnIfMissed: false);
      await tester.pumpAndSettle(const Duration(seconds: 2));
    }

    var card = byText('PPPong');
    final deadline = DateTime.now().add(const Duration(seconds: 20));
    while (card.evaluate().isEmpty && DateTime.now().isBefore(deadline)) {
      await tester.pump(const Duration(milliseconds: 300));
      await Future<void>.delayed(const Duration(milliseconds: 300));
      card = byText('PPPong');
    }
    if (card.evaluate().isEmpty) {
      debugPrint('HEARTBEAT: the disc never appeared on the shelf');
      return;
    }

    debugPrint('HEARTBEAT: tapping the disc now');
    await tester.tap(card.first, warnIfMissed: false);

    // Deliberately NOT pumpAndSettle: if the crash is in the first frame of the
    // emulator view, settling would take the isolate down before a single
    // heartbeat had been printed.
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 250));
      await Future<void>.delayed(const Duration(milliseconds: 250));
      debugPrint('HEARTBEAT t=${(i + 1) * 0.5}s alive');
    }

    // The one thing the screenshot run does that this did not. Capturing a
    // live emulator is the remaining difference between a run that survives
    // and a run whose process vanishes.
    debugPrint('HEARTBEAT: about to take a screenshot of the running emulator');
    await binding.takeScreenshot('emulator-live');
    debugPrint('HEARTBEAT: screenshot returned');

    // Now LEAVE the session. dispose() on the emulator screen writes NVRAM
    // and the SMPC state through the core, and those calls are wrapped for
    // Dart exceptions -- which does nothing for a fault inside the native
    // call. If the process dies here, that is where.
    debugPrint('HEARTBEAT: leaving the session now');
    final games = find.textContaining('Games', findRichText: true);
    if (games.evaluate().isNotEmpty) {
      Finder t = games;
      final ink = find.ancestor(of: games, matching: find.byType(InkWell));
      if (ink.evaluate().isNotEmpty) t = ink;
      await tester.tap(t.first, warnIfMissed: false);
    } else {
      debugPrint('HEARTBEAT: no Games entry to tap');
    }

    for (var i = 0; i < 16; i++) {
      await tester.pump(const Duration(milliseconds: 250));
      await Future<void>.delayed(const Duration(milliseconds: 250));
      debugPrint('HEARTBEAT after-leave t=${(i + 1) * 0.5}s alive');
    }
    debugPrint('HEARTBEAT: survived the whole run');
  });
}
