// library_grid.dart — The library screen.
//
// Pulls MediaEntry scans via [LibraryScanner], fans them out to the
// [BezelIndex] for bezel lookups (those happen inside each [MediaCard]
// so the grid stays responsive), and exposes a search box + format
// filter row. Selecting a tile hands the [MediaEntry] back to the parent
// (so the parent can route into the game launcher / details sheet).

import 'package:flutter/material.dart';

import '../data/media_entry.dart';
import '../services/library_scanner.dart';
import '../widgets/media_card.dart';

/// Possible tab filters across the top. `all` is the unfiltered view;
/// the format filters show only their respective kind.
enum LibraryFilter { all, chd, cue, mds, ccd, iso, other }

extension _FilterInfo on LibraryFilter {
  String get label {
    switch (this) {
      case LibraryFilter.all:
        return 'All';
      case LibraryFilter.chd:
        return 'CHD';
      case LibraryFilter.cue:
        return 'CUE';
      case LibraryFilter.mds:
        return 'MDS';
      case LibraryFilter.ccd:
        return 'CCD';
      case LibraryFilter.iso:
        return 'ISO';
      case LibraryFilter.other:
        return 'Other';
    }
  }

  bool matches(MediaFormat format) {
    switch (this) {
      case LibraryFilter.all:
        return true;
      case LibraryFilter.chd:
        return format == MediaFormat.chd;
      case LibraryFilter.cue:
        return format == MediaFormat.cue;
      case LibraryFilter.mds:
        return format == MediaFormat.mds;
      case LibraryFilter.ccd:
        return format == MediaFormat.ccd;
      case LibraryFilter.iso:
        return format == MediaFormat.iso;
      case LibraryFilter.other:
        return format == MediaFormat.unknown;
    }
  }
}

/// Scanned games laid out in a responsive grid + search + format filter.
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
  LibraryFilter _filter = LibraryFilter.all;

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

  /// Apply search + format filter on top of [_scan].
  List<MediaEntry> get _filtered {
    final q = _search.trim().toLowerCase();
    return _scan.entries.where((e) {
      final formatOk = _filter.matches(e.format);
      final searchOk = q.isEmpty ||
          e.displayName.toLowerCase().contains(q) ||
          e.baseName.toLowerCase().contains(q);
      return formatOk && searchOk;
    }).toList();
  }

  Map<LibraryFilter, int> get _counts {
    final result = <LibraryFilter, int>{
      for (final f in LibraryFilter.values) f: 0,
    };
    for (final e in _scan.entries) {
      // Increment any matching filter and "all".
      result[LibraryFilter.all] = (result[LibraryFilter.all] ?? 0) + 1;
      for (final f in LibraryFilter.values) {
        if (f != LibraryFilter.all && f.matches(e.format)) {
          result[f] = (result[f] ?? 0) + 1;
        }
      }
    }
    return result;
  }

  Widget _buildTabs() {
    final counts = _counts;
    return SizedBox(
      height: 36,
      child: ListView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        children: [
          for (final tab in LibraryFilter.values) ...[
            _TabButton(
              label: tab.label,
              count: counts[tab] ?? 0,
              selected: tab == _filter,
              onTap: () => setState(() => _filter = tab),
            ),
            const SizedBox(width: 6),
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
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 6),
          child: Text(
            'Game Library',
            style: const TextStyle(
                color: Colors.white,
                fontSize: 20,
                fontWeight: FontWeight.bold),
          ),
        ),
        _buildTabs(),
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 6, 12, 4),
          child: TextField(
            onChanged: (v) => setState(() => _search = v),
            style: const TextStyle(color: Colors.white, fontSize: 13),
            decoration: const InputDecoration(
              hintText: 'Search games...',
              hintStyle: TextStyle(color: Color(0xFF6D7689)),
              isDense: true,
              contentPadding:
                  EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              border: OutlineInputBorder(),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 2, 12, 4),
          child: Text(
            _scan.entries.isEmpty
                ? 'No Saturn disc images found. Supported: CHD, CUE, MDS, CCD, ISO.'
                : '${entries.length} of ${_scan.entries.length} entries | ${_scan.unreadableCount} unreadable',
            style: const TextStyle(
                color: Color(0xFF6D7689), fontSize: 12),
          ),
        ),
        Expanded(
          child: entries.isEmpty
              ? const Center(
                  child: Text(
                    'No games in this category.',
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
        borderRadius: BorderRadius.circular(8),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: selected
                ? const Color(0xFF3D8BFF)
                : const Color(0xFF1A1F2C),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(
              color: selected
                  ? const Color(0xFF3D8BFF)
                  : const Color(0xFF2B3340),
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                label,
                style: TextStyle(
                    color: fg, fontSize: 13, fontWeight: FontWeight.w500),
              ),
              const SizedBox(width: 5),
              Text(
                '$count',
                style: const TextStyle(
                    color: Color(0xFF6D7689), fontSize: 11),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
