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
import 'package:retro_saturn/widgets/virtua_gun_overlay.dart';

class EmulatorScreen extends StatefulWidget {
  final YmirCore core;
  final String? biosPath;
  final String? gamesFolder;
  final MediaEntry? entry;

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
    this.showPadOverlay = false,
  });

  @override
  State<EmulatorScreen> createState() => _EmulatorScreenState();
}

class _EmulatorScreenState extends State<EmulatorScreen> {
  GamepadService? _gamepad;
  String _currentDisc = '';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadMedia());
    _gamepad = GamepadService(widget.core, port: 1);
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
    }

    AppLog.log('emulator running: ${widget.core.status} @ ${widget.core.fps}fps');
  }

  @override
  Widget build(BuildContext context) {
    final ptype = widget.core.getPeripheralType(1);
    final showGun = ptype == YmirPeripheralType.virtuaGun;
    return Stack(children: [
      // No FPS overlay: the status row already reports the core's rate, and
      // this widget's counter measures its own redraws, which is a different
      // number under the same name drawn over the top-right of the game.
      FramebufferView(core: widget.core),
      if (showGun) VirtuaGunOverlay(core: widget.core, port: 1),
      if (widget.showPadOverlay) SaturnPadOverlay(core: widget.core),
    ]);
  }
}