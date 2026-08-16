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
      final rc = widget.core.loadBios(biosPath);
      setState(() => _lastResult = 'BIOS rc=$rc');
    } else {
      setState(() => _lastResult = 'no BIOS (run setup)');
    }

    if (entry != null && File(entry.path).existsSync()) {
      _currentDisc = entry.path;
      setState(() => _lastResult = 'loading disc…');
      final rc = widget.core.loadDisc(entry.path);
      setState(() => _lastResult = 'disc rc=$rc (${entry.displayName})');

      // Restore NVRAM (Saturn backup RAM). Wrapped — a missing or
      // corrupt save file is fine (returns false), but a hard error
      // here would crash the whole screen.
      try {
        final loaded = await BackupRamService.loadInto(widget.core, entry.path);
        debugPrint('NVRAM load: $loaded');
      } catch (_) {
        // ignore — proceed without NVRAM
      }

      // Start auto-save every 60s while playing (longer interval to
      // avoid racing with in-game BIOS operations like 'Erase backup').
      BackupRamService.startAutoSave(widget.core, entry.path);
    }

    setState(() => _lastResult = 'running (${widget.core.status})');
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
              const SizedBox(width: 4),
              Builder(
                builder: (ctx) => SizedBox(
                  width: 36, height: 36,
                  child: IconButton(
                    tooltip: 'Settings',
                    padding: EdgeInsets.zero,
                    iconSize: 20,
                    icon: const Icon(Icons.settings, color: Colors.white),
                    onPressed: () => Scaffold.of(ctx).openDrawer(),
                  ),
                ),
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