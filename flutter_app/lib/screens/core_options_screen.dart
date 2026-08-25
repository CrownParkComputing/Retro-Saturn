// core_options_screen.dart — Ymir core options, before or during a game.
//
// The same list widget serves two places: the Core page in the rail (set them
// before launching anything) and the in-game settings drawer (change them with
// a game running). Every option here is one ymir-core accepts at runtime, so
// there is no "apply" button -- moving a control moves the machine. The two
// that only take effect on the next boot say so on the row itself rather than
// pretending otherwise.

import 'package:flutter/material.dart';
import 'package:retro_saturn/data/core_option.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/core_options_service.dart';

class CoreOptionsScreen extends StatelessWidget {
  final YmirCore core;

  const CoreOptionsScreen({super.key, required this.core});

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(20),
      children: [
        Text('Core options', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 6),
        const Text(
          'These change how the emulated Saturn behaves. They apply straight '
          'away, with or without a game running, and are remembered for next '
          'time.',
          style: TextStyle(color: Colors.white54, height: 1.4),
        ),
        const SizedBox(height: 20),
        CoreOptionsList(core: core),
      ],
    );
  }
}

/// The rows themselves, without the page chrome, so the in-game drawer can
/// show exactly the same controls in a narrower space.
class CoreOptionsList extends StatefulWidget {
  final YmirCore core;
  final bool compact;

  const CoreOptionsList({super.key, required this.core, this.compact = false});

  @override
  State<CoreOptionsList> createState() => _CoreOptionsListState();
}

class _CoreOptionsListState extends State<CoreOptionsList> {
  Future<void> _set(CoreOption opt, int value) async {
    final applied = await CoreOptionsService.set(widget.core, opt.id, value);
    if (!mounted) return;
    setState(() {});
    if (applied != opt.sanitize(value)) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('The core would not accept that ${opt.title} value.'),
      ));
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (final opt in kCoreOptions) ...[
          _OptionRow(
            option: opt,
            value: CoreOptionsService.valueOf(opt.id),
            compact: widget.compact,
            onChanged: (v) => _set(opt, v),
          ),
          const SizedBox(height: 14),
        ],
        if (CoreOptionsService.anyChanged)
          Align(
            alignment: Alignment.centerLeft,
            child: TextButton.icon(
              icon: const Icon(Icons.settings_backup_restore, size: 18),
              label: const Text('Reset all to core defaults'),
              onPressed: () async {
                await CoreOptionsService.resetAll(widget.core);
                if (mounted) setState(() {});
              },
            ),
          ),
      ],
    );
  }
}

class _OptionRow extends StatelessWidget {
  final CoreOption option;
  final int value;
  final bool compact;
  final ValueChanged<int> onChanged;

  const _OptionRow({
    required this.option,
    required this.value,
    required this.compact,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    final isDefault = value == option.defaultValue;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(children: [
          Expanded(
            child: Text(
              option.title,
              style: const TextStyle(
                  color: Colors.white, fontWeight: FontWeight.w600),
            ),
          ),
          if (!isDefault)
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: Colors.blueAccent.withValues(alpha: 0.22),
                borderRadius: BorderRadius.circular(4),
              ),
              child: const Text('changed',
                  style: TextStyle(color: Colors.white70, fontSize: 11)),
            ),
        ]),
        if (!compact) ...[
          const SizedBox(height: 2),
          Text(option.description,
              style: const TextStyle(
                  color: Colors.white54, fontSize: 12, height: 1.35)),
        ],
        if (option.caveat != null) ...[
          const SizedBox(height: 4),
          Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const Icon(Icons.info_outline, size: 13, color: Colors.amber),
            const SizedBox(width: 5),
            Expanded(
              child: Text(option.caveat!,
                  style: const TextStyle(color: Colors.amber, fontSize: 11.5)),
            ),
          ]),
        ],
        const SizedBox(height: 6),
        _control(context),
      ],
    );
  }

  Widget _control(BuildContext context) {
    switch (option.kind) {
      case CoreOptionKind.toggle:
        return Align(
          alignment: Alignment.centerLeft,
          child: Switch(
            value: value != 0,
            onChanged: (v) => onChanged(v ? 1 : 0),
          ),
        );
      case CoreOptionKind.choice:
        return Align(
          alignment: Alignment.centerLeft,
          child: SegmentedButton<int>(
            segments: [
              for (final c in option.choices)
                ButtonSegment<int>(value: c.value, label: Text(c.label)),
            ],
            selected: {value},
            showSelectedIcon: false,
            onSelectionChanged: (s) => onChanged(s.first),
          ),
        );
      case CoreOptionKind.slider:
        final divisions = ((option.max - option.min) / option.step).round();
        return Row(children: [
          Expanded(
            child: Slider(
              value: value.toDouble().clamp(
                  option.min.toDouble(), option.max.toDouble()),
              min: option.min.toDouble(),
              max: option.max.toDouble(),
              divisions: divisions > 0 ? divisions : null,
              label: option.labelFor(value),
              onChanged: (v) => onChanged(v.round()),
            ),
          ),
          SizedBox(
            width: 56,
            child: Text(
              option.labelFor(value),
              textAlign: TextAlign.right,
              style: const TextStyle(color: Colors.white70),
            ),
          ),
        ]);
    }
  }
}
