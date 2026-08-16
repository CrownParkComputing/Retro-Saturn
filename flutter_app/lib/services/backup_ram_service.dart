// backup_ram_service.dart — Per-game Saturn internal backup RAM (NVRAM)
// persistence. ymir-core exposes load/save_backup_memory via the C bridge;
// this service keeps one 32 KiB file per game under <app docs>/saves/.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart' as crypto;
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/services/app_log.dart';

class BackupRamService {
  static const _savesSubdir = 'saves';

  /// Path to the on-disk file for a given game. SHA1 of the absolute
  /// game path gives a stable, filesystem-safe filename.
  static File fileFor(String discPath) {
    final digest = crypto.sha1.convert(utf8.encode(discPath)).toString();
    return File(p.join(savesDirSync, '$digest.bin'));
  }

  /// Synchronous saves dir — must be awaited once at startup before any
  /// loadInto/saveFrom call. Returns the path.
  static String _savesDirSyncCached = '';
  static String get savesDirSync {
    if (_savesDirSyncCached.isNotEmpty) return _savesDirSyncCached;
    // path_provider is async-only, so cache at first awaited init.
    throw StateError(
        'BackupRamService.ensureInit() must be awaited before fileFor()');
  }

  /// Call once at app startup (e.g. main() before runApp).
  static Future<void> ensureInit() async {
    if (_savesDirSyncCached.isNotEmpty) return;
    final base = await getApplicationSupportDirectory();
    final d = Directory(p.join(base.path, _savesSubdir));
    if (!d.existsSync()) d.createSync(recursive: true);
    _savesDirSyncCached = d.path;
    return;
  }

  /// Read the 32 KiB backup RAM for a game from disk and feed it to
  /// the emulator. Returns true if a save file existed and was loaded.
  static Future<bool> loadInto(YmirCore core, String discPath) async {
    final f = fileFor(discPath);
    if (!f.existsSync()) return false;
    final tmp = File('${f.path}.load.tmp');
    try {
      await tmp.writeAsBytes(await f.readAsBytes());
      final rc = core.loadBackupMemory(tmp.path, copyOnWrite: false);
      return rc == 0;
    } finally {
      if (tmp.existsSync()) await tmp.delete();
    }
  }

  /// Snapshot the emulator's current backup RAM to disk for the given game.
  /// Called when leaving the emulator or every N seconds while playing.
  static Future<void> saveFrom(YmirCore core, String discPath) async {
    final f = fileFor(discPath);
    final tmp = File('${f.path}.save.tmp');
    try {
      final rc = core.saveBackupMemory(tmp.path);
      if (rc != 0) {
        AppLog.log('NVRAM save: rc=$rc (${discPath.split('/').last})');
        return;
      }
      if (tmp.existsSync()) {
        await tmp.rename(f.path);
        AppLog.log('NVRAM saved: ${f.path.split('/').last}');
      }
    } catch (e) {
      AppLog.log('NVRAM save exception: $e');
      if (tmp.existsSync()) await tmp.delete();
    }
  }

  /// Periodic auto-save while a game is running. Spawn once per session
  /// via `BackupRamService.startAutoSave(core, discPath)`.
  static Timer? _autoSaveTimer;
  static String? _autoSavePath;

  static void startAutoSave(YmirCore core, String discPath,
      {Duration interval = const Duration(seconds: 60)}) {
    _autoSavePath = discPath;
    _autoSaveTimer?.cancel();
    _autoSaveTimer = Timer.periodic(interval, (_) async {
      if (_autoSavePath != null) {
        // Errors are swallowed — in-game BIOS operations like
        // 'Erase backup data' briefly clear the NVRAM; auto-save
        // during that transient state could otherwise throw. The save
        // is a 'best effort' snapshot, not a critical write.
        try {
          await saveFrom(core, _autoSavePath!);
        } catch (_) {
          // ignore — next tick will retry
        }
      }
    });
  }

  static void stopAutoSave() {
    _autoSaveTimer?.cancel();
    _autoSaveTimer = null;
    _autoSavePath = null;
  }
}