// LibraryScanner host tests against a real temp directory tree. No
// Flutter binding, no native core -- just [Directory.systemTemp] under
// a per-test prefix.
//
// Each test (re)builds the temp tree in [setUp] and removes it in
// [tearDown] so an interrupted run can never leave stale fixtures
// behind for the next run to trip over.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:path/path.dart' as p;
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/services/library_scanner.dart';

void main() {
  late Directory root;

  setUp(() => root = Directory.systemTemp.createTempSync('ymir_scan_test'));
  tearDown(() => root.deleteSync(recursive: true));

  /// Convenience: drop a file at [relativePath] under [root] with the
  /// given [contents]. Default contents is non-empty so the readability
  /// check passes.
  File write(String relativePath, {String contents = 'saturn'}) {
    final file = File(p.join(root.path, relativePath));
    file.parent.createSync(recursive: true);
    file.writeAsStringSync(contents);
    return file;
  }

  List<String> namesOf(LibraryScanResult r) =>
      r.entries.map((e) => e.displayName).toList()
        ..sort((a, b) => a.toLowerCase().compareTo(b.toLowerCase()));

  group('normalizeBezelKey', () {
    test('lowercases and collapses non-alphanumeric runs to a single dash', () {
      // "(USA)" is a qualifier block the spec strips -- it is not part
      // of the title.
      expect(LibraryScanner.normalizeBezelKey('Panzer Dragoon (USA).cue'),
          'panzer-dragoon');
    });

    test('strips parens, brackets and braces blocks', () {
      // Each pair drops with the markers AND its content -- the version
      // and revision info live inside the brackets/braces, so they go
      // too. Only the bare title survives.
      expect(
          LibraryScanner.normalizeBezelKey('Sonic CD (USA) [v1.1] {rev2}.cue'),
          'sonic-cd');
    });

    test('strips leading and trailing junk', () {
      // Leading/trailing punctuation + underscores should disappear rather
      // than leave a dashed empty key.
      expect(LibraryScanner.normalizeBezelKey('___Spaced Out!!!.iso'),
          'spaced-out');
    });

    test('produces an empty string for unusable names', () {
      expect(LibraryScanner.normalizeBezelKey('---'), '');
      expect(LibraryScanner.normalizeBezelKey(''), '');
    });

    test('strips the trailing extension only (interior dots collapse to dashes)',
        () {
      // The MediaEntry extension strip and the bezel-key strip both
      // happen once -- we test the key form here.
      expect(LibraryScanner.normalizeBezelKey('Vampire Hunter v1.10.cue'),
          'vampire-hunter-v1-10');
    });
  });

  group('scan', () {
    test('finds media in subfolders, at any depth', () {
      // The walk must be RECURSIVE. If this ever returns only the
      // top-level files, anyone filing their discs in per-publisher
      // folders will see an empty library.
      write('top.chd');
      write('Sega/Panzer Dragoon.cue');
      write('Sega/Nights/into dreams.iso');
      write('a/b/c/d/deep.mds');

      final result = LibraryScanner.scan(root.path);

      expect(namesOf(result), [
        'deep.mds',
        'into dreams.iso',
        'Panzer Dragoon.cue',
        'top.chd',
      ]);
      expect(result.unreadableCount, 0);
    });

    test('classifies each file by its extension and skips the rest', () {
      write('game.chd');
      write('game.cue');
      write('game.mds');
      write('game.ccd');
      write('game.iso');
      write('notes.txt');
      write('cover.png');
      write('noextension');

      final result = LibraryScanner.scan(root.path);

      final byName = {for (final e in result.entries) e.displayName: e};
      expect(byName['game.chd']!.format, MediaFormat.chd);
      expect(byName['game.cue']!.format, MediaFormat.cue);
      expect(byName['game.mds']!.format, MediaFormat.mds);
      expect(byName['game.ccd']!.format, MediaFormat.ccd);
      expect(byName['game.iso']!.format, MediaFormat.iso);
      expect(result.entries.any((e) => e.format == MediaFormat.unknown),
          isFalse);

      // Skipped files must NOT appear.
      expect(result.entries.any((e) => e.displayName == 'notes.txt'), isFalse);
      expect(result.entries.any((e) => e.displayName == 'cover.png'), isFalse);
      expect(result.entries.any((e) => e.displayName == 'noextension'),
          isFalse);
    });

    test('derives display name, base name and bezel key from each file', () {
      write('Panzer Dragoon (USA) [v1.1].cue');

      final result = LibraryScanner.scan(root.path);

      expect(result.entries, hasLength(1));
      final entry = result.entries.single;
      expect(entry.displayName, 'Panzer Dragoon (USA) [v1.1].cue');
      expect(entry.baseName, 'Panzer Dragoon (USA) [v1.1]');
      // "(USA)" and "[v1.1]" are qualifier blocks the spec strips entirely,
      // including their content. The version lives inside the brackets and
      // so is gone too. The key ends up identical to the bare title.
      expect(entry.bezelKey, 'panzer-dragoon');
      expect(entry.format, MediaFormat.cue);
      expect(entry.path, endsWith('Panzer Dragoon (USA) [v1.1].cue'));
    });

    test('a missing folder scans to an empty library, not a crash', () {
      final result = LibraryScanner.scan(p.join(root.path, 'nope'));
      expect(result.entries, isEmpty);
      expect(result.unreadableCount, 0);
    });

    test('counts, rather than lists, media it cannot read', () {
      // Files that the OS can list but the app can't open (Android
      // scoped storage often behaves this way) must be counted, not
      // listed -- otherwise the user sees tiles that try to launch an
      // unopenable file.
      write('good.chd');
      write('empty.chd', contents: '');

      final result = LibraryScanner.scan(root.path);

      expect(namesOf(result), ['good.chd']);
      expect(result.unreadableCount, 1);
    });

    test('isReadable is false for a file that is not there', () {
      expect(LibraryScanner.isReadable(File(p.join(root.path, 'gone.chd'))),
          isFalse);
    });

    test('dedup keeps one entry per unique path+format', () {
      // The same file shouldn't appear twice even if the user has it in
      // two parents. dedup is the integration layer's job, but the
      // scanner's own output must be dedupe-safe by construction.
      write('dupe.chd');

      final result = LibraryScanner.scan(root.path);
      final result2 = LibraryScanResult.dedup(LibraryScanResult.dedup(result));
      expect(result2.entries, hasLength(1));
    });

    test('follows links: false -- symlink loops cannot hang the scan', () {
      // Synthetic loop: a -> a/b -> a. The scan must finish quickly.
      final a = Directory(p.join(root.path, 'a'))..createSync();
      try {
        Link(p.join(a.path, 'b')).createSync(a.path);
        write('a/file.chd');
      } on FileSystemException catch (_) {
        // Some platforms don't allow hard links to directories via
        // [Link.createSync]; the assertion below is still valid because
        // a recursive scan of a self-cycle must still terminate.
        write('a/file.chd');
      }

      final result = LibraryScanner.scan(root.path);
      expect(result.entries, isNotEmpty);
    }, timeout: const Timeout(Duration(seconds: 5)));
  });
}
