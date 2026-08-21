// saturn_buttons.dart — 13 Saturn pad buttons + bit positions.
//
// Saturn pads have 12 buttons + Start = 13 total. The bridge stores
// them in a 16-bit mask; the LSB is YMIR_BUTTON_UP, the MSB is
// YMIR_BUTTON_R. Bit semantics: 1 = released (Ymir convention).

import '../ffi/ymir_bindings.dart';

/// Friendly labels shown in the controller mapper UI.
const Map<YmirButton, String> kSaturnButtonLabels = {
  YmirButton.up: 'D-Pad Up',
  YmirButton.down: 'D-Pad Down',
  YmirButton.left: 'D-Pad Left',
  YmirButton.right: 'D-Pad Right',
  YmirButton.start: 'Start',
  YmirButton.a: 'A',
  YmirButton.b: 'B',
  YmirButton.c: 'C',
  YmirButton.x: 'X',
  YmirButton.y: 'Y',
  YmirButton.z: 'Z',
  YmirButton.l: 'L',
  YmirButton.r: 'R',
};

/// Reverse lookup: gamepad service maps incoming buttons to these.
const Map<int, YmirButton> kGamepadDpadToSaturn = {
  // GamepadButton.dpadUp    → YmirButton.up   (filled in by service)
};

/// 16-bit mask helpers.
class SaturnPadMask {
  static int empty = 0xFFFF; // all released

  static int set(int mask, YmirButton button, bool pressed) {
    final bit = 1 << button.value;
    if (pressed) {
      return mask & ~bit;
    } else {
      return mask | bit;
    }
  }

  static bool isPressed(int mask, YmirButton button) =>
      (mask & (1 << button.value)) == 0;
}