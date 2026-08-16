// setup_scan_service.dart — Auto-scan a folder for Saturn BIOS + game
// files. Like ViceMultiplatform's auto-import flow: walk the folder,
// classify each file (BIOS: saturn*.bin / *.bin 512 KiB; game:
// .chd / .cue / .iso / .mds / .ccd / .img), report what was found.
//
// On Android the most common layout is /storage/FEDD-B1FF/Ymir/{BIOS,
// Games} (the previous ymir-android Java app's convention). On Linux
// it's ~/Ymir/{BIOS,Games}. iOS is file-import only.

import 'dart:io';

import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/services/library_scanner.dart';

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
  static const _defaultFolders = <String>[
    '/storage/FEDD-B1FF/Ymir',
    '/storage/emulated/0/Ymir',
    '/sdcard/Ymir',
  ];

  /// Get the default scan folder for the current platform.
  static String? defaultFolder() {
    if (Platform.isAndroid) {
      for (final f in _defaultFolders) {
        if (Directory(f).existsSync()) return f;
      }
      return _defaultFolders.first; // fall back even if missing
    }
    if (Platform.isLinux || Platform.isMacOS) {
      final home = Platform.environment['HOME'] ?? '/root';
      return '$home/Ymir';
    }
    return null;
  }

  /// Probe a folder for BIOS + game files. BIOS candidates = saturn*.bin
  /// or *.bin files with size exactly 524288 bytes (the IPL size).
  static Future<ScanResult> scan(String folderPath) async {
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

    return ScanResult(folderPath: folderPath,
        biosCandidates: bios, games: games);
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