// setup_scan_service.dart — Auto-scan a folder for Saturn BIOS + game
// files. Like ViceMultiplatform's auto-import flow: walk the folder,
// classify each file (BIOS: saturn*.bin / *.bin 512 KiB; game:
// .chd / .cue / .iso / .mds / .ccd / .img), report what was found.
//
// On Android the most common layout is /storage/FEDD-B1FF/Ymir/{BIOS,
// Games} (the previous ymir-android Java app's convention). On Linux
// it's ~/Ymir/{BIOS,Games}. iOS is file-import only.

import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/services/library_scanner.dart';

class ScanResult {
  final String folderPath;
  final List<String> biosCandidates;
  final List<MediaEntry> games;

  const ScanResult({
    required this.folderPath,
    required this.biosCandidates,
    required this.games,
  });

  bool get hasBios => biosCandidates.isNotEmpty;
  bool get hasGames => games.isNotEmpty;
  bool get isEmpty => !hasBios && !hasGames;
}

class SetupScanService {
  /// Default folders to probe in priority order. First hit wins.
  ///
  /// This list used to open with a HARDCODED volume UUID -- the developer's
  /// own SD card, '/storage/FEDD-B1FF/Ymir' -- which on every other device on
  /// earth is a folder that cannot exist. Removable cards are found by
  /// listing /storage instead: every mounted volume shows up there under its
  /// UUID, whoever's card it is.
  static const _defaultFolders = <String>[
    '/storage/emulated/0/Ymir',
    '/sdcard/Ymir',
  ];

  /// Ymir folders on removable volumes, found rather than remembered.
  static List<String> _removableFolders() {
    final List<String> found = <String>[];
    try {
      for (final FileSystemEntity entry
          in Directory('/storage').listSync(followLinks: false)) {
        final String name = entry.path.split('/').last;
        // Volume UUIDs look like FEDD-B1FF; skip the internal aliases.
        if (name == 'emulated' || name == 'self') continue;
        found.add('${entry.path}/Ymir');
      }
    } on FileSystemException {
      // /storage unreadable: internal-only device, or no permission yet.
    }
    return found;
  }

  /// Get the default scan folder for the current platform.
  static String? defaultFolder() {
    if (Platform.isAndroid) {
      for (final f in <String>[..._removableFolders(), ..._defaultFolders]) {
        if (Directory(f).existsSync()) return f;
      }
      return _defaultFolders.first; // fall back even if missing
    }
    if (Platform.isLinux || Platform.isMacOS) {
      final home = Platform.environment['HOME'] ?? '/root';
      return '$home/Ymir';
    }
    // iOS is deliberately absent here and handled by defaultFolderAsync():
    // its documents directory can only be resolved through path_provider,
    // which is async, and this method cannot be.
    return null;
  }

  /// The default scan folder, including iOS.
  ///
  /// On iOS there is nothing for the user to choose: the app can only read its
  /// own container, and the only way anything gets in is the Files app writing
  /// into the directory published by UIFileSharingEnabled -- which is
  /// Documents. So the default is not a convenience there, it is the entire
  /// mechanism, and the folder has to EXIST before the user opens Files or
  /// there is nowhere for them to drop a BIOS.
  ///
  /// The name matches what the setup wizard tells them to look for.
  static Future<String?> defaultFolderAsync() async {
    if (!Platform.isIOS) return defaultFolder();
    final docs = await getApplicationDocumentsDirectory();
    final folder = Directory(p.join(docs.path, 'Retro-Saturn'));
    // Created, not just named: an absent folder is invisible in Files, so the
    // instructions would point at something the user cannot find.
    await folder.create(recursive: true);
    await Directory(p.join(folder.path, 'BIOS')).create(recursive: true);
    await Directory(p.join(folder.path, 'Games')).create(recursive: true);
    return folder.path;
  }

  /// Async counterpart of [autoDetectFolder], so iOS is included.
  static Future<String?> autoDetectFolderAsync() async {
    final d = await defaultFolderAsync();
    if (d == null) return null;
    return Directory(d).existsSync() ? d : null;
  }

  /// Probe a folder for BIOS + game files. BIOS candidates = saturn*.bin
  /// or *.bin files with size exactly 524288 bytes (the IPL size).
  static Future<ScanResult> scan(String folderPath) =>
      // A background isolate: this recursive walk crosses the whole games
      // folder (often an SD card) during the wizard, and async dir.list on
      // the UI isolate still does its stat calls there.
      Isolate.run(() => _scanOnIsolate(folderPath));

  static Future<ScanResult> _scanOnIsolate(String folderPath) async {
    final bios = <String>[];
    final games = <MediaEntry>[];

    final dir = Directory(folderPath);
    if (!dir.existsSync()) {
      return ScanResult(folderPath: folderPath,
          biosCandidates: const [], games: const []);
    }

    // Scan recursively for BIOS + game files
    await for (final entity in dir.list(recursive: true, followLinks: false)) {
      if (entity is! File) continue;
      final name = entity.path.toLowerCase();
      final size = await entity.length();

      // BIOS detection: saturn*.bin OR any .bin that's exactly 512 KiB
      if (name.endsWith('.bin')) {
        if (size == 524288) {
          bios.add(entity.path);
        }
      }

      // Game detection
      final ext = name.split('.').last;
      if (['chd', 'cue', 'iso', 'mds', 'ccd', 'img'].contains(ext)) {
        if (size > 0) {
          games.add(MediaEntry(
            displayName: _displayName(entity.path),
            path: entity.path,
            format: MediaFormat.fromExtension(ext),
            baseName: _basename(entity.path),
            bezelKey: LibraryScanner.normalizeBezelKey(entity.path),
          ));
        }
      }
    }

    // The canonical layout is one folder -- usually called Saturn -- with
    // BIOS/ and Games/ subfolders inside. The recursive walk above already
    // finds both when the user picks that folder; this probe also covers
    // picking the Games/ subfolder by accident, by looking at its sibling
    // BIOS/ before reporting "no BIOS".
    if (bios.isEmpty) {
      for (final probe in <String>[
        p.join(p.dirname(folderPath), 'BIOS'),
        p.join(folderPath, 'BIOS'),
      ]) {
        final d = Directory(probe);
        if (!d.existsSync()) continue;
        try {
          for (final f in d.listSync(followLinks: false)) {
            if (f is! File) continue;
            if (!f.path.toLowerCase().endsWith('.bin')) continue;
            if (f.lengthSync() == 524288) bios.add(f.path);
          }
        } on FileSystemException {
          continue;
        }
        if (bios.isNotEmpty) break;
      }
    }

    return ScanResult(folderPath: folderPath,
        biosCandidates: bios, games: _dedup(games));
  }

  /// Two CHD/CUE files for the same game often live side by side — e.g.
  /// `Alien Trilogy (US).chd` and `Alien_Trilogy__US_.chd`. Dedup by
  /// bezelKey (already normalized via `LibraryScanner.normalizeBezelKey`)
  /// keeping the lexicographically-first entry per key. */
  static List<MediaEntry> _dedup(List<MediaEntry> games) {
    final byKey = <String, MediaEntry>{};
    for (final g in games) {
      final key = dedupBaseName(g.baseName);
      final existing = byKey[key];
      if (existing == null) {
        byKey[key] = g;
      } else {
        try {
          final aSz = File(existing.path).lengthSync();
          final bSz = File(g.path).lengthSync();
          if (bSz > aSz) byKey[key] = g;
        } catch (_) {
          // can't stat, keep whichever we saw first
        }
      }
    }
    final out = byKey.values.toList()
      ..sort((a, b) => a.displayName.toLowerCase()
          .compareTo(b.displayName.toLowerCase()));
    return out;
  }

  /// Returns the default scan folder, falling back to none.
  static String? autoDetectFolder() {
    final d = defaultFolder();
    if (d == null) return null;
    return Directory(d).existsSync() ? d : null;
  }

  static String _basename(String p) {
    final segs = p.split('/');
    final last = segs.isEmpty ? p : segs.last;
    final dot = last.lastIndexOf('.');
    return dot > 0 ? last.substring(0, dot) : last;
  }

  static String _displayName(String p) {
    final segs = p.split('/');
    final last = segs.isEmpty ? p : segs.last;
    return last;
  }
}