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

import 'package:flutter/material.dart';

import '../data/media_entry.dart';
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
    setState(() => _scanning = true);
    // The scan is fast on a real device but uses sync IO, so we hop to a
    // microtask rather than blocking a frame.
    await Future<void>.microtask(() {});
    final raw = LibraryScanner.scan(path);
    final result = LibraryScanResult.dedup(raw);
    if (!mounted) return;
    setState(() {
      _scan = result;
      _scanning = false;
    });
  }

  /// Apply search + letter filter on top of [_scan].
  List<MediaEntry> get _filtered {
    final q = _search.trim().toLowerCase();
    return _scan.entries.where((e) {
      final letterOk = _filter == 'All' || _firstCharLower(e) == _filter.toLowerCase();
      final searchOk = q.isEmpty ||
          e.displayName.toLowerCase().contains(q) ||
          e.baseName.toLowerCase().contains(q);
      return letterOk && searchOk;
    }).toList();
  }

  /// Count entries per letter tab. Letters with zero matches are hidden
  /// from the tab row, keeping the strip short on small libraries.
  Map<String, int> get _counts {
    final result = <String, int>{'All': _scan.entries.length};
    for (final e in _scan.entries) {
      final c = _firstCharLower(e);
      result[c] = (result[c] ?? 0) + 1;
    }
    return result;
  }

  Widget _buildTabs() {
    final counts = _counts;
    final tabs = ['All', ..._sortTabs]
        .where((t) => counts[t] != null && counts[t]! > 0)
        .toList();
    return SizedBox(
      height: 28,
      child: ListView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        children: [
          for (final tab in tabs) ...[
            _TabButton(
              label: tab,
              count: counts[tab] ?? 0,
              selected: tab == _filter,
              onTap: () => setState(() => _tab = tab),
            ),
            const SizedBox(width: 3),
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
    final entries = _filtered;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _buildTabs(),
        Padding(
          padding: const EdgeInsets.fromLTRB(8, 4, 8, 4),
          child: SizedBox(
            height: 32,
            child: TextField(
              onChanged: (v) => setState(() => _search = v),
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
          child: Text(
            _scan.entries.isEmpty
                ? 'No games found. Supported: CHD, CUE, MDS, CCD, ISO.'
                : '${entries.length} of ${_scan.entries.length} | ${_scan.unreadableCount} unreadable',
            style: const TextStyle(
                color: Color(0xFF6D7689), fontSize: 10),
          ),
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