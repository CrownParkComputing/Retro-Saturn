// app_prefs.dart — SharedPreferences wrapper for Retro-Saturn.
// Mirrors ViceMultiplatform's pattern: async load() at startup, key
// constants grouped by feature. Add to this file rather than reading
// SharedPreferences inline from screens.

import 'dart:ui' show Offset;
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart' show visibleForTesting;

import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AppPrefs {
  // Setup wizard
  static const _setupCompletedKey = 'setup_completed';
  static const _setupVersionKey = 'setup_version';
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

  /// Drops the cached instance so a freshly installed mock store takes effect.
  /// [load] is deliberately idempotent, which means a test that swaps the
  /// backing store mid-run would otherwise keep reading the previous one.
  @visibleForTesting
  static void resetForTest() => _prefs = null;

  static SharedPreferences get _p {
    if (_prefs == null) {
      throw StateError('AppPrefs.load() must be awaited first');
    }
    return _prefs!;
  }

  // ---- setup ----
  /// True when the wizard finished AND was last finished for the current
  /// build. A new version bumps the flag back to false so every shipped
  /// build gets a fresh first-run experience -- this is the contract the
  /// store review team and any new user picks up on first install.
  static Future<bool> isSetupCompletedFor(String version) async {
    if (!(_p.getBool(_setupCompletedKey) ?? false)) return false;
    final seen = _p.getString(_setupVersionKey);
    if (seen == null) {
      // An older build set the boolean but never wrote a version. Mark the
      // current build as seen so the wizard does not re-run on every launch
      // of that same build; the next version bump will trip the flag.
      await _p.setString(_setupVersionKey, version);
      return true;
    }
    return seen == version;
  }

  static Future<void> setSetupCompletedFor(String version) async {
    await _p.setBool(_setupCompletedKey, true);
    await _p.setString(_setupVersionKey, version);
  }

  /// Legacy boolean check kept for the rare caller that wants to know
  /// "has the wizard EVER been completed" -- e.g. deciding whether to
  /// greet the user on a build that does not bump the version.
  static Future<bool> isSetupCompleted() async => _p.getBool(_setupCompletedKey) ?? false;
  static Future<void> setSetupCompleted(bool v) => _p.setBool(_setupCompletedKey, v);


  // ---- container-relative paths ----
  //
  // iOS does not guarantee the app's data container keeps its UUID. It is
  //   .../Application/<UUID>/Documents/...
  // and that UUID is reassigned on reinstall, and on restore from a backup or
  // migration to a new device. An absolute path saved into SharedPreferences
  // therefore points at a directory that no longer exists, and nothing reports
  // it: the BIOS is simply "not found" and the shelf is simply empty, on an
  // install where the user did nothing wrong and their files are still there.
  //
  // Observed exactly that: the wizard had stored
  //   .../Application/4B4EC4AE-.../Documents/Retro-Saturn
  // while the app was running out of 11377381-. Apple's guidance is explicit
  // that container paths must not be persisted, only re-derived.
  //
  // So a path inside Documents is stored relative to it and rejoined on read.
  // A path outside Documents is stored as-is: it is somebody else's directory
  // and not ours to rewrite.
  static const _docsPrefix = '@documents/';

  static Future<String> _docsDir() async =>
      (await getApplicationDocumentsDirectory()).path;

  static Future<String> _portable(String abs) async {
    final docs = await _docsDir();
    if (p.equals(docs, abs)) return _docsPrefix;
    if (p.isWithin(docs, abs)) return _docsPrefix + p.relative(abs, from: docs);
    return abs;
  }

  static bool _exists(String path) =>
      File(path).existsSync() || Directory(path).existsSync();

  static Future<String> _resolve(String stored) async {
    if (stored.startsWith(_docsPrefix)) {
      final rest = stored.substring(_docsPrefix.length);
      final docs = await _docsDir();
      return rest.isEmpty ? docs : p.join(docs, rest);
    }
    // Written by a build that stored absolutes. If it still resolves, leave it
    // alone -- it may legitimately live outside the container. If it does not,
    // try the same tail under today's Documents before giving up, which is the
    // stale-container case and recovers the user's setup silently.
    if (!_exists(stored)) {
      const marker = '/Documents/';
      final i = stored.lastIndexOf(marker);
      if (i != -1) {
        final rebased =
            p.join(await _docsDir(), stored.substring(i + marker.length));
        if (_exists(rebased)) return rebased;
      }
    }
    return stored;
  }

  static Future<String?> getBiosPath() async {
    final v = _p.getString(_biosPathKey);
    return v == null ? null : _resolve(v);
  }

  static Future<void> setBiosPath(String path) async =>
      _p.setString(_biosPathKey, await _portable(path));

  static Future<String?> getGamesFolder() async {
    final v = _p.getString(_gamesFolderKey);
    return v == null ? null : _resolve(v);
  }

  static Future<void> setGamesFolder(String path) async =>
      _p.setString(_gamesFolderKey, await _portable(path));

  // ---- input ----
  static bool get leftHanded => _p.getBool(_leftHandedKey) ?? false;
  static Future<void> setLeftHanded(bool v) => _p.setBool(_leftHandedKey, v);

  // ---- video ----
  /// Whether Delete in the library asks first. On by default; the switch
  /// to turn it off lives in Paths for people who trust their thumbs.
  static const _confirmDeleteKey = 'confirm_delete';
  static Future<bool> getConfirmDelete() async =>
      _p.getBool(_confirmDeleteKey) ?? true;
  static Future<void> setConfirmDelete(bool v) =>
      _p.setBool(_confirmDeleteKey, v);

  /// Whether a new session starts with the on-screen pad visible.
  static const _showPadKey = 'show_pad_default';
  static Future<bool> getShowPadDefault() async =>
      _p.getBool(_showPadKey) ?? false;
  static Future<void> setShowPadDefault(bool v) => _p.setBool(_showPadKey, v);

  static const _screenFillKey = 'screen_fill';
  static bool get screenFill => _p.getBool(_screenFillKey) ?? false;
  static Future<void> setScreenFill(bool v) => _p.setBool(_screenFillKey, v);

  static bool get crtEnabled => _p.getBool(_crtEnabledKey) ?? true;
  static Future<void> setCrtEnabled(bool v) => _p.setBool(_crtEnabledKey, v);

  static bool get bezelEnabled => _p.getBool(_bezelEnabledKey) ?? true;
  static Future<void> setBezelEnabled(bool v) => _p.setBool(_bezelEnabledKey, v);

  // --- On-screen control positions -------------------------------------

  static const _controlPositionsKey = 'on_screen_control_positions';

  /// Where each on-screen control cluster sits, as fractions of the play
  /// area (0..1, centre of the control). Fractions rather than pixels so a
  /// layout made in landscape still means the same place after a resize or
  /// on the next device. Same contract as Retro-C64 and Retro-Spectrum.
  static Future<Map<String, Offset>> getControlPositions() async {
    final raw = _p.getString(_controlPositionsKey);
    if (raw == null || raw.isEmpty) return const {};
    try {
      final decoded = jsonDecode(raw) as Map<String, dynamic>;
      return {
        for (final e in decoded.entries)
          e.key: Offset(
            ((e.value as List)[0] as num).toDouble(),
            ((e.value as List)[1] as num).toDouble(),
          ),
      };
    } catch (_) {
      return const {};
    }
  }

  static Future<void> setControlPosition(String id, Offset fraction) async {
    final all = Map<String, Offset>.from(await getControlPositions());
    all[id] = fraction;
    await _p.setString(
      _controlPositionsKey,
      jsonEncode({
        for (final e in all.entries) e.key: [e.value.dx, e.value.dy],
      }),
    );
  }

  static Future<void> clearControlPositions() =>
      _p.remove(_controlPositionsKey);
}