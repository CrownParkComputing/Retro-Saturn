// about_screen_screen.dart — Credits + version + license.

import 'package:flutter/material.dart';

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
        const Center(child: Text('Ymir Multiplatform',
            style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold))),
        const Center(child: Text('Sega Saturn emulator',
            style: TextStyle(color: Colors.white54, fontSize: 13))),
        const SizedBox(height: 16),
        const _Bullet('Native C++ core: ymir-core (StrikerX3/Ymir, GPLv3)'),
        const _Bullet('Frontend: Flutter 3.41 / Dart 3.11'),
        const _Bullet('Bridge: dart:ffi + plain C ABI'),
        const _Bullet('Platforms: Linux x64, Android arm64-v8a, iOS arm64'),
        const SizedBox(height: 24),
        const Text('License', style: TextStyle(fontWeight: FontWeight.bold)),
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