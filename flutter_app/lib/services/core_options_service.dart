// core_options_service.dart — remembers the user's core options and applies
// them to the core.
//
// Two moments matter, and they are the two the user asked for:
//
//   before launching  applyAll() runs once the core exists, so a fresh boot
//                     already has the chosen settings rather than the core's
//                     defaults.
//   during a game     set() applies immediately as well as saving. Every
//                     option here is one ymir-core accepts at runtime; the
//                     couple that only take effect on the next boot say so in
//                     the catalogue rather than being hidden mid-game.
//
// Values are sanitized on the way in AND validated again by the bridge. That
// is not redundant: this side keeps the UI honest, the bridge refuses anything
// that would leave the Saturn in a state it cannot run.

import 'package:retro_saturn/data/core_option.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_log.dart';
import 'package:shared_preferences/shared_preferences.dart';

class CoreOptionsService {
  CoreOptionsService._();

  static const _prefix = 'core_option_';
  static SharedPreferences? _prefs;

  /// Cached so a rebuild of the options screen does not hit disk per row.
  static final Map<YmirCoreOption, int> _values = {};

  static Future<void> load() async {
    _prefs ??= await SharedPreferences.getInstance();
    _values.clear();
    for (final opt in kCoreOptions) {
      final stored = _prefs!.getInt('$_prefix${opt.id.name}');
      _values[opt.id] = opt.sanitize(stored ?? opt.defaultValue);
    }
  }

  /// The value the user has chosen, or the option's default.
  static int valueOf(YmirCoreOption id) =>
      _values[id] ?? coreOptionFor(id).defaultValue;

  static bool isDefault(YmirCoreOption id) =>
      valueOf(id) == coreOptionFor(id).defaultValue;

  /// True if anything has been changed from the core's defaults — the screen
  /// uses this to decide whether "Reset all" is worth offering.
  static bool get anyChanged => kCoreOptions.any((o) => !isDefault(o.id));

  /// Applies every remembered option. Call once after the core is created and
  /// before a disc is loaded.
  static void applyAll(YmirCore core) {
    for (final opt in kCoreOptions) {
      final value = valueOf(opt.id);
      final rc = core.setCoreOption(opt.id, value);
      if (rc != 0) {
        // A refusal here means this build's bounds and the core's disagree.
        // Fall back to the core's own default rather than leaving the user on
        // a setting that did not take.
        AppLog.log('core option ${opt.id.name}=$value refused (rc=$rc), '
            'falling back to ${opt.defaultValue}');
        _values[opt.id] = opt.defaultValue;
        core.setCoreOption(opt.id, opt.defaultValue);
      }
    }
    AppLog.log('core options applied (${kCoreOptions.length})');
  }

  /// Sets one option: applied to the running core and remembered. Returns the
  /// value actually in force, which may differ from what was asked for if the
  /// core refused it.
  static Future<int> set(YmirCore core, YmirCoreOption id, int value) async {
    final opt = coreOptionFor(id);
    final clean = opt.sanitize(value);
    final rc = core.setCoreOption(id, clean);
    if (rc != 0) {
      AppLog.log('core option ${id.name}=$clean refused (rc=$rc)');
      return valueOf(id);
    }
    _values[id] = clean;
    _prefs ??= await SharedPreferences.getInstance();
    await _prefs!.setInt('$_prefix${id.name}', clean);
    return clean;
  }

  /// Puts everything back to ymir-core's own defaults.
  static Future<void> resetAll(YmirCore core) async {
    for (final opt in kCoreOptions) {
      await set(core, opt.id, opt.defaultValue);
    }
  }

  /// Test seam: forget the cache so a test can load a fresh set.
  static void resetForTest() {
    _prefs = null;
    _values.clear();
  }
}
