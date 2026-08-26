// library_grid.dart — The library screen.
//
// Pulls MediaEntry scans via [LibraryScanner], exposes a search box
// + A-Z + 0-9 sort tabs. Selecting a tile hands the [MediaEntry]
// back to the parent (so the parent can route into the game launcher).
//
// Sort tabs (All / 0-9 / A / B / ... / Z) partition games by their
// baseName's first character. Case-insensitive. The format filter that
// used to live here was dropped — the bezel index + cover art are what
// matter, and the bezel A-Z folder layout already gives letter-based
// organisation.

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;

import '../data/media_entry.dart';
import '../services/app_prefs.dart';
import '../services/library_scanner.dart';
import '../widgets/media_card.dart';

const _digits = '0-9';

/// Letter tab labels. 0-9 first (file names starting with a digit are
/// common — 1943.chd, etc.), then A-Z. 'All' shows everything.
List<String> get _sortTabs {
  final tabs = <String>[_digits];
  for (var c = 0; c < 26; c++) {
    tabs.add(String.fromCharCode(0x41 + c));
  }
  return tabs;
}

String _firstCharLower(MediaEntry e) {
  final s = e.baseName.trim();
  if (s.isEmpty) return '#';
  final c = s[0].toLowerCase();
  if (RegExp(r'[a-z]').hasMatch(c)) return c;
  if (RegExp(r'[0-9]').hasMatch(c)) return _digits;
  return '#';
}

/// Scanned games laid out in a responsive grid + search + letter sort.
class LibraryGrid extends StatefulWidget {
  /// Directory to scan. Pass null to skip the scan (e.g. when the user
  /// hasn't picked a folder yet). Recomputed whenever [folderPath] changes.
  final String? folderPath;

  /// Called when the user wants to launch / inspect [entry]. The library
  /// grid itself never loads a disc -- that's the parent's job.
  final void Function(MediaEntry entry) onLaunch;

  /// Optional widget shown when [folderPath] is null. Lets the parent
  /// show a "Pick a folder" CTA in place of an empty grid.
  final Widget? emptyState;

  const LibraryGrid({
    super.key,
    required this.folderPath,
    required this.onLaunch,
    this.emptyState,
  });

  @override
  State<LibraryGrid> createState() => _LibraryGridState();
}

class _LibraryGridState extends State<LibraryGrid> {
  String _search = '';
  String _tab = 'All';
  String get _filter => _tab;

  /// The latest scan result. Recomputed by [_rescan] whenever the source
  /// folder changes.
  LibraryScanResult _scan = LibraryScanResult.empty;
  bool _scanning = false;

  /// What went wrong on the last scan, if anything -- surfaced rather than
  /// swallowed, because "no games found" and "the scan failed" need
  /// different fixes.
  String? _scanError;

  /// Search waits for the typing to pause. Filtering per keystroke walks
  /// the whole library once per character, which reads as a stiff keyboard
  /// on a large collection -- the lesson Retro-Amiga's library learned.
  Timer? _searchDebounce;

  /// Cached derivations, recomputed only when their inputs change --
  /// walking the entry list inside getters called from build() was the
  /// other half of the stiff keyboard.
  List<MediaEntry> _filteredCache = const [];
  Map<String, int> _countsCache = const {'All': 0};

  void _recompute() {
    final q = _search.trim().toLowerCase();
    _filteredCache = _scan.entries.where((e) {
      final letterOk =
          _filter == 'All' || _firstCharLower(e) == _filter.toLowerCase();
      final searchOk = q.isEmpty ||
          e.displayName.toLowerCase().contains(q) ||
          e.baseName.toLowerCase().contains(q);
      return letterOk && searchOk;
    }).toList();
    final counts = <String, int>{'All': _scan.entries.length};
    for (final e in _scan.entries) {
      final c = _firstCharLower(e);
      counts[c] = (counts[c] ?? 0) + 1;
    }
    _countsCache = counts;
  }

  @override
  void dispose() {
    _searchDebounce?.cancel();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _rescan());
  }

  @override
  void didUpdateWidget(LibraryGrid old) {
    super.didUpdateWidget(old);
    if (old.folderPath != widget.folderPath) _rescan();
  }

  Future<void> _rescan() async {
    final path = widget.folderPath;
    if (path == null) {
      setState(() => _scan = LibraryScanResult.empty);
      return;
    }
    setState(() {
      _scanning = true;
      _scanError = null;
    });
    try {
      final raw = await LibraryScanner.scan(path);
      final result = LibraryScanResult.dedup(raw);
      if (!mounted) return;
      setState(() {
        _scan = result;
        _scanning = false;
        _recompute();
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _scanning = false;
        _scanError = 'The scan failed: $e';
      });
    }
  }

  void _setSearch(String v) {
    _search = v;
    _searchDebounce?.cancel();
    _searchDebounce = Timer(const Duration(milliseconds: 150), () {
      if (mounted) setState(_recompute);
    });
  }

  void _setTab(String tab) {
    setState(() {
      _tab = tab;
      _recompute();
    });
  }

  /// Long-press on a card: the per-title actions. Play is the same as a
  /// tap; Rename and Delete act on the file itself and rescan.
  Future<void> _showEntryActions(MediaEntry entry) async {
    final action = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: const Color(0xFF13161F),
      builder: (BuildContext context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              title: Text(entry.displayName,
                  maxLines: 1, overflow: TextOverflow.ellipsis),
              subtitle: Text(entry.path,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style:
                      const TextStyle(fontSize: 11, color: Colors.white38)),
            ),
            const Divider(height: 1),
            ListTile(
              leading: const Icon(Icons.play_arrow),
              title: const Text('Play'),
              onTap: () => Navigator.pop(context, 'play'),
            ),
            ListTile(
              leading: const Icon(Icons.drive_file_rename_outline),
              title: const Text('Rename file'),
              onTap: () => Navigator.pop(context, 'rename'),
            ),
            ListTile(
              leading: const Icon(Icons.delete_outline),
              title: const Text('Delete file'),
              onTap: () => Navigator.pop(context, 'delete'),
            ),
          ],
        ),
      ),
    );
    if (!mounted || action == null) return;
    switch (action) {
      case 'play':
        widget.onLaunch(entry);
      case 'rename':
        await _renameEntry(entry);
      case 'delete':
        await _deleteEntry(entry);
    }
  }

  Future<void> _renameEntry(MediaEntry entry) async {
    final controller = TextEditingController(text: p.basename(entry.path));
    final newName = await showDialog<String>(
      context: context,
      builder: (BuildContext context) => AlertDialog(
        title: const Text('Rename file'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(labelText: 'File name'),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cancel')),
          FilledButton(
              onPressed: () => Navigator.pop(context, controller.text.trim()),
              child: const Text('Rename')),
        ],
      ),
    );
    if (newName == null || newName.isEmpty || !mounted) return;
    try {
      final target = p.join(p.dirname(entry.path), newName);
      if (File(target).existsSync()) {
        throw FileSystemException('a file with that name already exists');
      }
      await File(entry.path).rename(target);
      await _rescan();
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('Could not rename: $e')));
    }
  }

  Future<void> _deleteEntry(MediaEntry entry) async {
    if (await AppPrefs.getConfirmDelete()) {
      if (!mounted) return;
      final sure = await showDialog<bool>(
        context: context,
        builder: (BuildContext context) => AlertDialog(
          title: const Text('Delete this file?'),
          content: Text('${p.basename(entry.path)} will be deleted from '
              'disk. This cannot be undone.'),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(context, false),
                child: const Text('Cancel')),
            FilledButton(
                onPressed: () => Navigator.pop(context, true),
                child: const Text('Delete')),
          ],
        ),
      );
      if (sure != true) return;
    }
    if (!mounted) return;
    try {
      await File(entry.path).delete();
      await _rescan();
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('Could not delete: $e')));
    }
  }


  Widget _buildTabs() {
    final counts = _countsCache;
    final tabs = ['All', ..._sortTabs]
        .where((t) => counts[t] != null && counts[t]! > 0)
        .toList();
    return Container(
      height: 36,
      decoration: BoxDecoration(
        color: const Color(0xFF0A0C12),
        border: Border(
          bottom: BorderSide(color: Colors.white.withValues(alpha: 0.06)),
        ),
      ),
      child: ListView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        children: [
          for (final tab in tabs) ...[
            _TabButton(
              label: tab,
              count: counts[tab] ?? 0,
              selected: tab == _filter,
              onTap: () => _setTab(tab),
            ),
            const SizedBox(width: 4),
          ],
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (widget.folderPath == null) {
      return widget.emptyState ??
          const Center(
            child: Text(
              'Pick a games folder.',
              style: TextStyle(color: Color(0xFFB9C2CE)),
            ),
          );
    }
    if (_scanning && _scan.entries.isEmpty) {
      return const Center(
        child: CircularProgressIndicator(),
      );
    }
    final entries = _filteredCache;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (_scanError != null)
          Container(
            margin: const EdgeInsets.fromLTRB(8, 4, 8, 0),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            decoration: BoxDecoration(
              color: Colors.orange.withValues(alpha: 0.12),
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: Colors.orangeAccent),
            ),
            child: Text(_scanError!,
                style: const TextStyle(
                    color: Colors.orangeAccent, fontSize: 11)),
          ),
        _buildTabs(),
        Padding(
          padding: const EdgeInsets.fromLTRB(8, 4, 8, 4),
          child: SizedBox(
            height: 32,
            child: TextField(
              onChanged: _setSearch,
              style: const TextStyle(color: Colors.white, fontSize: 12),
              decoration: const InputDecoration(
                hintText: 'Search games...',
                hintStyle: TextStyle(color: Color(0xFF6D7689)),
                isDense: true,
                contentPadding:
                    EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                border: OutlineInputBorder(),
                isCollapsed: true,
              ),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(8, 0, 8, 2),
          child: Row(children: [
            Expanded(
              child: Text(
                _scan.entries.isEmpty
                    ? 'No games found. Supported: CHD, CUE, MDS, CCD, ISO.'
                    : '${entries.length} of ${_scan.entries.length} | ${_scan.unreadableCount} unreadable',
                style:
                    const TextStyle(color: Color(0xFF6D7689), fontSize: 10),
              ),
            ),
            // Rescan lives IN the library: files change under the app
            // (a copy finishes, a card is re-inserted), and the trip to
            // Paths was the long way round.
            InkWell(
              onTap: _scanning ? null : _rescan,
              child: const Padding(
                padding: EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                child: Row(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.refresh, size: 12, color: Color(0xFF6D7689)),
                  SizedBox(width: 3),
                  Text('Rescan',
                      style: TextStyle(
                          color: Color(0xFF6D7689), fontSize: 10)),
                ]),
              ),
            ),
          ]),
        ),
        Expanded(
          child: entries.isEmpty
              ? const Center(
                  child: Text(
                    'No games in this letter.',
                    style: TextStyle(color: Color(0xFF6D7689)),
                  ),
                )
              : LayoutBuilder(builder: (context, constraints) {
                  final columns =
                      (constraints.maxWidth / (kMediaCardWidth + 8))
                          .floor()
                          .clamp(2, 16);
                  return GridView.builder(
                    padding: const EdgeInsets.all(8),
                    gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                      crossAxisCount: columns,
                      mainAxisExtent: kMediaCardHeight + 6,
                      crossAxisSpacing: 4,
                      mainAxisSpacing: 4,
                    ),
                    itemCount: entries.length,
                    itemBuilder: (context, i) {
                      final entry = entries[i];
                      return MediaCard(
                        entry: entry,
                        onTap: () => widget.onLaunch(entry),
                        onLongPress: () => _showEntryActions(entry),
                      );
                    },
                  );
                }),
        ),
      ],
    );
  }
}

class _TabButton extends StatelessWidget {
  final String label;
  final int count;
  final bool selected;
  final VoidCallback onTap;
  const _TabButton({
    required this.label,
    required this.count,
    required this.selected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final fg = selected ? Colors.white : const Color(0xFFB9C2CE);
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(4),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: selected
                ? const Color(0xFF3D8BFF)
                : const Color(0xFF1A1F2C),
            borderRadius: BorderRadius.circular(4),
            border: Border.all(
              color: selected
                  ? const Color(0xFF3D8BFF)
                  : const Color(0xFF2B3340),
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(label,
                  style: TextStyle(
                      color: fg, fontSize: 11, fontWeight: FontWeight.w500)),
              const SizedBox(width: 3),
              Text('$count',
                  style: const TextStyle(
                      color: Color(0xFF6D7689), fontSize: 10)),
            ],
          ),
        ),
      ),
    );
  }
}