// ymir_core_paths.dart — Platform-agnostic on-disk paths for
// ymir-core's persistent state. Routes every call through
// path_provider so the same code resolves to:
//
//   Android: /data/data/<pkg>/files/
//   iOS:     <sandbox>/Library/Application Support/
//   Linux:   ~/.local/share/<app>/
//
// The previous incarnation of these paths was hardcoded
// "/sdcard/Android/data/com.crownpark.ymir_multiplatform/files/...",
// which was the Android-only external app dir and broke on iOS + Linux.
//
// Uses [getApplicationSupportDirectory] rather than
// [getExternalStorageDirectory] / [getApplicationDocumentsDirectory]
// so every platform lands on the same kind of "read/write app-private
// dir" semantic. The trade-off: the log file is internal, not
// surfaced through the Android/iOS Files app. Users grab logs from
// in-app via the Logs screen -- the path_provider choice keeps
// the file path derivation contract identical across all three
// targets.
//
// The NVRAM save path is not declared here -- BackupRamService has
// its own caching ensureInit() pattern that uses subdir='saves'.
// Adding it here would just be a parallel structure for the same
// data dir.

import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

class YmirCorePaths {
  YmirCorePaths._();

  static String _baseDir = '';
  static bool _dirsEnsured = false;

  /// One-shot base-dir resolution. Idempotent. Call once at app
  /// startup (before any path getter is touched). The base is cached
  /// so the synchronous getters below can be called from build()
  /// methods without an extra await.
  static Future<void> ensureInit() async {
    if (_baseDir.isNotEmpty) return;
    final base = await getApplicationSupportDirectory();
    _baseDir = base.path;
  }

  /// Make sure every subdir used by the app exists. Idempotent.
  /// Call after [ensureInit] at startup.
  static Future<void> ensureDirs() async {
    if (_dirsEnsured) return;
    await ensureInit();
    for (final sub in const ['roms', 'savestates', 'logs']) {
      final d = Directory(p.join(_baseDir, sub));
      if (!d.existsSync()) {
        await d.create(recursive: true);
      }
    }
    _dirsEnsured = true;
  }

  /// Whether [ensureInit] has resolved the base dir. Returns false on
  /// platforms where path_provider is broken (none of the three
  /// targets, but the check is cheap insurance).
  static bool get isReady => _baseDir.isNotEmpty;

  /// Saturn BIOS persistent SMPC state file. The BIOS writes this
  /// when the user completes the Set Clock / Set Language wizard; on
  /// subsequent boots the wizard auto-skips because the file says so.
  static String get smpcStatePath => p.join(_baseDir, 'roms', 'smpc_state.bin');

  /// Snapshot file written by [YmirCore.saveState] on Pause. Lives
  /// next to the BIOS state file under the app's private files dir
  /// so the upload-keystore-signed release build can read it back at
  /// the same path.
  static String get saveStatePath =>
      p.join(_baseDir, 'savestates', 'session.bin');

  /// Root for per-game save states. Each game gets a directory under
  /// here holding its slots, thumbnails and metadata.
  static String get saveStateRoot => p.join(_baseDir, 'savestates');

  /// App log file. Reachable from the in-app Logs screen; not
  /// surfaced directly through the device file picker.
  static String get appLogPath =>
      p.join(_baseDir, 'logs', 'ymir-multiplatform.log');
}
