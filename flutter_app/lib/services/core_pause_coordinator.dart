// core_pause_coordinator.dart — when the emulator should stop for the OS.
//
// This is the whole of the lifecycle decision, kept out of the widget so it
// can be tested against the real sequences a platform sends rather than by
// backgrounding an app by hand and watching a CPU counter.
//
// Two things make it less obvious than "pause unless resumed":
//
//   inactive is not backgrounding. It fires for the notification shade, the
//   app switcher, a permission dialog, and on desktop for simply losing window
//   focus. Pausing there freezes an emulator that is still on screen.
//
//   Backgrounding arrives as MORE THAN ONE event. Android sends hidden and
//   then paused. Recording "was the user already paused?" on each of them
//   reads back the pause the first event just applied, concludes the user
//   wanted it paused, and then declines to resume -- which leaves the game
//   frozen on one frame when you come back to it, with no error anywhere.
//   Only the FIRST transition into the background may sample that.

import 'package:flutter/widgets.dart';

/// The emulator's pause state, as far as this coordinator is concerned.
abstract class PausableCore {
  bool get presentationPaused;
  void setPresentationPaused(bool paused);
}

class CorePauseCoordinator {
  /// True once a background event has been acted on, until the app resumes.
  bool _inBackground = false;

  /// Whether the user had already paused the core when it went to the
  /// background, so resuming does not un-pause something they paused.
  bool _pausedByUser = false;

  bool get inBackground => _inBackground;

  /// True for states where the app is genuinely off screen.
  static bool isBackground(AppLifecycleState state) =>
      state == AppLifecycleState.paused || state == AppLifecycleState.hidden;

  /// Feed every lifecycle change here. Safe to call repeatedly with the same
  /// state; only transitions do anything.
  void onLifecycle(AppLifecycleState state, PausableCore? core) {
    if (isBackground(state)) {
      if (_inBackground) return; // hidden then paused: the second is a no-op.
      _inBackground = true;
      _pausedByUser = core?.presentationPaused ?? false;
      if (!_pausedByUser) core?.setPresentationPaused(true);
      return;
    }

    if (state == AppLifecycleState.resumed) {
      if (!_inBackground) return;
      _inBackground = false;
      if (!_pausedByUser) core?.setPresentationPaused(false);
      _pausedByUser = false;
    }

    // inactive and detached: nothing. detached is handled by the caller,
    // which has to shut the core down rather than pause it.
  }
}
