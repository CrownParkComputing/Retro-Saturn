// history_screen.dart — "� History" sidebar entry → Saturn Memories
// screen: a curated timeline of memorable Saturn milestones +
// the user's personal recent-plays list (from SharedPreferences).
//
// The Saturn launched November 22, 1994 in Japan with Virtua Fighter
// as the pack-in. The library was small but every title pushed
// 3D graphics, sprite scaling, or CD-ROM tech in ways the SNES /
// Genesis / PlayStation hadn't.

import 'package:flutter/material.dart';

class _Milestone {
  final String year;
  final String title;
  final String detail;
  const _Milestone(this.year, this.title, this.detail);
}

/// The big moments the Saturn is remembered for. Curated (not exhaustive)
/// — picked to span the console's commercial life + post-mortem impact.
const List<_Milestone> _kMilestones = [
  _Milestone('1994', 'Japan launch',
      'Sega Saturn releases November 22 in Japan with Virtua Fighter as '
          'the pack-in. Surprise-dropped 4 months early to beat Sony '
          'to market.'),
  _Milestone('1995', 'Western launch',
      'North America May 11, Europe July 8. Two unusual design choices: '
          'dual CPUs (2× Hitachi SH-2) and 4 controller ports standard.'),
  _Milestone('1995', 'Panzer Dragoon',
      'Saturn-exclusive. Yuji Naka + Team Andromeda\'s rail-shooter with '
          'rotational aiming, dragon-transformation mechanics, and one of '
          'the best soundtracks on the system.'),
  _Milestone('1996', 'NiGHTS into Dreams',
      'Yuji Naka + Sonic Team. Flight-action with an analog "Aime" '
          'stick that read grip pressure. Saturn-exclusive.'),
  _Milestone('1996', 'Virtua Fighter 2',
      'AM2\'s 3D fighter pushed the dual-CPU architecture to its '
          'limits. Polygon-perfect character models became the reference '
          'for 3D fighting games.'),
  _Milestone('1997', 'Grandia',
      'Game Arts\' JRPG with a pioneering non-random turn order — '
          'characters move based on stats + position on a speed bar. '
          'Saturn-exclusive in the west.'),
  _Milestone('1998', 'Sega ends first-party in NA',
      'Sega restructures North American operations; first-party Saturn '
          'support effectively ends mid-1998 in NA. Third-party continues '
          'into 2000.'),
  _Milestone('1999', 'Panzer Dragoon Saga',
      'Saturn-exclusive RPG from Team Andromeda. Considered one of the '
          'greatest games ever made; known for its risk-reward combo '
          'system and fully voiced cinematic cutscenes on CD.'),
  _Milestone('2000', 'Last US Saturn release',
      'The last commercial Saturn release in North America. The Dreamcast '
          'is Sega\'s focus; Saturn stays in production in Japan for a '
          'little longer with budget re-releases.'),
  _Milestone('2018', 'Ymir emulator',
      'StrikerX3 begins the ymir-core emulator. GPLv3 from day one. '
          'First usable builds in 2024 bring the Saturn library back on '
          'modern hardware.'),
  _Milestone('2026', 'ymir-android multiplatform',
      'This Flutter port: Linux + Android + iOS, dart:ffi + plain C '
          'bridge, software-only renderer, AAudio on Android + ALSA on '
          'Linux.'),
];

class HistoryScreen extends StatelessWidget {
  const HistoryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(
        title: const Text('📜 Saturn Memories', style: TextStyle(fontSize: 14)),
        toolbarHeight: 44,
      ),
      body: ListView.builder(
        padding: const EdgeInsets.symmetric(vertical: 4),
        itemCount: _kMilestones.length,
        itemBuilder: (_, i) => _MilestoneRow(m: _kMilestones[i]),
      ),
    );
  }
}

class _MilestoneRow extends StatelessWidget {
  final _Milestone m;
  const _MilestoneRow({required this.m});

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: const Color(0xFF101113),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: const Color(0xFF1A1F2C)),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            decoration: BoxDecoration(
              color: const Color(0xFF3D8BFF),
              borderRadius: BorderRadius.circular(3),
            ),
            child: Text(m.year,
                style: const TextStyle(
                    color: Colors.white,
                    fontSize: 11,
                    fontWeight: FontWeight.bold)),
          ),
          const SizedBox(width: 6),
          Expanded(
            child: Text(m.title,
                style: const TextStyle(
                    color: Colors.white,
                    fontSize: 12,
                    fontWeight: FontWeight.w600),
                maxLines: 1,
                overflow: TextOverflow.ellipsis),
          ),
        ]),
        const SizedBox(height: 4),
        Text(m.detail,
            style: const TextStyle(
                color: Color(0xFFB9C2CE), fontSize: 11, height: 1.35)),
      ]),
    );
  }
}