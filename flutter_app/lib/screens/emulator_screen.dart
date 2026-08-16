// emulator_screen.dart — Minimal emulator screen for v1. Loads the
// BIOS + disc from paths supplied by the caller (workbench/library
// is Phase 3), shows the framebuffer, and overlays an FPS counter.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/widgets/framebuffer_view.dart';

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
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      if (widget.biosPath != null) {
        widget.core.loadBios(widget.biosPath!);
      }
      if (widget.discPath != null) {
        widget.core.loadDisc(widget.discPath!);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Stack(children: [
          FramebufferView(core: widget.core, showFps: true),
          Positioned(
            top: 8,
            left: 8,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              color: Colors.black54,
              child: Text(
                widget.core.status,
                style: const TextStyle(color: Colors.white, fontSize: 12),
              ),
            ),
          ),
        ]),
      ),
    );
  }
}