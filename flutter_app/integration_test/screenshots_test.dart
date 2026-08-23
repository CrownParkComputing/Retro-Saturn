// Drives the app through the screens the App Store listing needs and asks the
// host driver to capture each one.
//
// This exists because the screenshots cannot be taken by hand at any useful
// scale: five sibling apps, several screens each, re-taken whenever the UI
// moves, and every image has to be the exact pixel size Apple validates. It
// also turns a screen that fails to build into a failing test rather than a
// screenshot nobody noticed was missing.
//
// Run it with tool/screenshots.sh, which supplies the simulator and fixtures.
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:retro_saturn/main.dart' as app;
import 'package:retro_saturn/services/setup_scan_service.dart';

/// Skips the launch-a-disc shot. That one boots the real core, which holds
/// the isolate for long enough that the driver's connection can drop -- so it
/// is separable from the eight static screens, which must not be lost with it.
const bool kSkipRunning = bool.fromEnvironment('SKIP_RUNNING');

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  /// Settles, then captures. pumpAndSettle alone is not enough: the library
  /// scans the disc folder off the main isolate, so the grid arrives after the
  /// frame that "settled" and an immediate capture catches an empty shelf.
  Future<void> shoot(WidgetTester tester, String name) async {
    await tester.pumpAndSettle(const Duration(milliseconds: 300));
    await binding.takeScreenshot(name);
  }

  Finder byText(String s) => find.textContaining(s, findRichText: true);

  /// A finder for the BUTTON carrying this label, not merely the text.
  ///
  /// These screens instruct as well as offer: the wizard's step 5 reads "Come
  /// back here and tap Rescan", and that sentence sits ABOVE the Rescan button
  /// in the tree. A plain text finder takes it, .first picks the prose, and
  /// the tap lands on a paragraph -- silently, because tapping a Text is a
  /// perfectly legal thing to do.
  Finder button(String label) {
    final text = find.textContaining(label, findRichText: true);
    for (final type in <Type>[
      ElevatedButton,
      FilledButton,
      OutlinedButton,
      TextButton,
      InkWell,
    ]) {
      final f = find.ancestor(of: text, matching: find.byType(type));
      if (f.evaluate().isNotEmpty) return f;
    }
    return text;
  }

  /// Pumps in real time. The folder scan runs off the main isolate, so frames
  /// pumped by the test clock do not advance it -- only actual elapsed time
  /// does, and pumpAndSettle returns long before the result arrives.
  /// Opens a rail category and PROVES it opened, by waiting for something
  /// only that screen shows.
  ///
  /// Without the proof this harness is worse than useless: a tap that lands on
  /// nothing leaves the library on screen, every later capture is the same
  /// library, and the run reports success with seven identical screenshots.
  /// That is exactly what it did.
  ///
  /// The tap goes to the rail's InkWell rather than its Text. Tapping the text
  /// is what missed -- and warnIfMissed was suppressed, so it missed silently.
  Future<void> openCategory(
      WidgetTester tester, String title, String marker) async {
    final entry = button(title);
    if (entry.evaluate().isEmpty) {
      // Capture whatever IS on screen before failing. A missing finder says
      // nothing about which screen the app actually ended up on, and that is
      // the only thing worth knowing here.
      await binding.takeScreenshot('FAILED-looking-for-$title');
      fail('no rail entry titled "$title"');
    }
    await tester.tap(entry.first);
    await tester.pumpAndSettle();

    if (byText(marker).evaluate().isEmpty) {
      await binding.takeScreenshot('FAILED-opening-$title');
      fail('tapped "$title" but "$marker" never appeared -- '
          'the panel did not change');
    }
  }

  /// Returns to the workbench when a screen opened as its own route.
  ///
  /// Not every category is an in-panel swap: store compliance pushes a
  /// full-screen page with its own back button, which covers the rail.
  Future<void> backToWorkbench(WidgetTester tester) async {
    try {
      await tester.pageBack();
      await tester.pumpAndSettle();
      return;
    } catch (_) {
      // pageBack fails two ways that both land here: nothing was pushed, or
      // the route's back control is a plain IconButton rather than one of the
      // three widget types it knows about. Neither is an error worth raising
      // -- if a route really is still up, the next lookup says so with the
      // screen attached.
    }
    for (final icon in <IconData>[
      Icons.arrow_back_ios,
      Icons.arrow_back_ios_new,
      Icons.arrow_back,
      Icons.chevron_left,
    ]) {
      final f = find.widgetWithIcon(IconButton, icon);
      if (f.evaluate().isNotEmpty) {
        await tester.tap(f.first, warnIfMissed: false);
        await tester.pumpAndSettle();
        return;
      }
    }
  }

  Future<void> waitReal(WidgetTester tester, Duration total) async {
    final end = DateTime.now().add(total);
    while (DateTime.now().isBefore(end)) {
      await tester.pump(const Duration(milliseconds: 200));
      await Future<void>.delayed(const Duration(milliseconds: 200));
    }
  }

  Future<bool> tapIfPresent(WidgetTester tester, Finder f) async {
    if (f.evaluate().isEmpty) return false;
    await tester.tap(f.first, warnIfMissed: false);
    await tester.pumpAndSettle(const Duration(seconds: 1));
    return true;
  }

  testWidgets('captures the listing screenshots', (tester) async {
    app.main();
    await tester.pumpAndSettle(const Duration(seconds: 3));

    // Install gave the app an empty container, so it is sitting in the wizard
    // with nothing to find. Stage the BIOS and disc, then let it look again.

    // Wait for the fixtures to actually appear rather than assuming the seed
    // landed before this line runs. Whether takeScreenshot's round trip to the
    // host is ordered against the copy is not something to take on trust, and
    // an unseeded run fails much later and much less clearly.
    final probeFolder0 = await SetupScanService.autoDetectFolderAsync();
    if (probeFolder0 != null) {
      final started = DateTime.now();
      var seen = 0;
      while (DateTime.now().difference(started) < const Duration(seconds: 20)) {
        seen = Directory(probeFolder0)
            .listSync(recursive: true)
            .whereType<File>()
            .length;
        if (seen > 0) break;
        await tester.pump(const Duration(milliseconds: 200));
        await Future<void>.delayed(const Duration(milliseconds: 200));
      }
      debugPrint('fixtures: $seen file(s) after '
          '${DateTime.now().difference(started).inMilliseconds}ms');
    }


    await tapIfPresent(tester, button('Rescan'));
    await waitReal(tester, const Duration(seconds: 4));

    // The wizard is the first thing a reviewer sees on a fresh install, so it
    // belongs in the listing. Detected by its own controls rather than by a
    // title, which is the check that previously mistook it for the library.
    final onWizard = button('Finish').evaluate().isNotEmpty;
    await shoot(tester, onWizard ? '01-setup-wizard' : '01-library');

    if (onWizard) {
      await tapIfPresent(tester, button('Finish'));
      await tester.pumpAndSettle(const Duration(seconds: 2));
      await shoot(tester, '02-library');
    }

    // <rail title, screenshot name, a string only that screen shows>
    for (final entry in <List<String>>[
      <String>['Compliance', '03-store-compliance', 'No BIOS is shipped'],
      <String>['Paths', '04-paths', 'Games folder'],
      <String>['Audio', '05-audio', 'Live level'],
      <String>['Input', '06-input', 'Peripherals'],
      <String>['Memories', '07-memories', 'Japan launch'],
      <String>['About', '08-about', 'Sega Saturn emulator'],
    ]) {
      await openCategory(tester, entry[0], entry[2]);
      await shoot(tester, entry[1]);
      await backToWorkbench(tester);
    }

    // Back to the shelf and launch the disc. This is the screenshot that shows
    // the emulator actually running, which is the one the listing most needs
    // and the one that cannot be faked.
    await openCategory(tester, 'Games', 'Search games');
    await shoot(tester, '09-library');

    if (!kSkipRunning && await tapIfPresent(tester, byText('PPPong'))) {
      // Emulation needs real time, not pumped frames: the core boots the BIOS
      // on its own thread, and that thread is not driven by the test clock.
      for (var i = 0; i < 24; i++) {
        await tester.pump(const Duration(milliseconds: 500));
        await Future<void>.delayed(const Duration(milliseconds: 500));
      }
      await shoot(tester, '10-running');
    }
  });
}
