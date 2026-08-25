// save_state_service.dart — per-game save state slots on disk.
//
// Layout, under the app's private files dir:
//
//   savestates/<gameId>/slot0.sav   the state itself (written by the core)
//                      /slot0.png   what the screen looked like at save time
//                      /slot0.json  when it was saved, and which game it was
//
// Slot 0 is the automatic one, written when a session ends, so Resume always
// has something to offer. Slots 1..5 are the user's and are never written
// without being asked -- an auto-save that can silently overwrite a slot
// someone deliberately kept is a save state system people stop trusting.
//
// gameId is the disc's file name plus a hash of its full path: two discs can
// share a name in different folders, and the name alone would collide their
// states together.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:path/path.dart' as p;
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_log.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';

/// Slot 0: written automatically when a session ends.
const int kAutoSlot = 0;

/// Slots the user manages by hand.
const int kManualSlots = 5;

/// Every slot index the UI shows, auto first.
List<int> get kAllSlots => [kAutoSlot, for (var i = 1; i <= kManualSlots; i++) i];

/// One save state on disk, or an empty slot if [savedAt] is null.
class SaveSlot {
  final int index;
  final String gameId;
  final String gameName;
  final String statePath;
  final String thumbPath;
  final DateTime? savedAt;
  final int bytes;

  const SaveSlot({
    required this.index,
    required this.gameId,
    required this.gameName,
    required this.statePath,
    required this.thumbPath,
    this.savedAt,
    this.bytes = 0,
  });

  bool get isEmpty => savedAt == null;
  bool get isAuto => index == kAutoSlot;

  String get label => isAuto ? 'Auto' : 'Slot $index';
}

class SaveStateService {
  SaveStateService._();

  /// Stable per-game id: readable, and unique across folders.
  static String gameId(MediaEntry entry) {
    final base = p.basenameWithoutExtension(entry.path);
    final safe = base.replaceAll(RegExp(r'[^A-Za-z0-9._-]'), '_');
    // A short path hash disambiguates two copies of the same title without
    // making the directory name unreadable.
    final hash = entry.path.hashCode.toUnsigned(32).toRadixString(16);
    return '${safe}_$hash';
  }

  static Directory _dirFor(MediaEntry entry) =>
      Directory(p.join(YmirCorePaths.saveStateRoot, gameId(entry)));

  static String _statePath(MediaEntry entry, int slot) =>
      p.join(_dirFor(entry).path, 'slot$slot.sav');

  /// The state file for a slot, for callers that hand the path on rather
  /// than loading it themselves (the workbench passes it to EmulatorScreen,
  /// which restores it after the disc is mounted).
  static String statePathFor(MediaEntry entry, int slot) =>
      _statePath(entry, slot);

  static String _thumbPath(MediaEntry entry, int slot) =>
      p.join(_dirFor(entry).path, 'slot$slot.png');

  static String _metaPath(MediaEntry entry, int slot) =>
      p.join(_dirFor(entry).path, 'slot$slot.json');

  /// Every slot for [entry], empty ones included, so the UI can render a
  /// full set of cells rather than only what happens to exist.
  static Future<List<SaveSlot>> slotsFor(MediaEntry entry) async {
    final id = gameId(entry);
    final out = <SaveSlot>[];
    for (final i in kAllSlots) {
      final state = File(_statePath(entry, i));
      DateTime? savedAt;
      int bytes = 0;
      if (await state.exists()) {
        bytes = await state.length();
        savedAt = await _savedAtFrom(entry, i, state);
      }
      out.add(SaveSlot(
        index: i,
        gameId: id,
        gameName: entry.displayName,
        statePath: state.path,
        thumbPath: _thumbPath(entry, i),
        savedAt: savedAt,
        bytes: bytes,
      ));
    }
    return out;
  }

  static Future<DateTime?> _savedAtFrom(
      MediaEntry entry, int slot, File state) async {
    final meta = File(_metaPath(entry, slot));
    if (await meta.exists()) {
      try {
        final map = jsonDecode(await meta.readAsString()) as Map<String, dynamic>;
        final iso = map['savedAt'] as String?;
        if (iso != null) return DateTime.tryParse(iso);
      } catch (_) {
        // Corrupt metadata is not a reason to hide a state that exists.
      }
    }
    // No metadata (or unreadable): the file's own timestamp still tells the
    // user which save is the recent one, which is what the list is for.
    return state.statSync().modified;
  }

  /// Writes [slot] for [entry]: state first, then the thumbnail, then the
  /// metadata. In that order deliberately -- metadata is what the list keys
  /// off, so it is only written once the state it describes is safely down.
  static Future<int> save(
    YmirCore core,
    MediaEntry entry,
    int slot, {
    FrameSnapshot? snapshot,
  }) async {
    await _dirFor(entry).create(recursive: true);
    final path = _statePath(entry, slot);

    final rc = core.saveState(path);
    if (rc != 0) {
      AppLog.log('save state slot $slot failed rc=$rc (${entry.displayName})');
      return rc;
    }

    if (snapshot != null) {
      try {
        final png = await _encodeThumb(snapshot);
        if (png != null) await File(_thumbPath(entry, slot)).writeAsBytes(png);
      } catch (e) {
        // A missing thumbnail costs a picture in the list. It must never
        // cost the state that was already written.
        AppLog.log('thumbnail for slot $slot failed: $e');
      }
    }

    await File(_metaPath(entry, slot)).writeAsString(jsonEncode({
      'savedAt': DateTime.now().toIso8601String(),
      'game': entry.displayName,
      'discPath': entry.path,
      'slot': slot,
    }));

    AppLog.log('save state slot $slot ok (${entry.displayName})');
    return 0;
  }

  /// Restores [slot]. The disc must already be mounted: the core validates
  /// the disc and BIOS hashes and refuses a state that belongs elsewhere.
  static int load(YmirCore core, MediaEntry entry, int slot) {
    final rc = core.loadState(_statePath(entry, slot));
    AppLog.log('load state slot $slot rc=$rc (${entry.displayName})');
    return rc;
  }

  static Future<void> delete(MediaEntry entry, int slot) async {
    for (final f in [
      File(_statePath(entry, slot)),
      File(_thumbPath(entry, slot)),
      File(_metaPath(entry, slot)),
    ]) {
      if (await f.exists()) await f.delete();
    }
    AppLog.log('deleted save state slot $slot (${entry.displayName})');
  }

  /// PNG bytes for the framebuffer, or null if it cannot be encoded.
  static Future<Uint8List?> _encodeThumb(FrameSnapshot snap) async {
    if (snap.width <= 0 || snap.height <= 0) return null;
    final completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(
      snap.argb.buffer.asUint8List(),
      snap.width,
      snap.height,
      ui.PixelFormat.rgba8888,
      completer.complete,
    );
    final image = await completer.future;
    try {
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      return data?.buffer.asUint8List();
    } finally {
      image.dispose();
    }
  }
}
