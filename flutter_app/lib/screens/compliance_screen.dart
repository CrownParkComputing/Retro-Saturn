// compliance_screen.dart — App Store / Play Store compliance page.
//
// One place that answers, on the device and without a network connection,
// every question a store review team asks about an emulator: what does it
// ship, what does it not ship, under what licences, and where are the
// files that prove it.
//
// It exists because those answers were previously spread between a
// first-run wizard the reviewer may never see, a text file in the
// repository they certainly will not, and nowhere at all.
//
// Unlike Retro-C64 there is no free-ROMs counterpart on Saturn: the
// Saturn's BIOS is still proprietary and Sega has not released a
// permissive substitute, so this page has no demo button. It is a
// document, not a different runtime.
//
// Matches the structure of Retro-C64's compliance_screen.dart so the
// two multiplatform shells read as sibling apps: numbered section
// headings, a short body each, and at the bottom the same kind of
// "Start over" affordance that lets a reviewer re-trigger the setup
// wizard without uninstalling the app.

import 'package:flutter/material.dart';

class ComplianceScreen extends StatelessWidget {
  /// Reopens the setup wizard. Supplied by the workbench, which owns that
  /// flag -- this screen does not navigate on its own.
  final VoidCallback? onRerunSetup;

  const ComplianceScreen({super.key, this.onRerunSetup});

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.fromLTRB(16, 14, 16, 24),
      children: [
        const Text('App Store / Play Store compliance',
            style: TextStyle(
                color: Colors.white,
                fontSize: 20,
                fontWeight: FontWeight.bold)),
        const SizedBox(height: 4),
        const Text(
          'Everything a store review needs, on the device. No network '
          'connection is required to check any of it.',
          style: TextStyle(color: Colors.white54, fontSize: 12),
        ),
        const SizedBox(height: 16),

        const _Head('1. No BIOS is shipped'),
        const _Body(
          'The Saturn BIOS is still under copyright by Sega. The app '
          'contains no BIOS and never distributes one. The emulator '
          'will not start without a BIOS supplied by the user, which '
          'is the same contract every Saturn emulator has used since '
          '1997.\n\n'
          'The only legitimate source is to dump it yourself from a Sega '
          'Saturn you own. The standard way:\n'
          '  1.  Install a flash cart with an SD slot (Satiator, MODE, '
          'Pseudo Saturn, AR 4-in-1, etc.) in the console\'s cartridge slot.\n'
          '  2.  Boot the cart\'s menu and run its BIOS dumper (Satiator: '
          '"Backup BIOS"; MODE: "ROM Dump"; Pseudo Saturn: "BIOS Dump").\n'
          '  3.  The cart writes a 512 KiB file to its SD card. The SHA-1 '
          'matches one of the two known-good dumps of the stock Saturn '
          'BIOS; the file is yours to use anywhere you like.\n\n'
          'Direct alternative: desolder the two BIOS ROM chips and read '
          'them with an EPROM programmer. Same 512 KiB output, no cart '
          'needed.\n\n'
          'Point the app at the resulting .bin file via Paths > "Pick a '
          'BIOS file" -- it does not need to be named saturn_bios.bin. '
          'A self-ripped BIOS supplied by you is exactly the same file '
          'as one you may already have for Mednafen / RetroArch / Ymir / '
          'Yabause; copy it across.\n\n'
          'On this device right now: no BIOS is installed (the app boots '
          'to an error screen until one is supplied).',
        ),

        const _Head('2. No games are shipped'),
        const _Body(
          'The app contains no game disc images. Everything playable '
          'comes from the user. It is a hardware emulator for a 1994 '
          'home console, permitted under App Review Guideline 4.7.\n\n'
          'If you have no Saturn games to hand and want to confirm the '
          'emulator boots something, there are public-domain homebrew '
          'Saturn titles (search "Saturn homebrew" on segaxtreme.net or '
          'the ssrf.ninja demo scene archive). A reviewer is welcome to '
          'drop a .chd or .cue into the Games folder under Paths -- the '
          'library grid lists it and the BIOS picks it up the same way '
          'it would a commercial disc.',
        ),

        const _Head('3. Free software, and where its source is'),
        const _Body(
          'The native emulator core is Ymir, by StrikerX3, under the '
          'GNU General Public License v3 or later. The Dart frontend '
          'and FFI bridge are part of this app and are likewise GPLv3. '
          'Both licence texts ship with the app. The source is at:\n\n'
          '  github.com/CrownParkComputing/Retro-Saturn\n'
          '  github.com/StrikerX3/Ymir\n\n'
          'The bundled Saturn theme assets (font, UI sprites) are '
          'original to this project under the same GPLv3 licence.',
        ),

        const _Head('4. Privacy'),
        const _Body(
          'No accounts, no sign-in, no analytics, no tracking, no data '
          'collected and none transmitted. The app makes no network '
          'request of its own. The one feature that can is cover-artwork '
          'matching, which does nothing until you enter a URL of your own '
          'in Settings.',
        ),

        const _Head('5. Reference'),
        const _Body(
          'Yaba Sanshiro 2 -- a long-standing GPLv2 Saturn emulator on '
          'the App Store -- states in its own listing that "for copyright '
          'protection, Yaba Sanshiro does not include BIOS data or games." '
          'Retro-Saturn follows the same contract:\n\n'
          '  apps.apple.com/us/app/yaba-sanshiro-2/id1549144351\n\n'
          'The yabause codebase that Yaba Sanshiro ships is GPLv2 and is '
          'the historical reference for Saturn emulation; Ymir is its modern '
          'successor.',
        ),

        if (onRerunSetup != null) ...[
          const _Head('Start over'),
          const _Body(
            'Reopens the first-run wizard, so a reviewer can confirm the '
            'BIOS / Games detection flow without uninstalling the app.',
          ),
          Align(
            alignment: Alignment.centerLeft,
            child: OutlinedButton.icon(
              onPressed: onRerunSetup,
              icon: const Icon(Icons.replay, size: 18),
              label: const Text('Back to the setup screen'),
            ),
          ),
        ],
      ],
    );
  }
}

class _Head extends StatelessWidget {
  final String label;
  const _Head(this.label);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 14, bottom: 4),
      child: Text(label,
          style: const TextStyle(
              color: Colors.white,
              fontSize: 14,
              fontWeight: FontWeight.bold)),
    );
  }
}

class _Body extends StatelessWidget {
  final String text;
  const _Body(this.text);

  @override
  Widget build(BuildContext context) {
    return Text(text,
        style: const TextStyle(color: Colors.white70, fontSize: 12, height: 1.4));
  }
}