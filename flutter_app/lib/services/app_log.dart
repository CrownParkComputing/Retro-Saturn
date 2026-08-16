// app_log.dart — A log the user can actually send me.
//
// Mirrors ViceMultiplatform's AppLog. Records Dart-side events to
// memory + a file under the app's external docs dir, with optional
// stdout/stderr capture for the native side (iOS only — Android goes
// to logcat instead, where adb is enough).
//
// The log lives in the app's external files dir, which the user can
// reach via the system Files app on Android (Android/data/<pkg>/files)
// or via the iOS Files app (when UIFileSharingEnabled is set). A log
// the user cannot get at is not a bug report.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as p;

class AppLog {
  AppLog._();

  /// Lines logged from Dart this session. Bounded so the log doesn't
  /// grow unbounded for hours-long sessions.
  static final List<String> _lines = <String>[];
  static const int _maxLines = 2000;

  static String? _filePath;
  static bool _initialized = false;

  /// Where the log file lives, once [init] has run.
  static String? get filePath => _filePath;

  /// One-shot setup. Idempotent. Writes to the app's private external
  /// storage dir (`/sdcard/Android/data/<pkg>/files/logs/`) so the log
  /// is reachable from the system Files app on Android, and from a
  /// `run-as` shell on debuggable builds.
  static Future<void> init() async {
    if (_initialized) return;
    _initialized = true;
    try {
      final base = '/sdcard/Android/data/com.crownpark.ymir_multiplatform/files';
      final logsDir = Directory(p.join(base, 'logs'));
      if (!logsDir.existsSync()) await logsDir.create(recursive: true);
      _filePath = p.join(logsDir.path, 'ymir-multiplatform.log');
    } catch (e) {
      _append('log init failed: $e');
    }
  }

  /// Records a line, to memory and to the file.
  static void log(String message) {
    final stamp = DateTime.now().toIso8601String().substring(11, 23);
    final line = '$stamp  $message';
    _append(line);
    final path = _filePath;
    if (path == null) return;
    try {
      File(path).writeAsStringSync('$line\n', mode: FileMode.append);
    } catch (_) {
      // A log write must never take the app down.
    }
  }

  static void _append(String line) {
    _lines.add(line);
    if (_lines.length > _maxLines) {
      _lines.removeRange(0, _lines.length - _maxLines);
    }
  }

  /// Everything worth showing: the Dart lines this session.
  /// Reads the file rather than returning [_lines], so we see lines
  /// from any process that wrote to the file.
  static Future<String> read() async {
    final path = _filePath;
    if (path == null) return _lines.join('\n');
    try {
      final text = await File(path).readAsString();
      return text.isEmpty ? _lines.join('\n') : text;
    } catch (e) {
      return '${_lines.join('\n')}\n(could not read $path: $e)';
    }
  }

  /// Empties the log, keeping a header so a fresh report says when it started.
  static Future<void> clear() async {
    _lines.clear();
    final path = _filePath;
    if (path == null) return;
    try {
      await File(path).writeAsString(
          '=== cleared ${DateTime.now().toIso8601String()} ===\n');
    } catch (_) {}
  }
}