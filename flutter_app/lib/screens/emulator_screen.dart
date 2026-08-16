// emulator_screen.dart — Emulator screen. Loads BIOS + disc on init,
// shows the framebuffer, mounts the on-screen Saturn pad, the Virtua
// Gun overlay (when port 1 peripheral == gun), and a settings drawer
// with the peripheral selector.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/peripheral_type.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/services/gamepad_service.dart';
import 'package:ymir_multiplatform/widgets/framebuffer_view.dart';
import 'package:ymir_multiplatform/widgets/peripheral_selector.dart';
import 'package:ymir_multiplatform/widgets/virtua_gun_overlay.dart';

class EmulatorScreen extends StatefulWidget {
  final YmirCore core;
  final String? biosPath;
  final String? discPath;

  const EmulatorScreen({
    super.key,
    required this.core,
    this.biosPath,
    this.discPath,
  });

  @override
  State<EmulatorScreen> createState() => _EmulatorScreenState();
}

class _EmulatorScreenState extends State<EmulatorScreen> {
  GamepadService? _gamepad;
  bool _padVisible = false; // hidden by default — gamepad is the input
  String _lastResult = 'starting…';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _loadMedia());
    _gamepad = GamepadService(widget.core, port: 1);
  }

  @override
  void dispose() {
    _gamepad?.dispose();
    super.dispose();
  }

  Future<void> _loadMedia() async {
    setState(() => _lastResult = 'loading BIOS…');
    if (widget.biosPath != null) {
      final rc = widget.core.loadBios(widget.biosPath!);
      setState(() => _lastResult = 'loadBios rc=$rc (${widget.biosPath})');
    }
    setState(() => _lastResult = 'loading disc…');
    if (widget.discPath != null) {
      final rc = widget.core.loadDisc(widget.discPath!);
      setState(() => _lastResult = 'loadDisc rc=$rc (${widget.discPath})');
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
          if (showGun)
            VirtuaGunOverlay(core: widget.core, port: 1),
          Positioned(
            top: 4,
            left: 4,
            right: 60,
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
          Positioned(
            top: 4,
            right: 4,
            child: Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                tooltip: 'Toggle pad',
                padding: EdgeInsets.zero,
                constraints: const BoxConstraints(minWidth: 36, minHeight: 36),
                icon: Icon(_padVisible ? Icons.gamepad : Icons.gamepad_outlined,
                    color: Colors.white, size: 20),
                onPressed: () => setState(() => _padVisible = !_padVisible),
              ),
              Builder(
                builder: (ctx) => IconButton(
                  tooltip: 'Settings',
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(minWidth: 36, minHeight: 36),
                  icon: const Icon(Icons.settings, color: Colors.white, size: 20),
                  onPressed: () => Scaffold.of(ctx).openDrawer(),
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
        ]),
      ),
    );
  }
}