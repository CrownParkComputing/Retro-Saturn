// peripheral_type.dart — Display-friendly metadata for each
// YmirPeripheralType. Wraps the FFI enum (defined in ymir_bindings.dart)
// with the labels + short descriptions the controller mapper and the
// library's per-port picker show to users.
//
// The enum itself stays the source of truth for the native ABI; this file
// just decorates it. Anywhere the UI needs to render a peripheral label,
// go through [displayNameForPeripheral] / [descriptionForPeripheral] (or
// the `displayName` / `description` extension getters) instead of typing
// the enum's name out by hand.

import '../ffi/ymir_bindings.dart';

/// One row of the peripheral catalogue: the FFI enum value, the user-facing
/// label and a one-line description suitable for a tooltip / picker cell.
class PeripheralInfo {
  final YmirPeripheralType type;
  final String displayName;
  final String description;

  const PeripheralInfo({
    required this.type,
    required this.displayName,
    required this.description,
  });
}

/// The full catalogue, in the order the picker should show them (most
/// common Saturn inputs first, then the niche ones). The "none" entry is
/// last because selecting it is only meaningful when the user is clearing
/// a previously-bound device.
const List<PeripheralInfo> kPeripheralCatalog = [
  PeripheralInfo(
    type: YmirPeripheralType.controlPad,
    displayName: 'Digital Pad',
    description: 'Standard Saturn 12-button + Start pad — covers 99% of games.',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.analogPad,
    displayName: '3D Control Pad',
    description: 'Analog variant of the Saturn pad — used by a few late titles.',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.arcadeRacer,
    displayName: 'Arcade Racer',
    description: 'Steering-wheel peripheral for Sega Rally / Daytona etc.',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.missionStick,
    displayName: 'Mission Stick',
    description: 'Vertical grip for flight-stick titles (e.g. Ace Combat).',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.virtuaGun,
    displayName: 'Virtua Gun',
    description: 'Light-gun peripheral — needed for shooting gallery titles.',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.shuttleMouse,
    displayName: 'Shuttle Mouse',
    description: 'Saturn mouse for desktop / GUI navigation.',
  ),
  PeripheralInfo(
    type: YmirPeripheralType.none,
    displayName: 'Disconnected',
    description: 'No peripheral bound to this port.',
  ),
];

/// Lookup map: FFI enum value -> metadata row. Rebuilt lazily so adding a
/// new peripheral in `kPeripheralCatalog` above doesn't require touching the
/// lookup.
final Map<YmirPeripheralType, PeripheralInfo> _peripheralIndex = {
  for (final entry in kPeripheralCatalog) entry.type: entry,
};

/// Display name for [type], falling back to the enum's identifier rather
/// than throwing if a new peripheral is added upstream before this file
/// is updated.
String displayNameForPeripheral(YmirPeripheralType type) =>
    _peripheralIndex[type]?.displayName ?? type.name;

/// One-line description for [type]; empty string for unknown entries so a
/// freshly-added upstream peripheral renders as a blank tooltip instead of
/// crashing the picker.
String descriptionForPeripheral(YmirPeripheralType type) =>
    _peripheralIndex[type]?.description ?? '';

/// Ergonomic extension form: `YmirPeripheralType.controlPad.displayName`.
extension YmirPeripheralTypeDisplay on YmirPeripheralType {
  String get displayName => displayNameForPeripheral(this);
  String get description => descriptionForPeripheral(this);
}
