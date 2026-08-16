// BezelIndex host tests against a fake A-Z bezel layout on disk.
//
// Builds a temp folder shaped like the one [BezelDownloader] produces
// (subdirectories A, B, C, ... under a root, plus a couple of PNGs at
// the root for any non-letter entries), indexes it via [BezelIndex],
// and asserts the exact + loose lookups behave the way the library
// grid expects.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:path/path.dart' as p;
import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/services/bezel_index.dart';
import 'package:ymir_multiplatform/services/library_scanner.dart';

void main() {
  late Directory root;

  setUp(() {
    root = Directory.systemTemp.createTempSync('ymir_bezel_test');
    BezelIndex.setRoot(root);
    BezelIndex.clearCache();
  });

  tearDown(() {
    BezelIndex.setRoot(null);
    BezelIndex.clearCache();
    if (root.existsSync()) root.deleteSync(recursive: true);
  });

  MediaEntry makeEntry(String filename) => MediaEntry(
        displayName: filename,
        path: p.join('/tmp', filename),
        format: MediaFormat.fromExtension(
            p.extension(filename).replaceFirst('.', '')),
        baseName: filename.substring(0, filename.lastIndexOf('.')),
        bezelKey: LibraryScanner.normalizeBezelKey(filename),
      );

  /// Place a fake bezel file at the canonical A-Z layout path the
  /// downloader produces, by building the bezel key explicitly.
  File placeBezel(String normalizedKey) {
    final path =
        p.join(root.path, normalizedKey[0].toLowerCase(), '$normalizedKey.png');
    final file = File(path);
    file.parent.createSync(recursive: true);
    file.writeAsBytesSync([1, 2, 3]);
    return file;
  }

  group('exact lookup', () {
    test('finds a bezel that matches the entry key exactly', () async {
      placeBezel('panzer-dragoon');
      await BezelIndex.refresh();

      final match =
          await BezelIndex.findBezelForGame(makeEntry('Panzer Dragoon.cue'));
      expect(match, isNotNull);
      expect(match!.kind, BezelMatchKind.exact);
      expect(p.basenameWithoutExtension(match.file.path), 'panzer-dragoon');
    });

    test('returns null when the entry key has no bezel on disk', () async {
      placeBezel('panzer-dragoon');
      await BezelIndex.refresh();

      final match =
          await BezelIndex.findBezelForGame(makeEntry('Nights into Dreams.chd'));
      expect(match, isNull);
    });

    test('handles a flat (non-A-Z) bezel for names starting with digits', () async {
      // The downloader falls back to flat-layout for non-letter keys.
      final flat = File(p.join(root.path, '1943.png'))..writeAsBytesSync([9]);
      await BezelIndex.refresh();

      final match = await BezelIndex.findBezelForGame(makeEntry('1943.chd'));
      expect(match, isNotNull);
      expect(match!.file.path, flat.path);
    });
  });

  group('loose lookup', () {
    test('matches when the bezel key contains the entry key', () async {
      // The bezel file is "panzer-dragoon-zone-of-remake" but the game
      // is the simpler "panzer-dragoon". Lookup must succeed and be
      // marked loose.
      placeBezel('panzer-dragoon-zone-of-remake');
      await BezelIndex.refresh();

      final match =
          await BezelIndex.findBezelForGame(makeEntry('Panzer Dragoon.cue'));
      expect(match, isNotNull);
      expect(match!.kind, BezelMatchKind.loose);
      expect(match.file.path,
          endsWith(p.join('p', 'panzer-dragoon-zone-of-remake.png')));
    });

    test('matches when the entry key contains the bezel key', () async {
      // Inverse of the above: bezel is short, entry is a longer region
      // build whose key contains the bezel key but is not identical.
      placeBezel('panzer-dragoon');
      await BezelIndex.refresh();

      // "Panzer Dragoon Saga" → "panzer-dragoon-saga" — qualifier
      // blocks in the title (which the spec strips) get lost here on
      // purpose: we want the entry key to genuinely contain the bezel
      // key without being equal to it.
      final match = await BezelIndex.findBezelForGame(
          makeEntry('Panzer Dragoon Saga.cue'));
      expect(match, isNotNull);
      expect(match!.kind, BezelMatchKind.loose);
      expect(match.file.path, endsWith(p.join('p', 'panzer-dragoon.png')));
    });

    test('prefers exact match over loose when both apply', () async {
      // Two bezels exist; the exact-match should win.
      placeBezel('panzer-dragoon');
      placeBezel('panzer-dragoon-zone-of-remake');
      await BezelIndex.refresh();

      final match = await BezelIndex.findBezelForGame(
          makeEntry('Panzer Dragoon (USA).cue'));
      expect(match, isNotNull);
      expect(match!.kind, BezelMatchKind.exact);
      expect(match.file.path, endsWith(p.join('p', 'panzer-dragoon.png')));
    });
  });

  group('A-Z split', () {
    test('indexes bezels split across letter folders', () async {
      final a = placeBezel('a-vampire-hunter');
      final b = placeBezel('battle-athletessis');
      final c = placeBezel('chrono-trigger');
      await BezelIndex.refresh();

      final ma =
          await BezelIndex.findBezelForGame(makeEntry('A Vampire Hunter.cue'));
      final mb = await BezelIndex.findBezelForGame(
          makeEntry('Battle Athletessis.chd'));
      final mc = await BezelIndex.findBezelForGame(makeEntry('Chrono Trigger.iso'));
      expect(ma?.file.path, a.path);
      expect(mb?.file.path, b.path);
      expect(mc?.file.path, c.path);
    });

    test('refresh rebuilds the index against a new root', () async {
      // First root has one bezel.
      placeBezel('alpha');
      await BezelIndex.refresh();
      expect(await BezelIndex.count(), 1);

      // Swap to a fresh root with two bezels.
      final root2 = Directory.systemTemp.createTempSync('ymir_bezel_test2');
      addTearDown(() => root2.deleteSync(recursive: true));
      BezelIndex.setRoot(root2);
      File(p.join(root2.path, 'b', 'beta.png')).createSync(recursive: true);
      File(p.join(root2.path, 'b', 'bravo.png')).createSync(recursive: true);

      await BezelIndex.refresh();
      expect(await BezelIndex.count(), 2);
    });
  });

  group('fallbacks', () {
    test('an empty root reports zero indexed', () async {
      final count = await BezelIndex.refresh();
      expect(count, 0);
    });

    test('a non-PNG sibling file is not indexed as a bezel', () async {
      // A bogus file alongside a real bezel; only the PNG should show
      // up in the index.
      final b = placeBezel('panzer-dragoon');
      File(p.join(root.path, 'p', 'README.txt')).writeAsStringSync('hi');
      File(p.join(root.path, 'p', 'preview.jpg')).writeAsBytesSync([0]);

      final count = await BezelIndex.refresh();
      expect(count, 1);

      final match =
          await BezelIndex.findBezelForGame(makeEntry('Panzer Dragoon.chd'));
      expect(match?.file.path, b.path);
    });

    test('zero entries returns null gracefully', () async {
      await BezelIndex.refresh();
      final match =
          await BezelIndex.findBezelForGame(makeEntry('Any Game.chd'));
      expect(match, isNull);
    });
  });
}
