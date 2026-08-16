# ADVANCED_SETTINGS — Peripheral selector + Virtua Gun

Ported from the original ymir-android Java work (see the historical
plan at `~/.hermes/plans/ymir-android-advanced-settings.md`).

## Peripheral types

| C enum                | Display name    | Notes                                                  |
|-----------------------|-----------------|--------------------------------------------------------|
| `YMIR_PERIPHERAL_NONE` | None            | No peripheral connected (port 2 default)              |
| `YMIR_PERIPHERAL_CONTROL_PAD` | Control Pad | Standard 12-button Saturn pad (port 1 default) |
| `YMIR_PERIPHERAL_ANALOG_PAD`  | 3D Control Pad | Analog stick + L/R triggers |
| `YMIR_PERIPHERAL_ARCADE_RACER`| Arcade Racer | Wheel on stick X, face buttons |
| `YMIR_PERIPHERAL_MISSION_STICK`| Mission Stick | Two analog sticks + throttle |
| `YMIR_PERIPHERAL_VIRTUA_GUN`  | Virtua Gun | Light gun (touch overlay) |
| `YMIR_PERIPHERAL_SHUTTLE_MOUSE`| Shuttle Mouse | 2-button mouse |

The `PeripheralSelector` widget renders 7 `ChoiceChip`s per port. Selecting
one calls `core.setPeripheralType(port, type)`; the bridge applies it at
end of the next frame (mailbox).

## Virtua Gun

Mounted above the framebuffer in `EmulatorScreen` when port 1 peripheral
== `VirtuaGun`. The overlay:

1. Subscribes to pointer events on the framebuffer bounds
2. Converts touch coords to VDP framebuffer pixels (clamped to
   `[1, fbW-1] × [1, fbH-1]`)
3. Calls `core.setVirtuaGunInput(port, x, y, trigger=true, start=false)`
4. On ACTION_UP calls `setVirtuaGunInput(port, 0xFFFF, 0xFFFF, false, false)`
   — ymir-core's "off-screen" signal that games like Virtual Cop
   interpret as "shot fired while off-screen = miss/reload"
5. Paints a red crosshair via `CustomPainter`

The resolution-change callback (`VDP.SetVDP2ResolutionChangedCallback`)
is wired by the bridge and updates `VirtuaGunOverlay`'s fb size so the
coordinate mapping tracks 704×512 hi-res as well as 320×224 NTSC.

## How the bridge coordinates with the emulator

```
EmulatorScreen
  └─ Listener / GestureDetector (touch coords)
       └─ core.setVirtuaGunInput(port, fbX, fbY, trigger, start)
            └─ ymir_bridge.cpp → YmirInstance::ports[port-1].gun_x/y/trigger
                 └─ fill_peripheral_report() builds the report on next INTBACK
                      ↓
                  SH-2 reads INTBACK → Virtua Gun state reaches the game
```

The same peripheral-state struct is read by `fill_peripheral_report`
each time ymir-core invokes the peripheral report callback (installed
in `ymir_bridge_create()` via `SetPeripheralReportCallback`).

## Gamepad service integration

When port 1's peripheral is the Virtua Gun, the Gamepad service
automatically maps the left stick to aim and the right trigger to the
trigger button. See `gamepad_service.dart:_pushStickAsAnalog`.

## Out of scope for v1

- Physical light gun (Sinden, AimTrak) — touch overlay only
- Haptic feedback on trigger pull — `Vibrator` API could add it
- Per-game peripheral override — currently process-wide