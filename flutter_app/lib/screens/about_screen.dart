// about_screen.dart — Credits + version + license + a Logs entry
// (Logs used to be a sidebar destination; it is now a sub-section
// here, matching the Retro-C64 / Retro-Amiga pattern where
// diagnostics live one tap off the About page rather than next to
// the production rail slots).

import 'package:flutter/material.dart';
import 'package:retro_saturn/screens/logs_screen.dart';

class AboutScreen extends StatelessWidget {
  const AboutScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(title: const Text('ℹ️ About')),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        const Center(child: Icon(Icons.videogame_asset, size: 80, color: Colors.white70)),
        const SizedBox(height: 12),
        const Center(child: Text('Retro-Saturn',
            style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold))),
        const Center(child: Text('Sega Saturn emulator',
            style: TextStyle(color: Colors.white54, fontSize: 13))),
        const SizedBox(height: 16),
        const _Bullet('Native C++ core: ymir-core (StrikerX3/Ymir, GPLv3)'),
        const _Bullet('Frontend: Flutter 3.41 / Dart 3.11'),
        const _Bullet('Bridge: dart:ffi + plain C ABI'),
        const _Bullet('Platforms: Linux x64, Android arm64-v8a, iOS arm64'),
        const SizedBox(height: 24),
        const Text('Diagnostics',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        const SizedBox(height: 4),
        Card(
          margin: EdgeInsets.zero,
          child: ListTile(
            leading: const Icon(Icons.terminal),
            title: const Text('App log'),
            subtitle: const Text(
                'Tail of the bridge log. Useful for diagnosing BIOS / disc '
                'load / NVRAM save failures without adb logcat.',
                style: TextStyle(fontSize: 11, color: Colors.white54)),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => Navigator.of(context).push(
              MaterialPageRoute(builder: (_) => const LogsScreen()),
            ),
          ),
        ),
        const SizedBox(height: 24),
        const Text('License',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
        const SizedBox(height: 4),
        const Text(
            'This program is free software; you can redistribute it and/or '
            'modify it under the terms of the GNU GPLv3 as published by the '
            'Free Software Foundation. The Saturn BIOS, game disc images, '
            'and other commercial content are copyrighted by their respective '
            'owners and are not bundled with this app.',
            style: TextStyle(fontSize: 12)),
      ]),
    );
  }
}

class _Bullet extends StatelessWidget {
  final String text;
  const _Bullet(this.text);
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Text('• ', style: TextStyle(color: Colors.white54)),
        Expanded(child: Text(text, style: const TextStyle(fontSize: 12))),
      ]),
    );
  }
}