// save_states_screen.dart — the Resume view: every save state on disk, newest
// first, with the picture the game was showing when it was written.
//
// Slot 0 of each game is the automatic one (written when a session ends), so
// the top of this list is nearly always "carry on where you left off". The
// rest are the user's own slots.
//
// Resuming from here has to mount the disc before restoring: the core
// validates the disc and BIOS hashes and refuses a state that belongs to
// another machine. So a tap hands the entry AND the slot back to the
// workbench, which launches the game and lets the emulator screen restore the
// state once the disc is actually in.

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/services/library_scanner.dart';
import 'package:retro_saturn/services/save_state_service.dart';

/// Handed back to the workbench: launch [entry], then restore [slot].
typedef ResumeRequest = void Function(MediaEntry entry, int slot);

class SaveStatesScreen extends StatefulWidget {
  final String gamesFolder;
  final ResumeRequest onResume;

  const SaveStatesScreen({
    super.key,
    required this.gamesFolder,
    required this.onResume,
  });

  @override
  State<SaveStatesScreen> createState() => _SaveStatesScreenState();
}

class _SaveStatesScreenState extends State<SaveStatesScreen> {
  bool _loading = true;
  final List<({MediaEntry entry, SaveSlot slot})> _saved = [];

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    setState(() => _loading = true);
    final found = <({MediaEntry entry, SaveSlot slot})>[];
    try {
      final entries = LibraryScanner.scan(widget.gamesFolder).entries;
      for (final entry in entries) {
        for (final slot in await SaveStateService.slotsFor(entry)) {
          if (!slot.isEmpty) found.add((entry: entry, slot: slot));
        }
      }
    } catch (_) {
      // A folder that has gone away (SD card pulled) leaves the list empty
      // rather than throwing into the panel.
    }
    found.sort((a, b) => b.slot.savedAt!.compareTo(a.slot.savedAt!));
    if (!mounted) return;
    setState(() {
      _saved
        ..clear()
        ..addAll(found);
      _loading = false;
    });
  }

  Future<void> _delete(MediaEntry entry, SaveSlot slot) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Delete ${slot.label}?'),
        content: Text(
            '${entry.displayName}\nSaved ${_when(slot.savedAt!)}.\n\nThis cannot be undone.'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('Keep')),
          TextButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('Delete')),
        ],
      ),
    );
    if (ok != true) return;
    await SaveStateService.delete(entry, slot.index);
    await _refresh();
  }

  static String _when(DateTime t) {
    final now = DateTime.now();
    final diff = now.difference(t);
    if (diff.inMinutes < 1) return 'just now';
    if (diff.inMinutes < 60) return '${diff.inMinutes} min ago';
    if (diff.inHours < 24) return '${diff.inHours} h ago';
    if (diff.inDays < 7) return '${diff.inDays} d ago';
    return '${t.day}/${t.month}/${t.year}';
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return const Center(child: CircularProgressIndicator());
    }
    if (_saved.isEmpty) {
      return const Center(
        child: Padding(
          padding: EdgeInsets.all(32),
          child: Text(
            'No save states yet.\n\n'
            'Play a game and press Pause, or just leave a session — '
            'the last one is kept automatically.',
            textAlign: TextAlign.center,
            style: TextStyle(color: Colors.white54, height: 1.5),
          ),
        ),
      );
    }

    return RefreshIndicator(
      onRefresh: _refresh,
      child: GridView.builder(
        padding: const EdgeInsets.all(16),
        gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
          maxCrossAxisExtent: 320,
          childAspectRatio: 4 / 3.4,
          crossAxisSpacing: 16,
          mainAxisSpacing: 16,
        ),
        itemCount: _saved.length,
        itemBuilder: (context, i) {
          final row = _saved[i];
          return _SlotCard(
            entry: row.entry,
            slot: row.slot,
            subtitle: _when(row.slot.savedAt!),
            onResume: () => widget.onResume(row.entry, row.slot.index),
            onDelete: () => _delete(row.entry, row.slot),
          );
        },
      ),
    );
  }
}

class _SlotCard extends StatelessWidget {
  final MediaEntry entry;
  final SaveSlot slot;
  final String subtitle;
  final VoidCallback onResume;
  final VoidCallback onDelete;

  const _SlotCard({
    required this.entry,
    required this.slot,
    required this.subtitle,
    required this.onResume,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final thumb = File(slot.thumbPath);
    return InkWell(
      onTap: onResume,
      onLongPress: onDelete,
      child: Container(
        decoration: BoxDecoration(
          color: const Color(0xFF1E2530),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: slot.isAuto ? Colors.blueAccent.withValues(alpha: 0.6) : Colors.white12,
          ),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
              child: ClipRRect(
                borderRadius:
                    const BorderRadius.vertical(top: Radius.circular(7)),
                child: thumb.existsSync()
                    ? Image.file(thumb, fit: BoxFit.cover,
                        errorBuilder: (_, _, _) => const _NoShot())
                    : const _NoShot(),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(10, 8, 10, 10),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    entry.displayName,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        color: Colors.white, fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Row(children: [
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 6, vertical: 2),
                      decoration: BoxDecoration(
                        color: slot.isAuto
                            ? Colors.blueAccent.withValues(alpha: 0.25)
                            : Colors.white10,
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Text(slot.label,
                          style: const TextStyle(
                              color: Colors.white70, fontSize: 11)),
                    ),
                    const SizedBox(width: 8),
                    Text(subtitle,
                        style: const TextStyle(
                            color: Colors.white54, fontSize: 12)),
                  ]),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _NoShot extends StatelessWidget {
  const _NoShot();

  @override
  Widget build(BuildContext context) => Container(
        color: const Color(0xFF11151C),
        alignment: Alignment.center,
        child: const Text('NO IMAGE',
            style: TextStyle(color: Colors.white24, fontSize: 12)),
      );
}
