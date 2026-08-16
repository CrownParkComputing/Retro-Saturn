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
        _draggableCluster(_dpad, const Size(80, 80), _buildDpad),
        _draggableCluster(_face, const Size(220, 36), _buildFace),
        _draggableCluster(_triggers, const Size(80, 36), _buildTriggers),
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
    return Padding(
      padding: const EdgeInsets.all(2),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          _padBtn(YmirButton.up, Icons.arrow_drop_up, sz: 24),
          _padBtn(YmirButton.left, Icons.arrow_left, sz: 24),
          _padBtn(YmirButton.right, Icons.arrow_right, sz: 24),
          _padBtn(YmirButton.down, Icons.arrow_drop_down, sz: 24),
        ]),
      ),
    );
  }

  Widget _buildFace(Offset _) {
    return Padding(
      padding: const EdgeInsets.all(2),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: [
          _padBtn(YmirButton.y, null, label: 'Y', sz: 12),
          _padBtn(YmirButton.b, null, label: 'B', sz: 12),
          _padBtn(YmirButton.a, null, label: 'A', sz: 12),
          _padBtn(YmirButton.c, null, label: 'C', sz: 12),
          _padBtn(YmirButton.x, null, label: 'X', sz: 12),
          _padBtn(YmirButton.z, null, label: 'Z', sz: 12),
          _padBtn(YmirButton.start, null, label: 'ST', sz: 12),
        ]),
      ),
    );
  }

  Widget _buildTriggers(Offset _) {
    return Padding(
      padding: const EdgeInsets.all(2),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: [
          _padBtn(YmirButton.l, null, label: 'L', sz: 12),
          _padBtn(YmirButton.r, null, label: 'R', sz: 12),
        ]),
      ),
    );
  }

  Widget _padBtn(YmirButton b, IconData? icon,
      {String? label, double sz = 20}) {
    final isDown = _pressed.contains(b.value);
    return Listener(
      onPointerDown: (_) => _press(b, true),
      onPointerUp:   (_) => _press(b, false),
      onPointerCancel: (_) => _press(b, false),
      child: Container(
        margin: const EdgeInsets.all(1),
        padding: const EdgeInsets.all(2),
        decoration: BoxDecoration(
          color: isDown ? Colors.white24 : Colors.black26,
          shape: BoxShape.circle,
          border: Border.all(color: Colors.white54, width: 1),
        ),
        child: icon != null
            ? Icon(icon, color: Colors.white, size: sz)
            : Text(label!,
                style: TextStyle(color: Colors.white, fontSize: sz)),
      ),
    );
  }
}