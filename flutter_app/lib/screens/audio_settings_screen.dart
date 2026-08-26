// audio_settings_screen.dart — The A/V page: display defaults (screen
// fill, on-screen pad) plus audio configuration. The Ymir bridge
// exposes a mute toggle and a read-only audio level meter; the rail
// gets a dedicated destination so the user can flip the mute without
// opening the in-game settings drawer. The meter's live tail is shown
// on the in-emulator status bar (see workbench_screen.dart's
// _statusBar) so the user can confirm audio is actually flowing.

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_prefs.dart';

class AudioSettingsScreen extends StatefulWidget {
  final YmirCore core;
  const AudioSettingsScreen({super.key, required this.core});

  @override
  State<AudioSettingsScreen> createState() => _AudioSettingsScreenState();
}

class _AudioSettingsScreenState extends State<AudioSettingsScreen> {
  bool _muted = false;
  bool _fill = AppPrefs.screenFill;
  bool _padDefault = false;

  @override
  void initState() {
    super.initState();
    _muted = widget.core.audioMuted;
    AppPrefs.getShowPadDefault().then((v) {
      if (mounted) setState(() => _padDefault = v);
    });
  }

  void _setMuted(bool v) {
    widget.core.setAudioMuted(v);
    setState(() => _muted = v);
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        // Display defaults live HERE, on the launcher side, so they can be
        // set before a game starts -- the session rail can still change
        // them mid-game.
        const _Section('Screen'),
        SwitchListTile(
          title: const Text('Stretch to fill the screen'),
          subtitle: const Text(
            "16:9 widescreen stretch instead of the Saturn's 4:3 shape. "
            'The Fill tool on the in-game rail changes this too.',
            style: TextStyle(fontSize: 11, color: Colors.white54),
          ),
          value: _fill,
          onChanged: (v) {
            AppPrefs.setScreenFill(v);
            setState(() => _fill = v);
          },
        ),
        SwitchListTile(
          title: const Text('Show the on-screen pad'),
          subtitle: const Text(
            'Whether a new session starts with the touch pad visible. '
            'The Pad tool on the in-game rail toggles it mid-game.',
            style: TextStyle(fontSize: 11, color: Colors.white54),
          ),
          value: _padDefault,
          onChanged: (v) {
            AppPrefs.setShowPadDefault(v);
            setState(() => _padDefault = v);
          },
        ),
        const SizedBox(height: 24),
        const _Section('Output'),
        SwitchListTile(
          title: const Text('Mute'),
          subtitle: const Text(
            'Silences the emulated SCSP output. The core keeps running, '
            'so un-muting snaps back to where the emulation is now.',
            style: TextStyle(fontSize: 11, color: Colors.white54),
          ),
          value: _muted,
          onChanged: _setMuted,
        ),
        const SizedBox(height: 24),
        const _Section('Live level'),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 4),
          child: _AudioLevelBar(level: widget.core.audioLevel, muted: _muted),
        ),
        const SizedBox(height: 8),
        const Text(
          'The bar samples the SCSP output as it is mixed. The peak '
          'follows the in-game music; if the bar is flat while music '
          'is playing, the BIOS has not finished booting yet, or the '
          'disc image is reading-only audio.',
          style: TextStyle(fontSize: 11, color: Colors.white38),
        ),
        const SizedBox(height: 24),
        const _Section('Bridge status'),
        Text(
          'Runtime: ${widget.core.status}',
          style: const TextStyle(fontSize: 12, fontFamily: 'monospace'),
        ),
        Text(
          'FPS: ${widget.core.fps}',
          style: const TextStyle(fontSize: 12, fontFamily: 'monospace'),
        ),
      ],
    );
  }
}

class _Section extends StatelessWidget {
  final String label;
  const _Section(this.label);
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Text(
        label,
        style: const TextStyle(
          fontSize: 11,
          color: Colors.white54,
          fontWeight: FontWeight.bold,
        ),
      ),
    );
  }
}

class _AudioLevelBar extends StatelessWidget {
  final int level;
  final bool muted;
  const _AudioLevelBar({required this.level, required this.muted});

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 18,
      decoration: BoxDecoration(
        color: Colors.white12,
        borderRadius: BorderRadius.circular(3),
      ),
      clipBehavior: Clip.antiAlias,
      child: LayoutBuilder(
        builder: (context, constraints) => Row(
          children: [
            AnimatedContainer(
              duration: const Duration(milliseconds: 100),
              width: audioBarFillWidth(constraints.maxWidth, level, muted),
              decoration: BoxDecoration(
                gradient: const LinearGradient(
                  colors: [Color(0xFF4040E0), Color(0xFF60A0FF)],
                ),
                borderRadius: BorderRadius.circular(3),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Width of the filled part of the level meter.
///
/// Computed rather than expressed as a fraction, because a fraction is what
/// broke it. AnimatedFractionallySizedBox multiplies the incoming maxWidth by
/// the factor, and this bar can be handed an unbounded width -- at which point
/// a level of zero makes that `infinity * 0.0`, which is NaN, and layout dies
/// with "BoxConstraints has NaN values in minWidth and maxWidth".
///
/// Zero is not a rare case either: the level is zero whenever nothing is
/// playing, so the screen was at its most fragile on a machine that had not
/// been started -- which is the state a store reviewer opens it in.
///
/// An unbounded width has no honest fraction, so the bar falls back to a fixed
/// size and stays readable instead of taking the whole scroll extent.
double audioBarFillWidth(double maxWidth, int level, bool muted) {
  if (muted) return 0;
  final pct = level.clamp(0, 100) / 100.0;
  const fallback = 240.0;
  final width = maxWidth.isFinite ? maxWidth : fallback;
  return width * pct;
}
