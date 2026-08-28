// ymir_native_paths.dart — per-platform resolution of the
// libymircore.{so,dylib} path. Mirrors the ViceMultiplatform pattern:
// Linux uses an absolute path next to the .so, Android uses the bare
// name (jniLibs), iOS uses .framework/<name>, with a Directory.systemTemp
// fallback for iOS where path_provider 2.6 is broken on Linux-built apps.

import 'dart:io';

import 'package:path/path.dart' as p;

class YmirNativePaths {
  /// Absolute path to libymircore.so on the host (Linux).
  static String? get linuxHostLibrary {
    final candidates = <String>[
      p.join(File(Platform.resolvedExecutable).parent.path,
          'lib', 'libymircore.so'),
      p.join(Directory.current.path,
          'native', 'ymir_core', 'linux', 'build', 'libymircore.so'),
      p.join(Directory.current.path, '..', 'native', 'ymir_core',
          'linux', 'build', 'libymircore.so'),
      p.join(Directory.current.path, '..', '..', 'native', 'ymir_core',
          'linux', 'build', 'libymircore.so'),
    ];
    for (final c in candidates) {
      if (File(c).existsSync()) return c;
    }
    return null;
  }

  /// Path to YmirCore.framework/YmirCore on iOS.
  static String? iosFrameworkLibrary() {
    final exe = File(Platform.resolvedExecutable).parent.path;
    final fw = p.join(exe, 'Frameworks', 'YmirCore.framework', 'YmirCore');
    return File(fw).existsSync() ? fw : null;
  }

  /// Returns the right path for this platform, or null if not found.
  static String? resolveLibrary() {
    if (Platform.isAndroid) return null; // bare name via jniLibs
    if (Platform.isIOS) return iosFrameworkLibrary();
    return linuxHostLibrary;
  }
}
