// storage_permission.dart — All-files access, the Retro-Amiga way.
//
// The library is read in place from wherever the user keeps it -- usually an
// SD card -- and Android 11+ will not let the app read a raw path there
// without the All-files-access grant. The wizard asks BEFORE the first scan,
// because a scan that silently finds nothing reads as "the app is broken",
// not "the app was never allowed to look".
import 'dart:io';

import 'package:flutter/services.dart';

class StoragePermission {
  StoragePermission._();

  static const MethodChannel _channel = MethodChannel('retro_saturn/storage');

  /// Only Android gates raw-path reads this way.
  static bool get isRelevant => Platform.isAndroid;

  static Future<bool> has() async {
    if (!isRelevant) return true;
    try {
      return await _channel.invokeMethod<bool>('hasSharedStorageAccess') ??
          false;
    } catch (_) {
      return false;
    }
  }

  /// Opens the system's All-files-access page and waits for the user to come
  /// back. Returns whether access was granted.
  static Future<bool> request() async {
    if (!isRelevant) return true;
    try {
      return await _channel
              .invokeMethod<bool>('requestSharedStorageAccess')
              .timeout(const Duration(seconds: 90)) ??
          false;
    } catch (_) {
      return false;
    }
  }

  /// Check, ask if needed, re-check. The one call sites use.
  static Future<bool> ensure() async {
    if (await has()) return true;
    return request();
  }
}
