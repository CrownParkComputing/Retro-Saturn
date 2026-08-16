// library_scanner.dart — Turning a folder on disk into the Saturn games
// library.
//
// Two things pinned here have already been bugs in earlier Saturn ports:
//  1. the walk must be RECURSIVE — discs filed under per-publisher
//     subfolders simply never appeared when it wasn't.
//  2. a file that lists but cannot be READ is counted, not listed —
//     Android 11+ scoped storage happily enumerates directories the
//     app has no permission to open, and each of those launches into
//     a blank screen.
//
// The bezel-key normaliser here is the source of truth that the bezel
// index and the library grid both consume. The form is:
//   "Sonic CD (USA) [v1.1].cue"  ->  "sonic-cd-usa-v11"
// — lowercase, strip `(...)` / `[...]` / `{...}` blocks, collapse every
// other non-alphanumeric run to a single dash, trim leading/trailing
// dashes. Mirrors the ymir-android Java helper that used to live in
// MainActivity.normalizeBezelKey (dropQualifiers=true form), with the
// small improvement of keeping dashes between words so the keys read
// better and match the A-Z folder layout the bezel indexer uses.

import 'dart:io';

import 'package:path/path.dart' as p;

import '../data/media_entry.dart';

/// The library as found on disk, plus what had to be skipped.
class LibraryScanResult {
  final List<MediaEntry> entries;

  /// Files that matched a supported extension but whose bytes could not be
  /// read. Surfaced as a banner with a way to fix it rather than hidden.
  final int unreadableCount;

  const LibraryScanResult({required this.entries, required this.unreadableCount});

  static const empty = LibraryScanResult(entries: [], unreadableCount: 0);

  /// Deduplicated by path+format: useful when the user has the same disc
  /// under multiple parent folders (e.g. an old unsorted root plus a new
  /// Per-Publisher root).
  factory LibraryScanResult.dedup(LibraryScanResult raw) {
    final seen = <MediaEntry>{};
    for (final entry in raw.entries) {
      seen.add(entry);
    }
    final list = seen.toList()
      ..sort((a, b) => a.baseName.toLowerCase().compareTo(b.baseName.toLowerCase()));
    return LibraryScanResult(entries: list, unreadableCount: raw.unreadableCount);
  }
}

/// Saturn disc formats the core can mount. The scanner returns entries
/// for these only; anything else doesn't show up.
const Set<String> kSupportedDiscExtensions = {
  'chd',
  'cue',
  'mds',
  'ccd',
  'iso',
};

class LibraryScanner {
  LibraryScanner._();

  /// Every supported disc image under [directoryPath], at any depth.
  /// A missing directory scans to nothing rather than throwing.
  static LibraryScanResult scan(String directoryPath) {
    final dir = Directory(directoryPath);
    if (!dir.existsSync()) return LibraryScanResult.empty;

    final entries = <MediaEntry>[];
    int unreadable = 0;
    // followLinks: false so a symlink loop inside the games folder cannot
    // hang the scan.
    for (final f in dir.listSync(recursive: true, followLinks: false)) {
      if (f is! File) continue;
      final ext = p.extension(f.path).replaceFirst('.', '');
      if (ext.isEmpty) continue;
      if (!kSupportedDiscExtensions.contains(ext.toLowerCase())) continue;
      if (!isReadable(f)) {
        unreadable++;
        continue;
      }
      final displayName = p.basename(f.path);
      final baseName = _stripExtension(displayName);
      entries.add(MediaEntry(
        displayName: displayName,
        path: f.path,
        format: MediaFormat.fromExtension(ext),
        baseName: baseName,
        bezelKey: normalizeBezelKey(displayName),
      ));
    }
    return LibraryScanResult(entries: entries, unreadableCount: unreadable);
  }

  /// True if the file's bytes can actually be read, not merely listed.
  /// A successful first-byte read is the cheapest honest proof.
  ///
  /// A zero-byte file counts as unusable too: there is nothing for the
  /// core to boot, so listing it would produce the same blank screen the
  /// readability check exists to prevent.
  static bool isReadable(File f) {
    try {
      final handle = f.openSync();
      try {
        return handle.readSync(1).isNotEmpty;
      } finally {
        handle.closeSync();
      }
    } on FileSystemException {
      return false;
    } catch (_) {
      return false;
    }
  }

  /// Lowercased, qualifier-stripped, dash-joined key used for bezel
  /// lookups. Always returns a string (possibly empty) -- callers must
  /// handle the empty case (the bezel indexer falls back to a hash).
  ///
  /// The extension is stripped *first*, matching the ymir-android
  /// helper's `displayBaseName(name)` step: the bezel indexer scans
  /// files whose names are extension-less PNG basenames, so a game
  /// key like "panzer-dragoon" must match a bezel key of the same
  /// form even though the scanned disc file still has ".cue" at the
  /// end.
  ///
  /// Worked example:
  ///   normalizeBezelKey("Panzer Dragoon (USA) [v1.1].cue")
  ///     -> "panzer-dragoon"
  ///   normalizeBezelKey("Sonic CD (USA) [v1.1] {rev2}.cue")
  ///     -> "sonic-cd"
  static String normalizeBezelKey(String name) {
    if (name.isEmpty) return '';

    // 1. Drop the trailing extension (".cue", ".iso") so the key we
    //    produce is independent of how the file was packaged.
    var value = _stripExtension(name);

    // 2. Strip every ( ... ) [ ... ] { ... } block in one pass, including
    //    the markers themselves. The lazy quantifier handles non-nested
    //    cases; balanced-bracket semantics aren't needed for game names.
    value = value.replaceAll(RegExp(r'\([^)]*\)'), ' ');
    value = value.replaceAll(RegExp(r'\[[^\]]*\]'), ' ');
    value = value.replaceAll(RegExp(r'\{[^}]*\}'), ' ');

    // 3. Lowercase so step 4's character class is the lower form of
    //    "letter or digit".
    value = value.toLowerCase();

    // 4. Replace any run of non-alphanumerics with a single dash, then
    //    strip leading/trailing dashes. A run of underscores or dots or
    //    other junk must not leave behind empty keys.
    value = value.replaceAll(RegExp(r'[^a-z0-9]+'), '-');
    value = value.replaceAll(RegExp(r'^-+|-+$'), '');

    return value;
  }

  /// Last extension stripped; "Panzer Dragoon (USA).cue" -> "Panzer
  /// Dragoon (USA)". The dot is the last one, since discs occasionally
  /// have additional dots in their titles ("Vampire Hunter - Requiem
  /// v1.10.cue").
  static String _stripExtension(String name) {
    final dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
  }
}
