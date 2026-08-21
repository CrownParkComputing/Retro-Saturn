// app_prefs.dart — SharedPreferences wrapper for Retro-Saturn.
// Mirrors ViceMultiplatform's pattern: async load() at startup, key
// constants grouped by feature. Add to this file rather than reading
// SharedPreferences inline from screens.

import 'package:shared_preferences/shared_preferences.dart';

class AppPrefs {
  // Setup wizard
  static const _setupCompletedKey = 'setup_completed';
  static const _biosPathKey = 'bios_path';
  static const _gamesFolderKey = 'games_folder';

  // On-screen pad / input
  static const _leftHandedKey = 'left_handed';

  // Video
  static const _crtEnabledKey = 'crt_enabled';
  static const _bezelEnabledKey = 'bezel_enabled';

  static SharedPreferences? _prefs;

  /// Load SharedPreferences at startup. Idempotent.
  static Future<void> load() async {
    _prefs ??= await SharedPreferences.getInstance();
  }

  static SharedPreferences get _p {
    if (_prefs == null) {
      throw StateError('AppPrefs.load() must be awaited first');
    }
    return _prefs!;
  }

  // ---- setup ----
  static Future<bool> isSetupCompleted() async => _p.getBool(_setupCompletedKey) ?? false;
  static Future<void> setSetupCompleted(bool v) => _p.setBool(_setupCompletedKey, v);

  static Future<String?> getBiosPath() async => _p.getString(_biosPathKey);
  static Future<void> setBiosPath(String p) => _p.setString(_biosPathKey, p);

  static Future<String?> getGamesFolder() async => _p.getString(_gamesFolderKey);
  static Future<void> setGamesFolder(String p) => _p.setString(_gamesFolderKey, p);

  // ---- input ----
  static bool get leftHanded => _p.getBool(_leftHandedKey) ?? false;
  static Future<void> setLeftHanded(bool v) => _p.setBool(_leftHandedKey, v);

  // ---- video ----
  static bool get crtEnabled => _p.getBool(_crtEnabledKey) ?? true;
  static Future<void> setCrtEnabled(bool v) => _p.setBool(_crtEnabledKey, v);

  static bool get bezelEnabled => _p.getBool(_bezelEnabledKey) ?? true;
  static Future<void> setBezelEnabled(bool v) => _p.setBool(_bezelEnabledKey, v);
}