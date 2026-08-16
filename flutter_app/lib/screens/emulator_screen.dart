// emulator_screen.dart — Game launcher. Loads BIOS + disc from the
// library grid tap, restores NVRAM (Saturn backup RAM), mounts the
// gamepad service + Virtua Gun overlay when appropriate, and
// auto-saves NVRAM every 30 seconds while playing.

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/data/peripheral_type.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/services/app_log.dart';
import 'package:ymir_multiplatform/services/backup_ram_service.dart';
import 'package:ymir_multiplatform/services/gamepad_service.dart';
import 'package:ymir_multiplatform/widgets/framebuffer_view.dart';
import 'package:ymir_multiplatform/widgets/peripheral_selector.dart';
import 'package:ymir_multiplatform/widgets/virtua_gun_overlay.dart';

class EmulatorScreen extends StatefulWidget {
  final YmirCore core;
  final String? biosPath;
  final String? gamesFolder;
  final MediaEntry? entry;

  const EmulatorScreen({
    super.key,
    required this.core,
    this.biosPath,
    this.gamesFolder,
    this.entry,
  });

  @override
  State<EmulatorScreen> createState() => _EmulatorScreenState();
}

class _EmulatorScreenState extends State<EmulatorScreen> {
  GamepadService? _gamepad;
  bool _padVisible = false;
  bool _paused = false;
  String _lastResult = 'starting…';
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
      widget.core.saveSmpcState('/sdcard/Android/data/com.crownpark.ymir_multiplatform/files/roms/smpc_state.bin');
    } catch (_) {}
    _gamepad?.dispose();
    super.dispose();
  }

  Future<void> _loadMedia() async {
    final biosPath = widget.biosPath;
    final entry = widget.entry;

    if (biosPath != null && File(biosPath).existsSync()) {
      setState(() => _lastResult = 'loading BIOS…');
      AppLog.log('loadBios: $biosPath');
      final rc = widget.core.loadBios(biosPath);
      AppLog.log('loadBios rc=$rc');
      setState(() => _lastResult = 'BIOS rc=$rc');
    } else {
      AppLog.log('loadBios: skipped (${biosPath == null ? "no path" : "missing file"})');
      setState(() => _lastResult = 'no BIOS (run setup)');
    }

    if (entry != null && File(entry.path).existsSync()) {
      _currentDisc = entry.path;
      setState(() => _lastResult = 'loading disc…');
      AppLog.log('loadDisc: ${entry.path}');
      final rc = widget.core.loadDisc(entry.path);
      AppLog.log('loadDisc rc=$rc');
      setState(() => _lastResult = 'disc rc=$rc (${entry.displayName})');

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

    setState(() => _lastResult = 'running (${widget.core.status})');
    AppLog.log('emulator running: ${widget.core.status} @ ${widget.core.fps}fps');
  }

  @override
  Widget build(BuildContext context) {
    final ptype = widget.core.getPeripheralType(1);
    final showGun = ptype == YmirPeripheralType.virtuaGun;
    return Scaffold(
      backgroundColor: Colors.black,
      drawer: _buildDrawer(context),
      body: SafeArea(
        child: Stack(children: [
          FramebufferView(core: widget.core, showFps: true),
          if (showGun) VirtuaGunOverlay(core: widget.core, port: 1),
          Positioned(
            top: 4, left: 4, right: 4,
            child: Row(children: [
              Flexible(
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                  color: Colors.black54,
                  child: Text(
                    widget.core.status,
                    style: const TextStyle(color: Colors.white, fontSize: 9),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
              // In-game icons. Order + icons match ViceMultiplatform:
              // pad toggle, keyboard / settings, pause, close.
              const SizedBox(width: 4),
              _InGameIcon(
                tooltip: _padVisible
                    ? 'Hide on-screen pad'
                    : 'Show on-screen pad',
                icon: _padVisible
                    ? Icons.gamepad
                    : Icons.gamepad_outlined,
                onPressed: () => setState(() => _padVisible = !_padVisible),
              ),
              Builder(
                builder: (ctx) => _InGameIcon(
                  tooltip: 'Settings',
                  icon: Icons.settings,
                  onPressed: () => Scaffold.of(ctx).openDrawer(),
                ),
              ),
              _InGameIcon(
                tooltip: _paused ? 'Resume' : 'Pause',
                icon: _paused ? Icons.play_arrow : Icons.pause,
                onPressed: () {
                  setState(() => _paused = !_paused);
                  widget.core.setPresentationPaused(_paused);
                },
              ),
              _InGameIcon(
                tooltip: 'Reset',
                icon: Icons.refresh,
                onPressed: () => widget.core.reset(hard: false),
              ),
              _InGameIcon(
                tooltip: 'Close game',
                icon: Icons.close,
                onPressed: () => Navigator.of(context).pop(),
              ),
            ]),
          ),
        ]),
      ),
    );
  }

  Widget _buildDrawer(BuildContext context) {
    return Drawer(
      child: SafeArea(
        child: ListView(padding: const EdgeInsets.all(16), children: [
          Text('Settings', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 16),
          PeripheralSelector(core: widget.core, port: 1),
          const SizedBox(height: 12),
          PeripheralSelector(core: widget.core, port: 2),
          const SizedBox(height: 16),
          Text('Bridge status', style: Theme.of(context).textTheme.titleSmall),
          Text(widget.core.status),
          Text('FPS: ${widget.core.fps}'),
          Text('Audio: ${widget.core.audioLevel}/100'),
          Text('Muted: ${widget.core.audioMuted}'),
          Text('Port 1: ${widget.core.getPeripheralType(1).displayName}'),
          Text('Port 2: ${widget.core.getPeripheralType(2).displayName}'),
          if (_currentDisc.isNotEmpty) Text('NVRAM: auto-save every 30s'),
        ]),
      ),
    );
  }
}

/// One Saturn pad overlay button + icon row, matching the in-game
/// icon strip ViceMultiplatform has at the top-right of the
/// emulator screen (pad / keyboard / pause / close).
class _InGameIcon extends StatelessWidget {
  final String tooltip;
  final IconData icon;
  final VoidCallback onPressed;
  const _InGameIcon({
    required this.tooltip,
    required this.icon,
    required this.onPressed,
  });
  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: SizedBox(
        width: 32, height: 32,
        child: IconButton(
          padding: EdgeInsets.zero,
          iconSize: 18,
          color: Colors.white,
          icon: Icon(icon),
          onPressed: onPressed,
        ),
      ),
    );
  }
}