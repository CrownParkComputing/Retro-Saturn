// The lifecycle decision, against the sequences platforms actually send.
//
// Both of these bugs were shipped to a device before this file existed:
//
//   Pausing on anything that was not `resumed` meant `inactive` -- the
//   notification shade, the app switcher, losing window focus on desktop --
//   froze an emulator that was still on screen.
//
//   Then, treating hidden and paused as two independent background events
//   meant the second one sampled the pause the first had just applied,
//   concluded the user wanted it paused, and refused to resume. The game came
//   back frozen on a single frame with nothing in any log.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/services/core_pause_coordinator.dart';

class FakeCore implements PausableCore {
  bool paused = false;
  int setCalls = 0;

  @override
  bool get presentationPaused => paused;

  @override
  void setPresentationPaused(bool value) {
    setCalls++;
    paused = value;
  }
}

void main() {
  late CorePauseCoordinator c;
  late FakeCore core;

  setUp(() {
    c = CorePauseCoordinator();
    core = FakeCore();
  });

  test('the Android sequence pauses once and resumes', () {
    // What Android actually sends on HOME, then on return.
    c.onLifecycle(AppLifecycleState.inactive, core);
    c.onLifecycle(AppLifecycleState.hidden, core);
    c.onLifecycle(AppLifecycleState.paused, core);
    expect(core.paused, isTrue, reason: 'backgrounded means stopped');

    c.onLifecycle(AppLifecycleState.inactive, core);
    c.onLifecycle(AppLifecycleState.resumed, core);
    expect(core.paused, isFalse,
        reason: 'coming back must start the emulator again');
  });

  test('the second background event does not re-sample the pause state', () {
    c.onLifecycle(AppLifecycleState.hidden, core);
    final callsAfterFirst = core.setCalls;
    c.onLifecycle(AppLifecycleState.paused, core);
    expect(core.setCalls, callsAfterFirst,
        reason: 'hidden already paused it; paused must be a no-op');

    c.onLifecycle(AppLifecycleState.resumed, core);
    expect(core.paused, isFalse);
  });

  test('inactive alone never pauses', () {
    c.onLifecycle(AppLifecycleState.inactive, core);
    expect(core.paused, isFalse,
        reason: 'the app is still on screen: the shade, a dialog, focus loss');
    expect(core.setCalls, 0);
    expect(c.inBackground, isFalse);
  });

  test('a core the user paused stays paused after a round trip', () {
    core.paused = true; // user hit Pause in the toolbar
    c.onLifecycle(AppLifecycleState.hidden, core);
    c.onLifecycle(AppLifecycleState.paused, core);
    c.onLifecycle(AppLifecycleState.resumed, core);
    expect(core.paused, isTrue,
        reason: 'resuming must not undo a pause the user asked for');
  });

  test('resumed without a preceding background does nothing', () {
    c.onLifecycle(AppLifecycleState.resumed, core);
    expect(core.setCalls, 0);
  });

  test('two background/foreground cycles both work', () {
    for (var i = 0; i < 2; i++) {
      c.onLifecycle(AppLifecycleState.hidden, core);
      c.onLifecycle(AppLifecycleState.paused, core);
      expect(core.paused, isTrue, reason: 'cycle $i should pause');
      c.onLifecycle(AppLifecycleState.resumed, core);
      expect(core.paused, isFalse, reason: 'cycle $i should resume');
    }
  });

  test('a null core is survivable at any point', () {
    c.onLifecycle(AppLifecycleState.hidden, null);
    c.onLifecycle(AppLifecycleState.resumed, null);
    // The core loads asynchronously at startup; a lifecycle change can land
    // before it exists.
  });

  test('detached is left alone — the caller shuts the core down', () {
    c.onLifecycle(AppLifecycleState.detached, core);
    expect(core.setCalls, 0);
  });
}
