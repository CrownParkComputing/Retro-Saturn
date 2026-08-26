// input_settings_screen.dart — Saturn pad mapping + peripheral type.
// Mirrors ViceMultiplatform's input_settings_screen.dart pattern
// (left-handed toggle, on-screen pad mode, joystick port, custom
// buttons list, per-key remap dialog).

import 'package:flutter/material.dart';
import 'package:retro_saturn/data/saturn_buttons.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/widgets/peripheral_selector.dart';

class InputSettingsScreen extends StatefulWidget {
  final YmirCore core;
  const InputSettingsScreen({super.key, required this.core});

  @override
  State<InputSettingsScreen> createState() => _InputSettingsScreenState();
}

class _InputSettingsScreenState extends State<InputSettingsScreen> {
  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Padding(
          padding: EdgeInsets.only(bottom: 8),
          child: Text(
            'Peripherals',
            style: TextStyle(
              fontSize: 11,
              color: Colors.white54,
              fontWeight: FontWeight.bold,
            ),
          ),
        ),
        PeripheralSelector(core: widget.core, port: 1),
        const SizedBox(height: 12),
        PeripheralSelector(core: widget.core, port: 2),
        const SizedBox(height: 24),
        const Padding(
          padding: EdgeInsets.only(bottom: 8),
          child: Text(
            'Saturn pad buttons',
            style: TextStyle(
              fontSize: 11,
              color: Colors.white54,
              fontWeight: FontWeight.bold,
            ),
          ),
        ),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(8),
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final b in YmirButton.values)
                  Chip(
                    label: Text(kSaturnButtonLabels[b] ?? b.name),
                    backgroundColor: const Color(0xFF202028),
                  ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                const Icon(Icons.gamepad, size: 18),
                const SizedBox(width: 8),
                const Expanded(
                  child: Text(
                    'Gamepad input uses the platform gamepads plugin. Connect a '
                    'Bluetooth / USB Xbox-style controller and the d-pad, A/B/X/Y, '
                    'L/R + Start are mapped to Saturn buttons automatically. '
                    'Per-peripheral semantics (analog stick on 3D pad / wheel on '
                    'Arcade Racer / aim on Virtua Gun) are picked from the peripheral '
                    'type above.',
                    style: TextStyle(fontSize: 12),
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
