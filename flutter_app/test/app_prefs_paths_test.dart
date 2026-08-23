// The app's data container is not a stable location. iOS reassigns the UUID in
// .../Application/<UUID>/Documents on reinstall, restore and device migration,
// so a saved absolute path silently stops resolving: no error, just a BIOS
// that is "not found" and an empty shelf, on an install where the user's files
// are still exactly where they left them.
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:path/path.dart' as p;
import 'package:shared_preferences/shared_preferences.dart';
import 'package:retro_saturn/services/app_prefs.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory container;   // stands in for .../Application/<UUID>
  late Directory docs;

  void pointAppAt(Directory d) {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      const MethodChannel('plugins.flutter.io/path_provider'),
      (call) async => d.path,
    );
  }

  setUp(() async {
    container = Directory.systemTemp.createTempSync('saturn_container');
    docs = Directory(p.join(container.path, 'Documents'))
      ..createSync(recursive: true);
    pointAppAt(docs);
    SharedPreferences.setMockInitialValues(<String, Object>{});
    AppPrefs.resetForTest();
    await AppPrefs.load();
  });

  tearDown(() => container.deleteSync(recursive: true));

  test('a path inside Documents survives the container being reassigned',
      () async {
    final bios = File(p.join(docs.path, 'Retro-Saturn', 'BIOS', 'b.bin'))
      ..createSync(recursive: true)
      ..writeAsBytesSync(List<int>.filled(16, 0));
    await AppPrefs.setBiosPath(bios.path);
    expect(await AppPrefs.getBiosPath(), bios.path);

    // Reinstall: same files, new container UUID.
    final moved = Directory.systemTemp.createTempSync('saturn_container2');
    final movedDocs = Directory(p.join(moved.path, 'Documents'))
      ..createSync(recursive: true);
    File(p.join(movedDocs.path, 'Retro-Saturn', 'BIOS', 'b.bin'))
      ..createSync(recursive: true)
      ..writeAsBytesSync(List<int>.filled(16, 0));
    pointAppAt(movedDocs);

    expect(await AppPrefs.getBiosPath(),
        p.join(movedDocs.path, 'Retro-Saturn', 'BIOS', 'b.bin'));
    moved.deleteSync(recursive: true);
  });

  test('the games folder is stored relative, not as an absolute', () async {
    final games = Directory(p.join(docs.path, 'Retro-Saturn'))
      ..createSync(recursive: true);
    await AppPrefs.setGamesFolder(games.path);
    final raw = (await SharedPreferences.getInstance()).getString('games_folder');
    expect(raw, '@documents/Retro-Saturn');
    expect(raw, isNot(contains(container.path)));
  });

  test('Documents itself round-trips', () async {
    await AppPrefs.setGamesFolder(docs.path);
    expect(await AppPrefs.getGamesFolder(), docs.path);
  });

  test('an absolute from an older build is rebased onto this container',
      () async {
    File(p.join(docs.path, 'Retro-Saturn', 'BIOS', 'b.bin'))
      ..createSync(recursive: true)
      ..writeAsBytesSync(List<int>.filled(16, 0));
    // Exactly what was found on the simulator: a real path into a container
    // that no longer exists.
    SharedPreferences.setMockInitialValues(<String, Object>{
      'bios_path': '/var/mobile/Containers/Data/Application/'
          '4B4EC4AE-0D69-4144-9724-3C564334A7DA/Documents/'
          'Retro-Saturn/BIOS/b.bin',
    });
    AppPrefs.resetForTest();
    await AppPrefs.load();
    expect(await AppPrefs.getBiosPath(),
        p.join(docs.path, 'Retro-Saturn', 'BIOS', 'b.bin'));
  });

  test('a path outside Documents that still resolves is left alone', () async {
    // Not ours to rewrite: it may be a genuine external location.
    final outside = File(p.join(container.path, 'elsewhere.bin'))
      ..createSync(recursive: true)
      ..writeAsBytesSync(<int>[0]);
    await AppPrefs.setBiosPath(outside.path);
    expect(await AppPrefs.getBiosPath(), outside.path);
  });

  test('an unresolvable path is returned unchanged, not silently invented',
      () async {
    SharedPreferences.setMockInitialValues(<String, Object>{
      'bios_path': '/nowhere/at/all/b.bin',
    });
    AppPrefs.resetForTest();
    await AppPrefs.load();
    expect(await AppPrefs.getBiosPath(), '/nowhere/at/all/b.bin');
  });
}
