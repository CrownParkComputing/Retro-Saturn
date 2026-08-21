// audio_settings_screen.dart — Audio configuration. The Ymir bridge
// exposes a mute toggle and a read-only audio level meter; the rail
// gets a dedicated destination so the user can flip the mute without
// opening the in-game settings drawer. The meter's live tail is shown
// on the in-emulator status bar (see workbench_screen.dart's
// _statusBar) so the user can confirm audio is actually flowing.

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';

class AudioSettingsScreen extends StatefulWidget {
  final YmirCore core;
  const AudioSettingsScreen({super.key, required this.core});

  @override
  State<AudioSettingsScreen> createState() => _AudioSettingsScreenState();
}

class _AudioSettingsScreenState extends State<AudioSettingsScreen> {
  bool _muted = false;

  @override
  void initState() {
    super.initState();
    _muted = widget.core.audioMuted;
  }

  void _setMuted(bool v) {
    widget.core.setAudioMuted(v);
    setState(() => _muted = v);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(title: const Text('🔊 Audio')),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        const _Section('Output'),
        SwitchListTile(
          title: const Text('Mute'),
          subtitle: const Text(
              'Silences the emulated SCSP output. The core keeps running, '
              'so un-muting snaps back to where the emulation is now.',
              style: TextStyle(fontSize: 11, color: Colors.white54)),
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
        Text('Runtime: ${widget.core.status}',
            style: const TextStyle(fontSize: 12, fontFamily: 'monospace')),
        Text('FPS: ${widget.core.fps}',
            style: const TextStyle(fontSize: 12, fontFamily: 'monospace')),
      ]),
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
      child: Text(label,
          style: const TextStyle(
              fontSize: 11,
              color: Colors.white54,
              fontWeight: FontWeight.bold)),
    );
  }
}

class _AudioLevelBar extends StatelessWidget {
  final int level;
  final bool muted;
  const _AudioLevelBar({required this.level, required this.muted});

  @override
  Widget build(BuildContext context) {
    final pct = (level.clamp(0, 100)) / 100.0;
    return Container(
      height: 18,
      decoration: BoxDecoration(
        color: Colors.white12,
        borderRadius: BorderRadius.circular(3),
      ),
      clipBehavior: Clip.antiAlias,
      child: Row(children: [
        AnimatedFractionallySizedBox(
          duration: const Duration(milliseconds: 100),
          widthFactor: muted ? 0 : pct,
          heightFactor: 1,
          child: Container(
            decoration: BoxDecoration(
              gradient: const LinearGradient(
                colors: [Color(0xFF4040E0), Color(0xFF60A0FF)],
              ),
              borderRadius: BorderRadius.circular(3),
            ),
          ),
        ),
      ]),
    );
  }
}
