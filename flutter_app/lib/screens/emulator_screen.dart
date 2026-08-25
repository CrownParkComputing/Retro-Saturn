// emulator_screen.dart — Renders the emulated framebuffer + the
// peripheral overlays (Virtua Gun, on-screen Saturn pad) inside the
// workbench's content panel. Loads BIOS + disc from the library grid
// tap, restores NVRAM (Saturn backup RAM), mounts the gamepad service,
// and auto-saves NVRAM every 60 seconds while playing.
//
// The in-game toolbar (pad toggle, settings, pause, close) lives in
// the workbench's status bar, beneath the content panel, matching
// Retro-C64's EmulatorControlStrip pattern. The settings drawer is
// rendered from the workbench too. This screen no longer owns its own
// Scaffold -- the emulator chrome is below the picture, not on it.

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_log.dart';
import 'package:retro_saturn/services/backup_ram_service.dart';
import 'package:retro_saturn/services/gamepad_service.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';
import 'package:retro_saturn/widgets/framebuffer_view.dart';
import 'package:retro_saturn/widgets/saturn_pad_overlay.dart';
import 'package:retro_saturn/widgets/shuttle_mouse_overlay.dart';
import 'package:retro_saturn/widgets/virtua_gun_overlay.dart';

class EmulatorScreen extends StatefulWidget {
  final YmirCore core;
  final String? biosPath;
  final String? gamesFolder;
  final MediaEntry? entry;

  /// Save state to restore once the disc is mounted, or null for a normal
  /// launch. Restoring has to wait for loadDisc: the core validates the disc
  /// and BIOS hashes and refuses a state that belongs to another machine, so
  /// a state loaded before the disc is in is a state rejected.
  final String? resumeStatePath;

  /// Owned by the workbench -- the in-game toolbar toggles this, and we
  /// render the on-screen Saturn pad when it is true. Lifting it out of
  /// EmulatorScreen means the pad toggle and the pad overlay see the
  /// same source of truth (the workbench), which is what the previous
  /// in-screen toolbar got wrong (toggle was here, overlay was never
  /// rendered).
  final bool showPadOverlay;

  const EmulatorScreen({
    super.key,
    required this.core,
    this.biosPath,
    this.gamesFolder,
    this.entry,
    this.resumeStatePath,
    this.showPadOverlay = false,
  });

  @override
  State<EmulatorScreen> createState() => _EmulatorScreenState();
}

class _EmulatorScreenState extends State<EmulatorScreen> {
  GamepadService? _gamepad;
  String _currentDisc = '';

  /// The emulated resolution, published by FramebufferView and consumed by
  /// the Virtua Gun overlay. Saturn titles switch modes mid-game, and an aim
  /// mapped against a stale resolution misses by more the further it is from
  /// the centre of the picture.
  final ValueNotifier<Size> _frameSize = ValueNotifier<Size>(Size.zero);

  /// Peripheral changes come from the settings drawer, not from this screen,
  /// so the overlay choice has to be re-read rather than captured once.
  Timer? _peripheralWatch;
  YmirPeripheralType _peripheral = YmirPeripheralType.controlPad;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadMedia());
    _gamepad = GamepadService(widget.core, port: 1);
    _peripheral = widget.core.getPeripheralType(1);
    _peripheralWatch = Timer.periodic(const Duration(milliseconds: 500), (_) {
      final now = widget.core.getPeripheralType(1);
      if (now != _peripheral && mounted) setState(() => _peripheral = now);
    });
  }

  @override
  void dispose() {
    // Persist NVRAM + SMPC state on exit. Errors are swallowed — if
    // the path is gone (app uninstalled mid-launch) or the NVRAM is
    // empty (user erased it via the BIOS), we don't want dispose()
    // itself to crash.
    if (_currentDisc.isNotEmpty) {
      try {
        BackupRamService.saveFrom(widget.core, _currentDisc);
      } catch (_) {}
    }
    BackupRamService.stopAutoSave();
    try {
      widget.core.saveSmpcState(YmirCorePaths.smpcStatePath);
    } catch (_) {}
    _peripheralWatch?.cancel();
    _frameSize.dispose();
    _gamepad?.dispose();
    super.dispose();
  }

  Future<void> _loadMedia() async {
    final biosPath = widget.biosPath;
    final entry = widget.entry;

    if (biosPath != null && File(biosPath).existsSync()) {
      AppLog.log('loadBios: $biosPath');
      final rc = widget.core.loadBios(biosPath);
      AppLog.log('loadBios rc=$rc');
    } else {
      AppLog.log('loadBios: skipped (${biosPath == null ? "no path" : "missing file"})');
    }

    if (entry != null && File(entry.path).existsSync()) {
      _currentDisc = entry.path;
      AppLog.log('loadDisc: ${entry.path}');
      final rc = widget.core.loadDisc(entry.path);
      AppLog.log('loadDisc rc=$rc');

      // Restore NVRAM (Saturn backup RAM). Wrapped — a missing or
      // corrupt save file is fine (returns false), but a hard error
      // here would crash the whole screen.
      try {
        final loaded = await BackupRamService.loadInto(widget.core, entry.path);
        AppLog.log('NVRAM load: $loaded (${entry.displayName})');
        debugPrint('NVRAM load: $loaded');
      } catch (e) {
        AppLog.log('NVRAM load exception: $e');
      }

      // Start auto-save every 60s while playing (longer interval to
      // avoid racing with in-game BIOS operations like 'Erase backup').
      BackupRamService.startAutoSave(widget.core, entry.path);
      AppLog.log('NVRAM auto-save started (60s interval)');

      // Now that the disc is mounted, a requested state can be restored.
      final resume = widget.resumeStatePath;
      if (resume != null && File(resume).existsSync()) {
        final rc = widget.core.loadState(resume);
        AppLog.log('resume state rc=$rc ($resume)');
        if (rc != 0 && mounted) {
          ScaffoldMessenger.of(context).showSnackBar(SnackBar(
            content: Text(_resumeError(rc)),
          ));
        }
      }
    }

    AppLog.log('emulator running: ${widget.core.status} @ ${widget.core.fps}fps');
  }

  /// Turn the core's refusal codes into something that says what to do.
  static String _resumeError(int rc) {
    switch (rc) {
      case -13:
        return 'That save state was made by an older build and cannot be '
            'loaded. Start a new session.';
      case -14:
        return 'That save state belongs to a different disc or BIOS.';
      default:
        return 'Could not restore that save state (error $rc).';
    }
  }

  @override
  Widget build(BuildContext context) {
    final showGun = _peripheral == YmirPeripheralType.virtuaGun;
    final showMouse = _peripheral == YmirPeripheralType.shuttleMouse;
    return Stack(children: [
      // No FPS overlay: the status row already reports the core's rate, and
      // this widget's counter measures its own redraws, which is a different
      // number under the same name drawn over the top-right of the game.
      FramebufferView(core: widget.core, frameSize: _frameSize),
      if (showGun)
        VirtuaGunOverlay(core: widget.core, port: 1, frameSize: _frameSize),
      if (showMouse) ShuttleMouseOverlay(core: widget.core, port: 1),
      // The pad overlay sits on top of neither: a gun or mouse game has no
      // use for it, and two pointer-hungry overlays in one stack means the
      // upper one eats every event the lower one needs.
      if (widget.showPadOverlay && !showGun && !showMouse)
        SaturnPadOverlay(core: widget.core),
    ]);
  }
}