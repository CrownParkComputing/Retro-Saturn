// shuttle_mouse_overlay.dart — Saturn Shuttle Mouse input, from a host mouse
// or from the touch screen. Mounted above the framebuffer when the port's
// peripheral is shuttleMouse.
//
// The Shuttle Mouse reports RELATIVE movement, not a position: there is no
// "where the cursor is" to sync to, only "how far it just moved". So:
//
//   mouse  — the host pointer's own movement deltas are forwarded as-is, and
//            the buttons map straight across (left/middle/right).
//   touch  — the panel behaves as a trackpad. Drag anywhere to move; a tap
//            that does not travel is a left click; the L/M/R buttons are
//            there for drag-and-click, which one finger cannot do.
//
// Sub-pixel movement is accumulated rather than truncated. Truncating each
// event throws away most of a slow, careful drag -- the pointer then refuses
// to move at all below a certain speed, which reads as the mouse being broken.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';

class ShuttleMouseOverlay extends StatefulWidget {
  final YmirCore core;
  final int port;

  /// Screen pixels to Saturn mouse units. A finger drag has to cover the
  /// pointer's travel within a panel much smaller than the desk a real mouse
  /// had, so touch is geared up; a host mouse is already 1:1.
  final double touchSensitivity;
  final double mouseSensitivity;

  const ShuttleMouseOverlay({
    super.key,
    required this.core,
    this.port = 1,
    this.touchSensitivity = 1.8,
    this.mouseSensitivity = 1.0,
  });

  @override
  State<ShuttleMouseOverlay> createState() => _ShuttleMouseOverlayState();
}

class _ShuttleMouseOverlayState extends State<ShuttleMouseOverlay> {
  /// Sub-unit movement not yet worth sending, kept so slow drags still move.
  double _carryX = 0;
  double _carryY = 0;

  /// Touch tap detection: a press that neither travels far nor lasts long is
  /// a click, not the start of a drag.
  Offset? _touchDownAt;
  DateTime? _touchDownWhen;
  bool _touchDragged = false;

  static const double _tapSlop = 12.0;
  static const Duration _tapMaxHold = Duration(milliseconds: 250);

  final Set<YmirMouseButton> _held = {};

  void _move(Offset delta, double sensitivity) {
    _carryX += delta.dx * sensitivity;
    _carryY += delta.dy * sensitivity;
    final dx = _carryX.truncate();
    final dy = _carryY.truncate();
    if (dx == 0 && dy == 0) return;
    _carryX -= dx;
    _carryY -= dy;
    widget.core.setMouseMotion(widget.port, dx, dy);
  }

  void _button(YmirMouseButton b, bool down) {
    if (down) {
      _held.add(b);
    } else {
      _held.remove(b);
    }
    setState(() {});
    widget.core.setMouseButton(widget.port, b, down);
  }

  Future<void> _clickLeft() async {
    _button(YmirMouseButton.left, true);
    // Held across a few peripheral reads. The bridge latches the gun's
    // trigger for this reason; the mouse buttons are plain state, so the
    // hold has to happen here.
    await Future<void>.delayed(const Duration(milliseconds: 60));
    if (!mounted) return;
    _button(YmirMouseButton.left, false);
  }

  // ---- host mouse -------------------------------------------------------

  void _syncMouseButtons(int buttons) {
    _button(YmirMouseButton.left, (buttons & kPrimaryMouseButton) != 0);
    _button(YmirMouseButton.right, (buttons & kSecondaryMouseButton) != 0);
    _button(YmirMouseButton.middle, (buttons & kMiddleMouseButton) != 0);
  }

  void _onHover(PointerHoverEvent e) => _move(e.delta, widget.mouseSensitivity);

  // ---- pointer plumbing -------------------------------------------------

  void _onPointerDown(PointerDownEvent e) {
    if (e.kind == PointerDeviceKind.mouse) {
      _syncMouseButtons(e.buttons);
      return;
    }
    _touchDownAt = e.localPosition;
    _touchDownWhen = DateTime.now();
    _touchDragged = false;
  }

  void _onPointerMove(PointerMoveEvent e) {
    if (e.kind == PointerDeviceKind.mouse) {
      _move(e.delta, widget.mouseSensitivity);
      return;
    }
    final origin = _touchDownAt;
    if (origin != null && (e.localPosition - origin).distance > _tapSlop) {
      _touchDragged = true;
    }
    _move(e.delta, widget.touchSensitivity);
  }

  void _onPointerUp(PointerUpEvent e) {
    if (e.kind == PointerDeviceKind.mouse) {
      _syncMouseButtons(e.buttons);
      return;
    }
    final when = _touchDownWhen;
    final quick = when != null && DateTime.now().difference(when) < _tapMaxHold;
    _touchDownAt = null;
    _touchDownWhen = null;
    if (!_touchDragged && quick) {
      _clickLeft();
    }
  }

  @override
  Widget build(BuildContext context) {
    // The button row is a SIBLING of the trackpad surface, not a child: nested
    // inside, a press on L would also register as a tap on the trackpad and
    // fire a second, phantom left click.
    return Stack(
      children: [
        Positioned.fill(
          child: Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: _onPointerDown,
            onPointerMove: _onPointerMove,
            onPointerUp: _onPointerUp,
            onPointerHover: _onHover,
            child: const SizedBox.expand(),
          ),
        ),
        // Inside the safe area, not flush to the edge -- see the same note in
        // virtua_gun_overlay: this device runs gesture navigation and the app
        // draws behind the reserved strip, so a button at bottom: 12 renders
        // perfectly and never receives a touch.
        Positioned.fill(
          child: SafeArea(
            minimum: const EdgeInsets.all(16),
            child: Align(
              alignment: Alignment.bottomLeft,
              child: Padding(
                padding: const EdgeInsets.only(bottom: 12, left: 8),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    _MouseButton(
                      label: 'L',
                      held: _held.contains(YmirMouseButton.left),
                      onChanged: (d) => _button(YmirMouseButton.left, d),
                    ),
                    const SizedBox(width: 8),
                    _MouseButton(
                      label: 'M',
                      held: _held.contains(YmirMouseButton.middle),
                      onChanged: (d) => _button(YmirMouseButton.middle, d),
                    ),
                    const SizedBox(width: 8),
                    _MouseButton(
                      label: 'R',
                      held: _held.contains(YmirMouseButton.right),
                      onChanged: (d) => _button(YmirMouseButton.right, d),
                    ),
                    const SizedBox(width: 8),
                    _MouseButton(
                      label: 'START',
                      held: _held.contains(YmirMouseButton.start),
                      onChanged: (d) => _button(YmirMouseButton.start, d),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _MouseButton extends StatelessWidget {
  final String label;
  final bool held;
  final ValueChanged<bool> onChanged;

  const _MouseButton({
    required this.label,
    required this.held,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: (_) => onChanged(true),
      onPointerUp: (_) => onChanged(false),
      onPointerCancel: (_) => onChanged(false),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        decoration: BoxDecoration(
          color: held ? Colors.lightBlueAccent : Colors.black54,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: Colors.white24),
        ),
        child: Text(
          label,
          style: const TextStyle(
            color: Colors.white,
            fontSize: 12,
            letterSpacing: 1.2,
          ),
        ),
      ),
    );
  }
}
