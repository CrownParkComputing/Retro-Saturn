// virtua_gun_overlay.dart — Tap-to-shoot overlay for games that need
// the Virtua Gun (Virtual Cop, Layer Section, etc.). Mounted above
// the framebuffer in EmulatorScreen when port 1 peripheral == VirtuaGun.
//
// Touch coords are scaled into the VDP framebuffer space and fed via
// core.setVirtuaGunInput(port, x, y, trigger, start). On ACTION_UP we
// pass (0xFFFF, 0xFFFF) — ymir-core's convention for "off-screen",
// which games use to detect "shot fired while off-screen = miss/reload".

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';

class VirtuaGunOverlay extends StatefulWidget {
  final YmirCore core;
  final int port;

  const VirtuaGunOverlay({super.key, required this.core, this.port = 1});

  @override
  State<VirtuaGunOverlay> createState() => _VirtuaGunOverlayState();
}

class _VirtuaGunOverlayState extends State<VirtuaGunOverlay> {
  Offset? _crosshair;
  int _fbW = 320;
  int _fbH = 224;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _syncFbSize());
  }

  void _syncFbSize() {
    final snap = widget.core.framebuffer;
    if (snap != null) {
      setState(() {
        _fbW = snap.width;
        _fbH = snap.height;
      });
      widget.core.setVirtuaGunFbSize(_fbW, _fbH);
    }
  }

  void _onPointerDown(PointerDownEvent e) {
    final size = (context.findRenderObject() as RenderBox).size;
    final px = e.localPosition.dx.clamp(0.0, size.width - 1);
    final py = e.localPosition.dy.clamp(0.0, size.height - 1);
    final fbX = (1 + (px * (_fbW - 2) / size.width)).toInt().clamp(1, _fbW - 1);
    final fbY = (1 + (py * (_fbH - 2) / size.height)).toInt().clamp(1, _fbH - 1);
    setState(() => _crosshair = e.localPosition);
    widget.core.setVirtuaGunInput(widget.port, fbX, fbY, true, false);
  }

  void _onPointerUp(PointerUpEvent e) {
    // Off-screen = ymir-core's "shot missed" signal.
    widget.core.setVirtuaGunInput(widget.port, 0xFFFF, 0xFFFF, false, false);
    setState(() => _crosshair = null);
  }

  void _onPointerMove(PointerMoveEvent e) {
    if (_crosshair == null) return;
    final size = (context.findRenderObject() as RenderBox).size;
    final px = e.localPosition.dx.clamp(0.0, size.width - 1);
    final py = e.localPosition.dy.clamp(0.0, size.height - 1);
    final fbX = (1 + (px * (_fbW - 2) / size.width)).toInt().clamp(1, _fbW - 1);
    final fbY = (1 + (py * (_fbH - 2) / size.height)).toInt().clamp(1, _fbH - 1);
    setState(() => _crosshair = e.localPosition);
    widget.core.setVirtuaGunInput(widget.port, fbX, fbY, true, false);
  }

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: _onPointerDown,
      onPointerMove: _onPointerMove,
      onPointerUp:   _onPointerUp,
      child: CustomPaint(
        painter: _CrosshairPainter(_crosshair),
        child: const SizedBox.expand(),
      ),
    );
  }
}

class _CrosshairPainter extends CustomPainter {
  final Offset? position;
  _CrosshairPainter(this.position);

  @override
  void paint(Canvas canvas, Size size) {
    if (position == null) return;
    final p = Paint()
      ..color = Colors.redAccent
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    final c = position!;
    canvas.drawLine(Offset(c.dx - 12, c.dy), Offset(c.dx + 12, c.dy), p);
    canvas.drawLine(Offset(c.dx, c.dy - 12), Offset(c.dx, c.dy + 12), p);
    canvas.drawCircle(c, 4, p);
  }

  @override
  bool shouldRepaint(_CrosshairPainter old) => old.position != position;
}