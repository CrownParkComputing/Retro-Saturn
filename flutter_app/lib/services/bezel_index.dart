// bezel_index.dart — Index the bezel PNGs on disk by their normalized
// key, so the library grid can resolve a bezel for any game without ever
// having to enumerate the whole collection per tile.
//
// Two flavours of match:
//   * EXACT — the normalized file basename matches the entry's
//     normalizeBezelKey() output. This is the common case for files
//     produced by the canonical pack builder (everything lowercased,
//     dedashed).
//   * LOOSE — the file's normalized name CONTAINS the entry's normalized
//     key (or vice versa). Catches the everyday difference between
//     `panzer-dragoon` (bezel) and `panzer-dragoon-usa-1` (game) where
//     the user clearly meant one for the other.
//
// Index is built lazily on first use, then cached. Call [refresh] to
// rebuild after a new pack has been installed.

import 'dart:io';

import 'package:path/path.dart' as p;

import '../data/media_entry.dart';
import 'bezel_downloader.dart';
import 'library_scanner.dart';

/// Whether a bezel is an exact or loose match. Surfaced in the bezel
/// status string so the per-game overlay can show "loose match" when
/// the index had to fall back to a substring match.
enum BezelMatchKind { exact, loose }

/// One lookup result. [file] is the PNG file; [key] is the normalized
/// key the index found it under; [kind] distinguishes exact from loose.
class BezelMatch {
  final File file;
  final String key;
  final BezelMatchKind kind;
  final String displayName;

  const BezelMatch({
    required this.file,
    required this.key,
    required this.kind,
    required this.displayName,
  });
}

/// Looks up bezels for game entries by walking a known bezel folder.
class BezelIndex {
  BezelIndex._();

  /// The bezel root the index reads from. Tests point this at a
  /// [Directory.systemTemp] subfolder via [setRoot]; production code
  /// relies on the [bezelRoot] helper.
  static Directory? _overrideRoot;

  /// Pin the directory the index reads from. Pass null to revert to the
  /// default resolution.
  static void setRoot(Directory? dir) => _overrideRoot = dir;

  /// Cached exact-key -> PNG file map, rebuilt on [refresh].
  static Map<String, File>? _exact;

  /// Cached loose index: each bezel's normalized name -> PNG file. Keys
  /// may collide; the index keeps the first one encountered.
  static Map<String, File>? _loose;

  /// Root directory used for the last [refresh].
  static Directory? _lastRoot;

  /// The directory the index will use. Falls back to the downloader's
  /// default `<app docs>/bezels` path when nothing is pinned.
  static Future<Directory> _resolveRoot() async {
    final ovr = _overrideRoot;
    if (ovr != null) return ovr;
    return bezelRoot();
  }

  /// Force a rebuild of the in-memory index against [root] (defaults to
  /// the configured root via [_resolveRoot]).
  ///
  /// Returns how many PNGs are now indexed. A zero count means the
  /// folder is missing or has no PNGs -- callers treat that as "no
  /// bezel pack installed" and prompt for one.
  static Future<int> refresh({Directory? root}) async {
    final r = root ?? await _resolveRoot();
    _lastRoot = r;
    _exact = {};
    _loose = {};
    if (!r.existsSync()) return 0;

    // Walk every .png under the A-Z subdirectories as well as the flat
    // fallback. Recursive walk is safe because the per-letter split
    // caps the depth at 1 in practice.
    for (final f in r.listSync(recursive: true, followLinks: false)) {
      if (f is! File) continue;
      if (!f.path.toLowerCase().endsWith('.png')) continue;
      final basename = p.basenameWithoutExtension(f.path);
      final key = LibraryScanner.normalizeBezelKey(basename);
      if (key.isEmpty) continue;
      _exact!.putIfAbsent(key, () => f);
      _loose!.putIfAbsent(key, () => f);
    }
    return _exact!.length;
  }

  /// Same as [refresh] but with a specific root supplied as a one-shot
  /// (also remembers it for subsequent lookups).
  static Future<int> build(Directory root) async {
    _overrideRoot = root;
    return refresh(root: root);
  }

  /// Total bezels indexed; forces a lazy [refresh] on first call.
  static Future<int> count() async {
    if (_exact == null || _rootHasChanged) {
      await refresh();
    }
    return _exact?.length ?? 0;
  }

  /// True if the cached index was built against a different root than
  /// is now current (a new pack might have been installed).
  static bool get _rootHasChanged {
    final last = _lastRoot;
    final cur = _overrideRoot;
    if (last == null && cur == null) return false;
    if (last == null || cur == null) return true;
    return last.path != cur.path;
  }

  /// Look up a bezel for [entry]. Returns null when nothing in the index
  /// matches the entry by either exact or loose key. The returned
  /// [BezelMatch] carries the original display name + match kind so the
  /// overlay can show how it got the picture.
  static Future<BezelMatch?> findBezelForGame(MediaEntry entry) async {
    if (_exact == null || _rootHasChanged) {
      await refresh();
    }
    final exact = _exact ?? const <String, File>{};
    final loose = _loose ?? const <String, File>{};

    final key = entry.bezelKey;
    if (key.isEmpty) {
      return _fallbackLoose(loose, entry);
    }

    // 1. Exact match.
    final exactFile = exact[key];
    if (exactFile != null) {
      return BezelMatch(
        file: exactFile,
        key: key,
        kind: BezelMatchKind.exact,
        displayName: p.basenameWithoutExtension(exactFile.path),
      );
    }

    return _fallbackLoose(loose, entry);
  }

  static BezelMatch? _fallbackLoose(
    Map<String, File> loose,
    MediaEntry entry,
  ) {
    final key = entry.bezelKey;
    // 2. Loose: bezel key contains game key or vice versa.
    for (final entry2 in loose.entries) {
      final bezelKey = entry2.key;
      if (bezelKey.contains(key) || key.contains(bezelKey)) {
        return BezelMatch(
          file: entry2.value,
          key: bezelKey,
          kind: BezelMatchKind.loose,
          displayName: p.basenameWithoutExtension(entry2.value.path),
        );
      }
    }
    return null;
  }

  /// Test hook: drop the cached index so the next call rebuilds from
  /// whatever root is currently configured.
  static void clearCache() {
    _exact = null;
    _loose = null;
    _lastRoot = null;
  }
}
