// Save state slots on disk.
//
// The core's save/load returned YMIR_ERR_GENERIC from the day the bridge was
// written, so none of this existed to be tested. What is worth pinning here is
// the part the core knows nothing about: which file a slot maps to, that two
// discs sharing a file name do not share their states, that the automatic slot
// cannot be clobbered by a manual save, and that deleting takes the thumbnail
// and metadata with it rather than leaving a card with no state behind it.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:path/path.dart' as p;
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/services/save_state_service.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';

import 'fakes/fake_ymir_core.dart';

/// A core whose saveState actually puts bytes on disk, so the service's own
/// file handling is exercised end to end.
class WritingFakeCore extends FakeYmirCore {
  @override
  int saveState(String path) {
    if (saveStateReturn != 0) return saveStateReturn;
    File(path).writeAsBytesSync(List<int>.filled(64, 7));
    return 0;
  }
}

MediaEntry entryFor(String path) => MediaEntry(
      displayName: p.basename(path),
      path: path,
      baseName: p.basenameWithoutExtension(path),
      format: MediaFormat.chd,
      bezelKey: '',
    );

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory support;

  setUp(() async {
    support = Directory.systemTemp.createTempSync('saturn_states');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      const MethodChannel('plugins.flutter.io/path_provider'),
      (call) async => support.path,
    );
    await YmirCorePaths.ensureInit();
  });

  tearDown(() {
    if (support.existsSync()) support.deleteSync(recursive: true);
  });

  test('two discs with the same file name keep separate states', () {
    final a = entryFor('/games/us/Virtua Cop (US).chd');
    final b = entryFor('/games/eu/Virtua Cop (US).chd');
    expect(SaveStateService.gameId(a), isNot(SaveStateService.gameId(b)),
        reason: 'the folder is part of what makes a disc distinct');
  });

  test('a game id is safe to use as a directory name', () {
    final id = SaveStateService.gameId(
        entryFor(r'/games/Panzer Dragoon: Zwei (JP) [!].chd'));
    expect(id, isNot(contains('/')));
    expect(id, isNot(contains(':')));
    expect(RegExp(r'^[A-Za-z0-9._-]+$').hasMatch(id), isTrue);
  });

  test('every slot is listed, empty ones included', () async {
    final slots = await SaveStateService.slotsFor(entryFor('/g/Daytona.chd'));
    expect(slots.length, kManualSlots + 1);
    expect(slots.first.isAuto, isTrue, reason: 'the auto slot leads the list');
    expect(slots.every((s) => s.isEmpty), isTrue);
  });

  test('saving writes the state, a thumbnail is optional, metadata follows',
      () async {
    final core = WritingFakeCore();
    final entry = entryFor('/g/Sega Rally.chd');

    final rc = await SaveStateService.save(core, entry, 2);
    expect(rc, 0);

    final slots = await SaveStateService.slotsFor(entry);
    final slot2 = slots.firstWhere((s) => s.index == 2);
    expect(slot2.isEmpty, isFalse);
    expect(slot2.savedAt, isNotNull);
    expect(slot2.bytes, 64);
    expect(File(slot2.statePath).existsSync(), isTrue);

    // No snapshot was passed, so there is no picture -- and that must not stop
    // the slot being usable.
    expect(File(slot2.thumbPath).existsSync(), isFalse);

    final meta = File(p.join(
        p.dirname(slot2.statePath), 'slot2.json'));
    final map = jsonDecode(meta.readAsStringSync()) as Map<String, dynamic>;
    expect(map['game'], 'Sega Rally.chd');
    expect(map['discPath'], '/g/Sega Rally.chd');
  });

  test('a failed save leaves no metadata claiming a state exists', () async {
    final core = WritingFakeCore()..saveStateReturn = -1;
    final entry = entryFor('/g/Nights.chd');

    final rc = await SaveStateService.save(core, entry, 1);
    expect(rc, -1);

    final slots = await SaveStateService.slotsFor(entry);
    expect(slots.firstWhere((s) => s.index == 1).isEmpty, isTrue,
        reason: 'a slot that failed to write must not appear saved');
  });

  test('a manual save never touches the automatic slot', () async {
    final core = WritingFakeCore();
    final entry = entryFor('/g/Burning Rangers.chd');

    await SaveStateService.save(core, entry, kAutoSlot);
    final autoBefore = (await SaveStateService.slotsFor(entry))
        .firstWhere((s) => s.isAuto)
        .savedAt;

    await SaveStateService.save(core, entry, 3);

    final after = await SaveStateService.slotsFor(entry);
    expect(after.firstWhere((s) => s.isAuto).savedAt, autoBefore);
    expect(after.firstWhere((s) => s.index == 3).isEmpty, isFalse);
  });

  test('deleting a slot removes its state, picture and metadata', () async {
    final core = WritingFakeCore();
    final entry = entryFor('/g/Guardian Heroes.chd');
    await SaveStateService.save(core, entry, 4);

    final dir = p.dirname(SaveStateService.statePathFor(entry, 4));
    File(p.join(dir, 'slot4.png')).writeAsBytesSync([1, 2, 3]);

    await SaveStateService.delete(entry, 4);

    expect(File(SaveStateService.statePathFor(entry, 4)).existsSync(), isFalse);
    expect(File(p.join(dir, 'slot4.png')).existsSync(), isFalse);
    expect(File(p.join(dir, 'slot4.json')).existsSync(), isFalse);
    expect((await SaveStateService.slotsFor(entry))
        .firstWhere((s) => s.index == 4)
        .isEmpty, isTrue);
  });

  test('a state with unreadable metadata still lists, dated by the file',
      () async {
    final core = WritingFakeCore();
    final entry = entryFor('/g/Shining Force III.chd');
    await SaveStateService.save(core, entry, 5);

    final dir = p.dirname(SaveStateService.statePathFor(entry, 5));
    File(p.join(dir, 'slot5.json')).writeAsStringSync('{not json');

    final slot = (await SaveStateService.slotsFor(entry))
        .firstWhere((s) => s.index == 5);
    expect(slot.isEmpty, isFalse,
        reason: 'corrupt metadata must not hide a state that exists');
    expect(slot.savedAt, isNotNull);
  });
}
