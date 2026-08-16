// saturn_pad_overlay.dart — Draggable on-screen Saturn pad (12 buttons + start).
//
// Layout:
//   ┌──────────────────────────────────┐
//   │  [L]    [X][Y][Z]    [Up]       │
//   │  [Y]                  [Left][Right]
//   │                       [Down]    │
//   │         [A][B][C]    [Start]    │
//   │  [R]                          │
//   └──────────────────────────────────┘
//
// Drag-to-reposition via Stack + Positioned + GestureDetector. Each
// cluster can be moved independently. Position state persists via
// AppPrefs (Phase 3) — for now we just keep it in widget state.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/saturn_buttons.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';

class SaturnPadOverlay extends StatefulWidget {
  final YmirCore core;
  final int port;
  final bool visible;

  const SaturnPadOverlay({
    super.key,
    required this.core,
    this.port = 1,
    this.visible = true,
  });

  @override
  State<SaturnPadOverlay> createState() => _SaturnPadOverlayState();
}

class _SaturnPadOverlayState extends State<SaturnPadOverlay> {
  /// Position of each cluster as fractions of screen size.
  Offset _dpad = const Offset(0.05, 0.45);
  Offset _face = const Offset(0.42, 0.62);
  Offset _triggers = const Offset(0.05, 0.18);

  final Set<int> _pressed = {};

  void _press(YmirButton b, bool down) {
    if (down) _pressed.add(b.value); else _pressed.remove(b.value);
    widget.core.setPadButton(widget.port, b, down);
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    if (!widget.visible) return const SizedBox.shrink();
    return LayoutBuilder(builder: (context, c) {
      return Stack(children: [
        _draggableCluster(_dpad, const Size(120, 120), _buildDpad),
        _draggableCluster(_face, const Size(150, 90), _buildFace),
        _draggableCluster(_triggers, const Size(110, 60), _buildTriggers),
      ]);
    });
  }

  Widget _draggableCluster(Offset frac, Size size, Widget Function(Offset) build) {
    return Positioned(
      left: frac.dx * (MediaQuery.of(context).size.width - size.width),
      top: frac.dy * (MediaQuery.of(context).size.height - size.height),
      width: size.width,
      height: size.height,
      child: GestureDetector(
        onPanUpdate: (d) {
          setState(() {
            final w = MediaQuery.of(context).size.width - size.width;
            final h = MediaQuery.of(context).size.height - size.height;
            final nx = (frac.dx + d.delta.dx / (w <= 0 ? 1 : w)).clamp(0.0, 1.0);
            final ny = (frac.dy + d.delta.dy / (h <= 0 ? 1 : h)).clamp(0.0, 1.0);
            if (identical(build, _buildDpad)) _dpad = Offset(nx, ny);
            else if (identical(build, _buildFace)) _face = Offset(nx, ny);
            else _triggers = Offset(nx, ny);
          });
        },
        child: Container(
          decoration: BoxDecoration(
            color: Colors.black26,
            borderRadius: BorderRadius.circular(12),
          ),
          child: build(Offset.zero),
        ),
      ),
    );
  }

  Widget _buildDpad(Offset _) {
    return Row(mainAxisAlignment: MainAxisAlignment.center, children: [
      _padBtn(YmirButton.up, Icons.arrow_drop_up),
      _padBtn(YmirButton.left, Icons.arrow_left),
      _padBtn(YmirButton.right, Icons.arrow_right),
      _padBtn(YmirButton.down, Icons.arrow_drop_down),
    ]);
  }

  Widget _buildFace(Offset _) {
    return Row(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: [
      _padBtn(YmirButton.y, null, label: 'Y'),
      _padBtn(YmirButton.b, null, label: 'B'),
      _padBtn(YmirButton.a, null, label: 'A'),
      _padBtn(YmirButton.c, null, label: 'C'),
      _padBtn(YmirButton.x, null, label: 'X'),
      _padBtn(YmirButton.z, null, label: 'Z'),
      _padBtn(YmirButton.start, null, label: 'ST'),
    ]);
  }

  Widget _buildTriggers(Offset _) {
    return Row(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: [
      _padBtn(YmirButton.l, null, label: 'L'),
      _padBtn(YmirButton.r, null, label: 'R'),
    ]);
  }

  Widget _padBtn(YmirButton b, IconData? icon, {String? label}) {
    final isDown = _pressed.contains(b.value);
    return Listener(
      onPointerDown: (_) => _press(b, true),
      onPointerUp:   (_) => _press(b, false),
      onPointerCancel: (_) => _press(b, false),
      child: Container(
        margin: const EdgeInsets.all(4),
        padding: const EdgeInsets.all(6),
        decoration: BoxDecoration(
          color: isDown ? Colors.white24 : Colors.transparent,
          shape: BoxShape.circle,
          border: Border.all(color: Colors.white38),
        ),
        child: icon != null
            ? Icon(icon, color: Colors.white, size: 24)
            : Text(label!, style: const TextStyle(color: Colors.white, fontSize: 14)),
      ),
    );
  }
}