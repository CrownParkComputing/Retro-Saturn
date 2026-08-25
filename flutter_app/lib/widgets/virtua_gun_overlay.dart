// virtua_gun_overlay.dart — aiming and firing for games that need the Virtua
// Gun (Virtua Cop, House of the Dead, Die Hard Arcade's gun mode, ...).
// Mounted above the framebuffer in EmulatorScreen when the port's peripheral
// is virtuaGun.
//
// Two input devices, one gun:
//
//   touch  — tap the picture to aim and fire there. Drag to walk the aim
//            around with the trigger held, lift to release it. Tapping a
//            letterbox bar, or the RELOAD button, aims off screen.
//   mouse  — the pointer IS the gun. Moving aims, left button fires, right
//            button reloads. No click needed to aim, which is what makes a
//            desktop light-gun game playable.
//
// Coordinates go through mapToFramebuffer() rather than being scaled against
// this widget's own size: the picture is drawn with BoxFit.contain and is
// letterboxed on any panel that is not the Saturn's aspect, so the widget's
// top-left is not the picture's top-left.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/input/picture_mapping.dart';

class VirtuaGunOverlay extends StatefulWidget {
  final YmirCore core;
  final int port;

  /// Live emulated resolution, published by FramebufferView. Saturn titles
  /// change mode mid-game (320x224 to 704x512 and back), and an aim scaled
  /// against a stale resolution drifts further the further you aim from the
  /// centre.
  final ValueNotifier<Size>? frameSize;

  const VirtuaGunOverlay({
    super.key,
    required this.core,
    this.port = 1,
    this.frameSize,
  });

  @override
  State<VirtuaGunOverlay> createState() => _VirtuaGunOverlayState();
}

class _VirtuaGunOverlayState extends State<VirtuaGunOverlay> {
  /// Where to draw the crosshair, in widget-local coordinates.
  Offset? _crosshair;

  /// True while the aim is off the picture — drawn differently so a missed
  /// shot looks deliberate rather than broken.
  bool _offScreen = false;

  bool _trigger = false;
  bool _reload = false;
  bool _start = false;

  Size get _fb {
    final s = widget.frameSize?.value ?? Size.zero;
    // Before the first frame arrives, assume the most common Saturn mode.
    // Any aim taken this early is thrown away by the game anyway.
    if (s.width < 1 || s.height < 1) return const Size(320, 224);
    return s;
  }

  Size? get _widgetSize {
    final box = context.findRenderObject() as RenderBox?;
    if (box == null || !box.hasSize) return null;
    return box.size;
  }

  void _push() {
    final aim = _crosshair;
    final size = _widgetSize;
    FbPoint? p;
    if (aim != null && size != null) {
      p = mapToFramebuffer(aim, size, _fb.width.toInt(), _fb.height.toInt());
    }
    final off = p == null;
    if (off != _offScreen) setState(() => _offScreen = off);

    widget.core.setVirtuaGunState(
      widget.port,
      p?.x ?? kGunOffScreen,
      p?.y ?? kGunOffScreen,
      trigger: _trigger,
      start: _start,
      // A trigger pulled while off the picture IS a reload — that is the
      // gesture the arcade cabinet had, and games listen for it.
      reload: _reload || (off && _trigger),
    );
  }

  void _aimAt(Offset local, {bool? trigger}) {
    if (trigger != null) _trigger = trigger;
    setState(() => _crosshair = local);
    _push();
  }

  // ---- touch + mouse buttons -------------------------------------------

  void _onPointerDown(PointerDownEvent e) {
    if (e.kind == PointerDeviceKind.mouse) {
      // Buttons only: a mouse has already been aiming via hover.
      _trigger = (e.buttons & kPrimaryMouseButton) != 0;
      _reload = (e.buttons & kSecondaryMouseButton) != 0;
      _aimAt(e.localPosition);
      return;
    }
    _aimAt(e.localPosition, trigger: true);
  }

  void _onPointerMove(PointerMoveEvent e) {
    if (e.kind == PointerDeviceKind.mouse) {
      _trigger = (e.buttons & kPrimaryMouseButton) != 0;
      _reload = (e.buttons & kSecondaryMouseButton) != 0;
    }
    _aimAt(e.localPosition);
  }

  void _onPointerUp(PointerUpEvent e) {
    _trigger = false;
    _reload = false;
    if (e.kind == PointerDeviceKind.mouse) {
      // Keep aiming: the pointer is still on screen and still the gun.
      _aimAt(e.localPosition);
      return;
    }
    // Touch: the finger is gone, so nothing is aiming any more. Hold the
    // last position rather than snapping the crosshair away mid-shot.
    _push();
  }

  /// Mouse movement with no button held. This is the aim on desktop.
  void _onPointerHover(PointerHoverEvent e) => _aimAt(e.localPosition);

  // ---- on-screen buttons (touch) ---------------------------------------

  void _setReload(bool down) {
    _reload = down;
    // Reload is "shoot off screen", so drop the aim while it is held.
    if (down) setState(() => _crosshair = null);
    _push();
  }

  void _setStart(bool down) {
    _start = down;
    _push();
  }

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      // The crosshair is the cursor here; two pointers on screen at once
      // read as a rendering fault.
      cursor: SystemMouseCursors.none,
      // The buttons are SIBLINGS of the aiming surface, not children of it.
      // Nested inside, a press on RELOAD would also land on the aiming
      // Listener and yank the aim to the corner the button sits in -- both
      // widgets are in the hit path, and neither can stop the other.
      child: Stack(
        children: [
          Positioned.fill(
            child: Listener(
              behavior: HitTestBehavior.opaque,
              onPointerDown: _onPointerDown,
              onPointerMove: _onPointerMove,
              onPointerUp: _onPointerUp,
              onPointerHover: _onPointerHover,
              child: CustomPaint(
                painter: _CrosshairPainter(_crosshair, _offScreen, _trigger),
              ),
            ),
          ),
          // Inside the safe area, not flush to the edge.
          //
          // At bottom: 12 these rendered correctly and could not be pressed.
          // The device runs gesture navigation, which reserves the bottom
          // ~54px of the display (dumpsys: cur=1920x1080 vs app=1920x1026),
          // and the app draws edge-to-edge behind it -- so the home-gesture
          // handler took the touch before Flutter was offered it, and the
          // button looked live while being inert. SafeArea uses the system's
          // own inset; the extra padding keeps a thumb's clearance from the
          // swipe zone rather than sitting on its boundary.
          Positioned.fill(
            child: SafeArea(
              minimum: const EdgeInsets.all(16),
              child: Align(
                alignment: Alignment.bottomRight,
                child: Padding(
                  padding: const EdgeInsets.only(bottom: 12, right: 8),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      _GunButton(label: 'START', onChanged: _setStart),
                      const SizedBox(width: 8),
                      _GunButton(label: 'RELOAD', onChanged: _setReload),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// A hold-to-press button. Press and release both matter — the Saturn reads
/// button state, not taps — so this reports edges rather than clicks.
class _GunButton extends StatefulWidget {
  final String label;
  final ValueChanged<bool> onChanged;

  const _GunButton({required this.label, required this.onChanged});

  @override
  State<_GunButton> createState() => _GunButtonState();
}

class _GunButtonState extends State<_GunButton> {
  bool _down = false;

  void _set(bool down) {
    setState(() => _down = down);
    widget.onChanged(down);
  }

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: (_) => _set(true),
      onPointerUp: (_) => _set(false),
      onPointerCancel: (_) => _set(false),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        decoration: BoxDecoration(
          color: _down ? Colors.redAccent : Colors.black54,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: Colors.white24),
        ),
        child: Text(
          widget.label,
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

class _CrosshairPainter extends CustomPainter {
  final Offset? position;
  final bool offScreen;
  final bool firing;

  _CrosshairPainter(this.position, this.offScreen, this.firing);

  @override
  void paint(Canvas canvas, Size size) {
    final c = position;
    if (c == null) return;
    final paint = Paint()
      ..color = offScreen
          ? Colors.white38
          : (firing ? Colors.amberAccent : Colors.redAccent)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    canvas.drawLine(Offset(c.dx - 12, c.dy), Offset(c.dx + 12, c.dy), paint);
    canvas.drawLine(Offset(c.dx, c.dy - 12), Offset(c.dx, c.dy + 12), paint);
    canvas.drawCircle(c, firing ? 7 : 4, paint);
  }

  @override
  bool shouldRepaint(_CrosshairPainter old) =>
      old.position != position ||
      old.offScreen != offScreen ||
      old.firing != firing;
}
