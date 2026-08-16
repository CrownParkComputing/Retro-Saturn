// peripheral_selector.dart — Lets the user pick which peripheral is
// plugged into SMPC port 1 (and port 2). Calls
// core.setPeripheralType(port, type) which the bridge applies at end
// of the next frame.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';

class PeripheralSelector extends StatelessWidget {
  final YmirCore core;
  final int port;

  const PeripheralSelector({super.key, required this.core, this.port = 1});

  static const _labels = {
    YmirPeripheralType.none:          ('None',          'No peripheral connected'),
    YmirPeripheralType.controlPad:   ('Control Pad',   'Standard 12-button Saturn pad'),
    YmirPeripheralType.analogPad:    ('3D Control Pad','Analog stick + L/R triggers'),
    YmirPeripheralType.arcadeRacer:  ('Arcade Racer',  'Wheel + face buttons'),
    YmirPeripheralType.missionStick: ('Mission Stick', 'Two analog sticks + throttle'),
    YmirPeripheralType.virtuaGun:    ('Virtua Gun',    'Light gun (touch overlay)'),
    YmirPeripheralType.shuttleMouse: ('Shuttle Mouse', '2-button mouse'),
  };

  @override
  Widget build(BuildContext context) {
    final current = core.getPeripheralType(port);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Port $port peripheral',
                style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            Wrap(spacing: 8, children: [
              for (final t in YmirPeripheralType.values)
                ChoiceChip(
                  label: Text(_labels[t]!.$1),
                  selected: current == t,
                  onSelected: (sel) {
                    if (sel) core.setPeripheralType(port, t);
                  },
                ),
            ]),
            const SizedBox(height: 8),
            Text(_labels[current]!.$2,
                style: Theme.of(context).textTheme.bodySmall),
          ],
        ),
      ),
    );
  }
}