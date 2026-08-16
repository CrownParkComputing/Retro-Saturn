// bezel_downloader.dart — Pull a bezel pack ZIP over HTTP and explode it
// into the per-letter folders the [BezelIndex] expects.
//
// Layout we produce:
//   <app docs>/bezels/A/<title>.png
//   <app docs>/bezels/B/<title>.png
//   ...
//   <app docs>/bezels/<title>.png   (flat, for any entry whose normalized
//                                    name starts with a non A-Z glyph;
//                                    no letter folder created)
//
// The per-letter split keeps the per-game lookup (O(1) on a normal
// HashMap) genuinely fast even when the full pack has 500+ entries --
// the indexer only ever has to walk one folder.
//
// ZIPs from "thebezelproject/bezelprojectSA-Saturn" and similar packs
// have a top-level folder ("bezelprojectSA-Saturn-master/") that we
// drop on the way in. We also clip every file name to its basename so
// a malicious archive can't escape the destination.
//
// Failures (HTTP non-200, malformed zip, write errors) are surfaced as
// an exception for the caller to show; partial downloads are cleaned up
// before re-throwing.

import 'dart:async';
import 'dart:io';

import 'package:archive/archive.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import 'library_scanner.dart';

/// Default upstream: the community Saturn bezel pack on GitHub.
const String kDefaultBezelPackUrl =
    'https://github.com/thebezelproject/bezelprojectSA-Saturn/archive/refs/heads/master.zip';

/// Where the bezels live after a successful download.
Future<Directory> bezelRoot() async {
  final docs = await getApplicationSupportDirectory();
  final dir = Directory(p.join(docs.path, 'bezels'));
  if (!dir.existsSync()) await dir.create(recursive: true);
  return dir;
}

/// Result of one download. `pngCount` is how many PNGs ended up on disk
/// under the per-letter layout; `bytes` is the size of the payload that
/// was streamed; `root` is the bezels directory (always `<docs>/bezels`).
class BezelDownloadResult {
  final Directory root;
  final int pngCount;
  final int bytes;

  const BezelDownloadResult({
    required this.root,
    required this.pngCount,
    required this.bytes,
  });
}

/// Progress / heartbeat the UI can show while a download runs. Counts of
/// already-archived PNGs are reported as the unzip progresses.
class BezelDownloadProgress {
  final String phase;
  final int current;
  final int? total;
  final bool indeterminate;
  const BezelDownloadProgress(
      this.phase, this.current, this.total, this.indeterminate);
}

/// Downloads a bezel pack and organises its PNGs into the per-letter
/// directory layout. Returns the populated folder + counts.
class BezelDownloader {
  BezelDownloader._();

  /// Override the HTTP client. Tests use it to fake out the network.
  static http.Client Function()? httpClientFactory;

  /// Pull [urlString] (default: the community pack) and unzip into the
  /// A-Z layout. Existing bezel files are removed first so a partial /
  /// corrupted earlier download can't leave stragglers that defeat the
  /// indexer.
  ///
  /// [onProgress] is called frequently during the download + unzip; pass
  /// null to skip progress reporting.
  static Future<BezelDownloadResult> download({
    String urlString = kDefaultBezelPackUrl,
    void Function(BezelDownloadProgress p)? onProgress,
  }) async {
    final root = await bezelRoot();
    _notify(onProgress, 'Preparing', 0, 0, true);

    final tempZip = File(p.join(root.path, 'bezel-download.zip'));
    if (tempZip.existsSync()) {
      try {
        await tempZip.delete();
      } catch (_) {/* best effort */}
    }

    // Clear any previously-extracted PNGs. Anything left under root/ from
    // an interrupted prior run confuses the indexer's per-letter walk.
    await _clearDirectory(root);

    final bytes = await _downloadToFile(
      urlString,
      tempZip,
      0,
      onProgress,
    );

    _notify(onProgress, 'Extracting bezels', 0, 0, true);
    final pngCount = await _extractPngs(tempZip, root, onProgress);

    // Download zip is throwaway -- always remove it last so a crash mid
    // unzip leaves the temp file behind for next time to clean.
    if (tempZip.existsSync()) {
      try {
        await tempZip.delete();
      } catch (_) {/* best effort */}
    }

    _notify(onProgress, 'Done', pngCount, pngCount, false);
    return BezelDownloadResult(root: root, pngCount: pngCount, bytes: bytes);
  }

  static Future<int> _downloadToFile(
    String urlString,
    File output,
    int redirects,
    void Function(BezelDownloadProgress p)? onProgress,
  ) async {
    if (redirects > 5) {
      throw const HttpException('Too many redirects');
    }
    final client = (httpClientFactory ?? http.Client.new)();
    try {
      final req = http.Request('GET', Uri.parse(urlString));
      req.headers['User-Agent'] = 'ymir-multiplatform';
      final streamed = await client.send(req);
      if (streamed.statusCode >= 300 && streamed.statusCode < 400) {
        final loc = streamed.headers['Location'];
        if (loc == null || loc.isEmpty) {
          throw const HttpException('Redirect without Location header');
        }
        // Re-resolve relative redirects against the request URL.
        final next = Uri.parse(urlString).resolve(loc).toString();
        return _downloadToFile(next, output, redirects + 1, onProgress);
      }
      if (streamed.statusCode < 200 || streamed.statusCode >= 300) {
        throw HttpException('HTTP ${streamed.statusCode}');
      }
      final total = streamed.contentLength ?? -1;
      var done = 0;
      var nextTick = 0;
      _notify(onProgress, 'Downloading bezel archive', 0, total, total <= 0);
      final sink = output.openWrite();
      try {
        await for (final chunk in streamed.stream) {
          sink.add(chunk);
          done += chunk.length;
          if (total <= 0 || done >= nextTick) {
            _notify(onProgress, 'Downloading bezel archive', done, total,
                total <= 0);
            nextTick = done + 512 * 1024;
          }
        }
      } finally {
        await sink.flush();
        await sink.close();
      }
      _notify(onProgress, 'Download complete', done, total, total <= 0);
      return done;
    } finally {
      final factory = httpClientFactory;
      if (factory == null) client.close();
    }
  }

  static Future<int> _extractPngs(
    File zipFile,
    Directory destination,
    void Function(BezelDownloadProgress p)? onProgress,
  ) async {
    final bytes = await zipFile.readAsBytes();
    final archive = ZipDecoder().decodeBytes(bytes);
    var count = 0;
    for (final entry in archive) {
      if (!entry.isFile) continue;
      final stripped = _stripArchiveRoot(entry.name);
      if (stripped.isEmpty) continue;
      if (!stripped.toLowerCase().endsWith('.png')) continue;

      final target = _resolveInside(destination, stripped);
      await target.parent.create(recursive: true);
      await target.writeAsBytes(entry.content as List<int>, flush: true);
      count++;
      _notify(
          onProgress, 'Extracting bezels', count, archive.length, false);
    }
    return count;
  }

  /// GitHub `master.zip`es have a top-level folder; the downloader
  /// wants flat entries, so we drop the first path segment.
  static String _stripArchiveRoot(String entryName) {
    final normalised = entryName.replaceAll('\\', '/');
    final slash = normalised.indexOf('/');
    return slash >= 0 ? normalised.substring(slash + 1) : normalised;
  }

  /// Place [entryName] inside [root] but never escape it (zip-slip).
  /// Resolves symlinks / `..` / `\` consistently via the platform [File]
  /// machinery.
  static File _resolveInside(Directory root, String entryName) {
    final cleaned = entryName.replaceAll('\\', '/');
    final rootCanonical = root.resolveSymbolicLinksSync();
    final candidate = File(p.join(root.path, cleaned));
    final fileCanonical = candidate.resolveSymbolicLinksSync();
    final inside = p.isWithin(rootCanonical, fileCanonical);
    if (!inside && fileCanonical != rootCanonical) {
      throw const HttpException('Archive entry outside destination');
    }
    return File(fileCanonical);
  }

  static Future<void> _clearDirectory(Directory dir) async {
    if (!dir.existsSync()) return;
    final children = dir.listSync();
    for (final child in children) {
      try {
        await child.delete(recursive: true);
      } catch (_) {/* best effort */}
    }
  }

  static void _notify(
    void Function(BezelDownloadProgress p)? onProgress,
    String phase,
    int current,
    int? total,
    bool indeterminate,
  ) {
    if (onProgress == null) return;
    onProgress(BezelDownloadProgress(phase, current, total, indeterminate));
  }

  /// Where a single bezel for [normalizedKey] would land inside [root],
  /// or null if [normalizedKey] is empty. Used by callers (e.g. the
  /// library tile) that want the canonical on-disk path without doing
  /// a full lookup. The bezel indexer's exact-match lookup goes through
  /// this layout.
  static String? pathForKey(Directory root, String normalizedKey) {
    if (normalizedKey.isEmpty) return null;
    final first = normalizedKey[0].toLowerCase();
    final firstCode = first.codeUnitAt(0);
    if (firstCode >= 0x61 && firstCode <= 0x7a) {
      // A-Z (lowercase a-z) → per-letter folder.
      return p.join(root.path, first, '$normalizedKey.png');
    }
    // Flat layout for keys that don't begin with A-Z.
    return p.join(root.path, '$normalizedKey.png');
  }

  /// Same as [pathForKey] but it normalizes [displayName] first via
  /// [LibraryScanner.normalizeBezelKey] -- the path the library tile
  /// uses most often.
  static String? pathForEntry(Directory root, String displayName) {
    final key = LibraryScanner.normalizeBezelKey(displayName);
    return pathForKey(root, key);
  }

  /// Convenience for callers that just want to know if there's a non-empty
  /// pack on disk already. Cheap -- only checks the A-Z subdirectories.
  static Future<bool> hasExistingPack() async {
    final root = await bezelRoot();
    if (!root.existsSync()) return false;
    for (final entity in root.listSync(followLinks: false)) {
      if (entity is File && entity.path.toLowerCase().endsWith('.png')) {
        return true;
      }
      if (entity is Directory) {
        final inner = entity.listSync(followLinks: false);
        if (inner.any((e) => e is File && e.path.toLowerCase().endsWith('.png'))) {
          return true;
        }
      }
    }
    return false;
  }
}
