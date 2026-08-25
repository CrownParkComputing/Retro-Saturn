// core_option.dart — the catalogue of Ymir core options the app exposes.
//
// ymir-core's Configuration is the source of truth for what these do; this
// file is what the user reads, plus the bounds the core will actually accept.
// Bounds are duplicated here deliberately: the screen has to know what to
// render a slider between, and the bridge validates the same numbers again so
// a wrong one is refused rather than clamped silently.
//
// Deliberately NOT exposed:
//   debugTracing        an alternative code path with every debug facility on,
//                       for a large speed loss. A developer switch, not a
//                       player one.
//   threadedSCSP        documented upstream as unimplemented.
//   RTC mode/strategy   only meaningful for deterministic runs (TAS work).
//   preferredRegionOrder a list rather than a value, and the autodetect
//                       toggle covers what a player actually wants.

import 'package:retro_saturn/ffi/ymir_bindings.dart';

enum CoreOptionKind { toggle, choice, slider }

class CoreOptionChoice {
  final int value;
  final String label;
  const CoreOptionChoice(this.value, this.label);
}

class CoreOption {
  final YmirCoreOption id;
  final String title;
  final String description;
  final CoreOptionKind kind;
  final int defaultValue;

  /// choice options only
  final List<CoreOptionChoice> choices;

  /// slider options only
  final int min;
  final int max;
  final int step;
  final String Function(int value)? format;

  /// Shown as a warning in the UI. Some options only take effect on the next
  /// boot, and one of them resets the machine the moment it changes.
  final String? caveat;

  const CoreOption({
    required this.id,
    required this.title,
    required this.description,
    required this.kind,
    required this.defaultValue,
    this.choices = const [],
    this.min = 0,
    this.max = 1,
    this.step = 1,
    this.format,
    this.caveat,
  });

  String labelFor(int value) {
    switch (kind) {
      case CoreOptionKind.toggle:
        return value != 0 ? 'On' : 'Off';
      case CoreOptionKind.choice:
        return choices
            .firstWhere((c) => c.value == value,
                orElse: () => CoreOptionChoice(value, '$value'))
            .label;
      case CoreOptionKind.slider:
        return format?.call(value) ?? '$value';
    }
  }

  /// Clamped and snapped to something the core will accept.
  int sanitize(int value) {
    switch (kind) {
      case CoreOptionKind.toggle:
        return value != 0 ? 1 : 0;
      case CoreOptionKind.choice:
        return choices.any((c) => c.value == value) ? value : defaultValue;
      case CoreOptionKind.slider:
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
  }
}

/// In the order the options screen shows them: the ones that change how the
/// machine feels first, the accuracy switches after.
const List<CoreOption> kCoreOptions = [
  CoreOption(
    id: YmirCoreOption.sh2Overclock,
    title: 'SH-2 overclock',
    description:
        'Runs the Saturn’s two main CPUs faster than the real thing. Can '
        'clear up slowdown and long loading in CPU-heavy games; too much of it '
        'makes some games run their logic too fast or misbehave.',
    kind: CoreOptionKind.slider,
    defaultValue: 100,
    min: 50,
    max: 300,
    step: 10,
    format: _percent,
  ),
  CoreOption(
    id: YmirCoreOption.cdReadSpeed,
    title: 'CD read speed',
    description:
        'How fast the emulated CD drive reads. 2x is the real Saturn. Higher '
        'cuts loading, but a few games time their streaming against the drive '
        'and will stutter or desync.',
    kind: CoreOptionKind.slider,
    defaultValue: 2,
    min: 2,
    max: 200,
    step: 2,
    format: _times,
  ),
  CoreOption(
    id: YmirCoreOption.videoStandard,
    title: 'Video standard',
    description:
        'NTSC runs at 60Hz, PAL at 50Hz. Region autodetect usually picks this '
        'for you from the disc.',
    kind: CoreOptionKind.choice,
    defaultValue: 0,
    choices: [
      CoreOptionChoice(0, 'NTSC (60Hz)'),
      CoreOptionChoice(1, 'PAL (50Hz)'),
    ],
    caveat: 'Takes effect on the next boot.',
  ),
  CoreOption(
    id: YmirCoreOption.autodetectRegion,
    title: 'Autodetect region',
    description:
        'Picks the machine’s region from the regions the disc supports, '
        'rather than forcing one.',
    kind: CoreOptionKind.toggle,
    defaultValue: 1,
  ),
  CoreOption(
    id: YmirCoreOption.audioInterpolation,
    title: 'Audio interpolation',
    description:
        'Linear is what the real SCSP does. Nearest neighbour is harsher and '
        'noticeably more aliased, and costs slightly less.',
    kind: CoreOptionKind.choice,
    defaultValue: 1,
    choices: [
      CoreOptionChoice(0, 'Nearest neighbour'),
      CoreOptionChoice(1, 'Linear (accurate)'),
    ],
  ),
  CoreOption(
    id: YmirCoreOption.emulateSh2Cache,
    title: 'Emulate SH-2 cache',
    description:
        'More accurate, and needed by a few specific games. Costs a little '
        'speed and purges the caches when switched.',
    kind: CoreOptionKind.toggle,
    defaultValue: 0,
  ),
  CoreOption(
    id: YmirCoreOption.threadedVdp1,
    title: 'Threaded VDP1',
    description:
        'Runs the sprite/polygon renderer on its own thread. Faster on '
        'multi-core devices; turn off if you are chasing a rendering glitch.',
    kind: CoreOptionKind.toggle,
    defaultValue: 1,
  ),
  CoreOption(
    id: YmirCoreOption.threadedVdp2,
    title: 'Threaded VDP2',
    description: 'Runs the background/tilemap renderer on its own thread.',
    kind: CoreOptionKind.toggle,
    defaultValue: 1,
  ),
  CoreOption(
    id: YmirCoreOption.threadedDeinterlace,
    title: 'Threaded deinterlacer',
    description:
        'Deinterlaces on its own thread. Only applies when the VDP2 renderer '
        'is threaded, and only to interlaced modes.',
    kind: CoreOptionKind.toggle,
    defaultValue: 1,
  ),
  CoreOption(
    id: YmirCoreOption.cdBlockLle,
    title: 'CD block low-level emulation',
    description:
        'Emulates the CD block’s own processor instead of simulating its '
        'behaviour. More accurate for a handful of titles, and slower.',
    kind: CoreOptionKind.toggle,
    defaultValue: 0,
    caveat: 'Needs the CD block ROM, and hard-resets the machine when changed.',
  ),
];

String _percent(int v) => '$v%';
String _times(int v) => '${v}x';

CoreOption coreOptionFor(YmirCoreOption id) =>
    kCoreOptions.firstWhere((o) => o.id == id);
