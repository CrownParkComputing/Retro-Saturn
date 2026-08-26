// gamepad_service.dart — Maps physical gamepads (via the `gamepads` plugin)
// to a 16-bit Saturn pad mask, then publishes via `maskChanges` stream.
//
// EmulatorScreen ORs this mask with the on-screen pad's mask and feeds
// the result to the bridge via setPadButton(...) per button. Pattern
// mirrors ViceMultiplatform's gamepad_service.dart verbatim with the
// bitmask expanded from 6 bits (C64) to 16 bits (Saturn — 12 buttons
// + spare bits).
//
// Stick Y convention: Up = NEGATIVE. Verified against Xbox Wireless
// + Retroid Pocket Flip2 — anything else is unverified.
//
// Active peripheral is queried from the bridge so the same physical
// gamepad maps sensibly to whichever peripheral is plugged into port 1:
//   - ControlPad/AnalogPad/MissionStick → D-pad + 6 face buttons + L/R
//   - ArcadeRacer                       → D-pad + face buttons + wheel on left-stick X
//   - VirtuaGun                         → crosshair on left-stick + trigger on A
//   - ShuttleMouse                      → left-stick as X delta + A as left button

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:gamepads/gamepads.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';

class GamepadService extends ChangeNotifier {
  final YmirCore core;
  final int port;

  StreamSubscription? _sub;
  bool _disposed = false;
  final bool _connected = false;

  /// 16-bit Saturn mask. 1 = released (Ymir convention).
  int _mask = 0xFFFF;

  /// Stick state (analog pad + virtua gun + shuttle mouse).
  double _stickX = 0.0;
  double _stickY = 0.0;

  /// Fired when the pad's back/select button goes down. The Saturn pad has
  /// no Select, so the button is free -- the session screen uses it to open
  /// the pause menu, because reaching for the touchscreen mid-game on a
  /// handheld means letting go of the controls.
  VoidCallback? onMenu;

  /// Latest trigger / start for Virtua Gun.
  bool _triggerPressed = false;
  bool _startPressed = false;

  /// Stream of mask changes for the emulator screen to OR with on-screen input.
  final StreamController<int> _maskChanges = StreamController<int>.broadcast();
  Stream<int> get maskChanges => _maskChanges.stream;
  int get currentMask => _mask;
  bool get connected => _connected;

  GamepadService(this.core, {this.port = 1}) {
    _sub = Gamepads.normalizedEvents.listen(_handleEvent,
        onError: (Object e) => debugPrint('gamepad error: $e'));
  }

  /// Visible-for-testing event handler — same logic the real stream uses.
  @visibleForTesting
  void handleEvent(NormalizedGamepadEvent event) {
    final button = event.button;
    if (button != null) {
      final down = event.value != 0;
      switch (button) {
        case GamepadButton.dpadUp:    _setBit(YmirButton.up,    down); break;
        case GamepadButton.dpadDown:  _setBit(YmirButton.down,  down); break;
        case GamepadButton.dpadLeft:  _setBit(YmirButton.left,  down); break;
        case GamepadButton.dpadRight: _setBit(YmirButton.right, down); break;
        case GamepadButton.a:         _setBit(YmirButton.a, down); break;
        case GamepadButton.b:         _setBit(YmirButton.b, down); break;
        case GamepadButton.x:         _setBit(YmirButton.x, down); break;
        case GamepadButton.y:         _setBit(YmirButton.y, down); break;
        case GamepadButton.start:     _setBit(YmirButton.start, down); break;
        case GamepadButton.back:      if (down) onMenu?.call(); break;
        case GamepadButton.leftBumper:  _setBit(YmirButton.l, down); break;
        case GamepadButton.rightBumper: _setBit(YmirButton.r, down); break;
        default: break;
      }
      return;
    }

    final axis = event.axis;
    if (axis == GamepadAxis.leftStickX) {
      _stickX = event.value;
    } else if (axis == GamepadAxis.leftStickY) {
      _stickY = event.value;
    } else if (axis == GamepadAxis.rightTrigger) {
      _triggerPressed = event.value > 0.5;
    } else if (axis == GamepadAxis.leftTrigger) {
      _startPressed = event.value > 0.5;
    } else {
      return;
    }

    _pushStickAsAnalog();
  }

  void _handleEvent(NormalizedGamepadEvent event) {
    try { handleEvent(event); } catch (e) { debugPrint('gamepad handler: $e'); }
  }

  void _setBit(YmirButton b, bool down) {
    final mask = (1 << b.value);
    if (down) {
      _mask &= ~mask;
    } else {
      _mask |= mask;
    }
    _maskChanges.add(_mask);
    core.setPadButton(port, b, down);
    notifyListeners();
  }

  /// Maps stick X/Y to the appropriate peripheral-type semantics.
  void _pushStickAsAnalog() {
    final ptype = core.getPeripheralType(port);
    switch (ptype) {
      case YmirPeripheralType.virtuaGun:
        final x = (160 + (_stickX * 160)).toInt().clamp(1, 319);
        final y = (112 + (_stickY * 112)).toInt().clamp(1, 223);
        core.setVirtuaGunInput(port, x, y, _triggerPressed, _startPressed);
        break;
      case YmirPeripheralType.shuttleMouse:
        break;
      case YmirPeripheralType.arcadeRacer:
        final wheel = (128 + (_stickX * 127)).toInt().clamp(0, 255);
        core.setAnalogAxis(port, wheel, 128, 128, 128);
        break;
      case YmirPeripheralType.analogPad:
      case YmirPeripheralType.missionStick:
        final lx = (128 + (_stickX * 127)).toInt().clamp(0, 255);
        final ly = (128 + (_stickY * 127)).toInt().clamp(0, 255);
        core.setAnalogAxis(port, lx, ly, 128, 128);
        break;
      default:
        break;
    }
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _sub?.cancel();
    _maskChanges.close();
    super.dispose();
  }
}