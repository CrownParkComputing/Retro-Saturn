// media_card.dart — One library tile: a bezel frame around the title
// text, falling back to the cover-format label when no bezel matches.
//
// Three layers stacked from the back:
//   * the bezel PNG (found by [BezelIndex.findBezelForGame]),
//   * the disc-format badge (CHD / CUE / ...),
//   * the title (truncated).
//
// A game loads its bezel asynchronously after first build; until the
// lookup resolves the tile shows the format badge in the slot.

import 'dart:io';

import 'package:flutter/material.dart';

import '../data/media_entry.dart';
import '../services/bezel_index.dart';

/// Default size of a tile. The grid screen derives column count from
/// its actual width; this constant sets the height of one tile.
const double kMediaCardWidth = 156;
const double kMediaCardHeight = 174;
const double kMediaCardCoverHeight = 132;

/// One tile in the library grid.
class MediaCard extends StatefulWidget {
  final MediaEntry entry;
  final VoidCallback onTap;
  final VoidCallback? onLongPress;

  const MediaCard({
    super.key,
    required this.entry,
    required this.onTap,
    this.onLongPress,
  });

  @override
  State<MediaCard> createState() => _MediaCardState();
}

class _MediaCardState extends State<MediaCard> {
  BezelMatch? _bezel;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void didUpdateWidget(MediaCard old) {
    super.didUpdateWidget(old);
    if (old.entry.path != widget.entry.path) _load();
  }

  Future<void> _load() async {
    final match = await BezelIndex.findBezelForGame(widget.entry);
    if (!mounted) return;
    setState(() => _bezel = match);
  }

  /// The image a tile prefers: the matched bezel. Falls back to nothing
  /// when the index hasn't found one.
  File? get _bezelFile => _bezel?.file;

  /// Title shown under the cover slot. Long titles ellipsise on the right.
  String _titleLabel(MediaEntry e) => e.baseName;

  @override
  Widget build(BuildContext context) {
    final entry = widget.entry;
    return SizedBox(
      width: kMediaCardWidth,
      height: kMediaCardHeight,
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: widget.onTap,
          onLongPress: widget.onLongPress,
          borderRadius: BorderRadius.circular(8),
          child: Container(
            decoration: BoxDecoration(
              color: const Color(0xFF1A1F2C),
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: const Color(0xFF2B3340)),
            ),
            clipBehavior: Clip.antiAlias,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                SizedBox(
                  height: kMediaCardCoverHeight,
                  child: Stack(
                    fit: StackFit.expand,
                    children: [
                      _bezelFile != null
                          ? Image.file(
                              _bezelFile!,
                              fit: BoxFit.contain,
                              // The PNG may vanish under us between
                              // index lookup and render -- show the
                              // badge instead of taking the grid down.
                              errorBuilder: (_, _, _) =>
                                  _badgePlaceholder(entry),
                            )
                          : _badgePlaceholder(entry),
                      // Subtle bezel-name badge in the corner when we
                      // matched loosely, so the user knows the picture
                      // is approximate.
                      if (_bezel?.kind == BezelMatchKind.loose)
                        Positioned(
                          bottom: 2,
                          right: 4,
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 4, vertical: 1),
                            decoration: BoxDecoration(
                              color: Colors.black54,
                              borderRadius: BorderRadius.circular(4),
                            ),
                            child: const Text(
                              'loose',
                              style: TextStyle(
                                  color: Colors.white70, fontSize: 8),
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.fromLTRB(6, 6, 6, 2),
                  child: Text(
                    _titleLabel(entry),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        color: Colors.white,
                        fontSize: 11,
                        height: 1.2),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.fromLTRB(6, 0, 6, 6),
                  child: Text(
                    '.${entry.format.extensionLabel.toLowerCase()}',
                    style: const TextStyle(
                        color: Color(0xFF6D7689),
                        fontSize: 10),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  /// The image-less default: the format label in large dim text. Same
  /// treatment VICE's media card uses -- a clear "no picture yet" hint
  /// rather than an empty box.
  Widget _badgePlaceholder(MediaEntry entry) => Container(
        color: const Color(0xFF2B3340),
        alignment: Alignment.center,
        child: Text(
          entry.format.extensionLabel,
          style: const TextStyle(
              color: Color(0xFFB9C2CE),
              fontSize: 18,
              fontWeight: FontWeight.w600),
        ),
      );
}
