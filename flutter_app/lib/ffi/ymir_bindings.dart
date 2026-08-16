// ymir_bindings.dart — raw `dart:ffi` typedefs for the C ABI in
// native/ymir_core/bridge/ymir_bridge.h.
//
// All functions are 32-bit-int returning except setters (void) and
// `ymir_bridge_get_*` accessors. The framebuffer is XRGB8888
// little-endian (byte order 0xAARRGGBB); Dart's `ui.decodeImageFromPixels`
// accepts this directly on little-endian platforms (all three targets).

import 'dart:ffi';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

/// Saturn peripheral types — matches the C `YmirPeripheralType` enum.
enum YmirPeripheralType {
  none(0),
  controlPad(1),
  analogPad(2),
  arcadeRacer(3),
  missionStick(4),
  virtuaGun(5),
  shuttleMouse(6);

  const YmirPeripheralType(this.value);
  final int value;
}

/// Saturn pad buttons — matches `YmirButton`.
enum YmirButton {
  up(0),
  down(1),
  left(2),
  right(3),
  start(4),
  a(5),
  b(6),
  c(7),
  x(8),
  y(9),
  z(10),
  l(11),
  r(12);

  const YmirButton(this.value);
  final int value;
}

/// Error codes returned by all `ymir_bridge_*` functions that return int.
class YmirErr {
  static const ok = 0;
  static const errGeneric = -1;
  static const errInvalidHandle = -2;
  static const errInvalidArg = -3;
  static const errFileOpen = -4;
  static const errFileRead = -5;
  static const errFileWrite = -6;
  static const errBadFormat = -7;
  static const errSnapshotTimeout = -8;
  static const errSnapshotBadMagic = -9;
  static const errSnapshotVersion = -10;
  static const errBusy = -11;
  static const errNotRunning = -12;
}

// ============================================================
//  Native typedefs (mirrored from ymir_bridge.h)
// ============================================================

typedef _H = ffi.Pointer<ffi.Uint8>; // opaque YmirInstance*
typedef _C = ffi.Pointer<Utf8>; // const char* (C string)

// All Dart-side typedefs use Dart int (not ffi.Int32) because Dart FFI
// auto-converts between the two for primitive integer types. Using
// ffi.Int32 on the Dart side caused a "can't assign int to Int32" type
// error in Dart 3.11.
typedef _CreateNative = ffi.Pointer<ffi.Uint8> Function();
typedef _CreateDart = ffi.Pointer<ffi.Uint8> Function();

typedef _VoidHandleNative = ffi.Void Function(_H);
typedef _VoidHandleDart = void Function(ffi.Pointer<ffi.Uint8>);

typedef _IntHandleStrNative = ffi.Int32 Function(_H, _C);
typedef _IntHandleStrDart = int Function(ffi.Pointer<ffi.Uint8>, ffi.Pointer<Utf8>);

typedef _IntHandleStrIntNative = ffi.Int32 Function(_H, _C, ffi.Int32);
typedef _IntHandleStrIntDart = int Function(
    ffi.Pointer<ffi.Uint8>, ffi.Pointer<Utf8>, int);

typedef _IntHandleIntNative = ffi.Int32 Function(_H, ffi.Int32);
typedef _IntHandleIntDart = int Function(ffi.Pointer<ffi.Uint8>, int);

typedef _IntHandleNative = ffi.Int32 Function(_H);
typedef _IntHandleDart = int Function(ffi.Pointer<ffi.Uint8>);

typedef _VoidHandleIntNative = ffi.Void Function(_H, ffi.Int32);
typedef _VoidHandleIntDart = void Function(ffi.Pointer<ffi.Uint8>, int);

typedef _VoidHandleIntIntNative = ffi.Void Function(_H, ffi.Int32, ffi.Int32);
typedef _VoidHandleIntIntDart = void Function(ffi.Pointer<ffi.Uint8>, int, int);

typedef _VoidHandleIntIntIntNative = ffi.Void Function(_H, ffi.Int32, ffi.Int32, ffi.Int32);
typedef _VoidHandleIntIntIntDart = void Function(ffi.Pointer<ffi.Uint8>, int, int, int);

typedef _VoidHandle5IntNative =
    ffi.Void Function(_H, ffi.Int32, ffi.Int32, ffi.Int32, ffi.Int32, ffi.Int32);
typedef _VoidHandle5IntDart =
    void Function(ffi.Pointer<ffi.Uint8>, int, int, int, int, int);

typedef _StrHandleNative = ffi.Pointer<Utf8> Function(_H);
typedef _StrHandleDart = ffi.Pointer<Utf8> Function(ffi.Pointer<ffi.Uint8>);

typedef _FbHandleNative = ffi.Pointer<ffi.Uint32> Function(
    _H, ffi.Pointer<ffi.Int32>, ffi.Pointer<ffi.Int32>);
typedef _FbHandleDart = ffi.Pointer<ffi.Uint32> Function(
    ffi.Pointer<ffi.Uint8>, ffi.Pointer<ffi.Int32>, ffi.Pointer<ffi.Int32>);

/// Low-level bindings to libymircore.{so,dylib}.
class YmirCoreBindings {
  final DynamicLibrary _lib;

  YmirCoreBindings._(this._lib);

  factory YmirCoreBindings.load({String? libraryPath}) {
    final DynamicLibrary lib;
    if (Platform.isLinux) {
      lib = DynamicLibrary.open(libraryPath ?? 'libymircore.so');
    } else if (Platform.isAndroid) {
      lib = DynamicLibrary.open(libraryPath ?? 'libymircore.so');
    } else if (Platform.isIOS) {
      lib = libraryPath != null
          ? DynamicLibrary.open(libraryPath)
          : DynamicLibrary.process();
    } else {
      throw UnsupportedError(
          'ymir_multiplatform: no libymircore binding for ${Platform.operatingSystem}');
    }
    return YmirCoreBindings._(lib);
  }

  late final _create = _lib.lookupFunction<_CreateNative, _CreateDart>(
      'ymir_bridge_create');
  late final _destroy = _lib.lookupFunction<_VoidHandleNative, _VoidHandleDart>(
      'ymir_bridge_destroy');

  late final _loadBios = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_load_bios');
  late final _loadDisc = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_load_disc');
  late final _loadBup = _lib.lookupFunction<_IntHandleStrIntNative, _IntHandleStrIntDart>(
      'ymir_bridge_load_internal_backup_memory');
  late final _saveBup = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_save_internal_backup_memory');
  late final _loadSmpc = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_load_smpc_state');
  late final _saveSmpc = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_save_smpc_state');

  late final _runFrame = _lib.lookupFunction<_IntHandleNative, _IntHandleDart>(
      'ymir_bridge_run_frame');
  late final _reset = _lib.lookupFunction<_VoidHandleIntNative, _VoidHandleIntDart>(
      'ymir_bridge_reset');

  late final _setPaused = _lib.lookupFunction<_VoidHandleIntNative, _VoidHandleIntDart>(
      'ymir_bridge_set_presentation_paused');
  late final _getPaused = _lib.lookupFunction<_IntHandleNative, _IntHandleDart>(
      'ymir_bridge_get_presentation_paused');

  late final _setMuted = _lib.lookupFunction<_VoidHandleIntNative, _VoidHandleIntDart>(
      'ymir_bridge_set_audio_muted');
  late final _getMuted = _lib.lookupFunction<_IntHandleNative, _IntHandleDart>(
      'ymir_bridge_get_audio_muted');
  late final _getLevel = _lib.lookupFunction<_IntHandleNative, _IntHandleDart>(
      'ymir_bridge_get_audio_level');
  late final _getFps = _lib.lookupFunction<_IntHandleNative, _IntHandleDart>(
      'ymir_bridge_get_fps');
  late final _getStatus = _lib.lookupFunction<_StrHandleNative, _StrHandleDart>(
      'ymir_bridge_get_status');

  late final _getFramebuffer = _lib.lookupFunction<_FbHandleNative, _FbHandleDart>(
      'ymir_bridge_get_framebuffer');

  late final _setPeripheralType = _lib.lookupFunction<_VoidHandleIntIntNative, _VoidHandleIntIntDart>(
      'ymir_bridge_set_peripheral_type');
  late final _getPeripheralType = _lib.lookupFunction<_IntHandleIntNative, _IntHandleIntDart>(
      'ymir_bridge_get_peripheral_type');

  late final _setPadButton = _lib.lookupFunction<_VoidHandleIntIntIntNative, _VoidHandleIntIntIntDart>(
      'ymir_bridge_set_pad_button');

  late final _setGun = _lib.lookupFunction<_VoidHandle5IntNative, _VoidHandle5IntDart>(
      'ymir_bridge_set_virtua_gun_input');

  late final _setAnalog = _lib.lookupFunction<_VoidHandle5IntNative, _VoidHandle5IntDart>(
      'ymir_bridge_set_analog_axis');

  late final _saveState = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_save_state');
  late final _loadState = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_load_state');
  late final _swapState = _lib.lookupFunction<_IntHandleStrNative, _IntHandleStrDart>(
      'ymir_bridge_swap_state');

  // ============================================================
  //  Public Dart wrappers
  // ============================================================

  ffi.Pointer<ffi.Uint8> create() => _create();

  void destroy(ffi.Pointer<ffi.Uint8> p) => _destroy(p);

  int loadBios(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _loadBios(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int loadDisc(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _loadDisc(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int loadBackupMemory(ffi.Pointer<ffi.Uint8> p, String path,
      {bool copyOnWrite = false}) {
    final cstr = path.toNativeUtf8();
    try {
      return _loadBup(p, cstr, copyOnWrite ? 1 : 0);
    } finally {
      calloc.free(cstr);
    }
  }

  int saveBackupMemory(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _saveBup(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int loadSmpcState(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _loadSmpc(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int saveSmpcState(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _saveSmpc(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int runFrame(ffi.Pointer<ffi.Uint8> p) => _runFrame(p);

  void reset(ffi.Pointer<ffi.Uint8> p, {bool hard = false}) =>
      _reset(p, hard ? 1 : 0);

  void setPresentationPaused(ffi.Pointer<ffi.Uint8> p, bool paused) =>
      _setPaused(p, paused ? 1 : 0);

  bool getPresentationPaused(ffi.Pointer<ffi.Uint8> p) =>
      _getPaused(p) != 0;

  void setAudioMuted(ffi.Pointer<ffi.Uint8> p, bool muted) =>
      _setMuted(p, muted ? 1 : 0);

  bool getAudioMuted(ffi.Pointer<ffi.Uint8> p) => _getMuted(p) != 0;

  int getAudioLevel(ffi.Pointer<ffi.Uint8> p) => _getLevel(p);
  int getFps(ffi.Pointer<ffi.Uint8> p) => _getFps(p);
  String getStatus(ffi.Pointer<ffi.Uint8> p) =>
      _getStatus(p).toDartString();

  /// Returns the current framebuffer (XRGB8888 little-endian) and its size.
  /// The Uint32List is a copy — the underlying pointer is only valid until
  /// the next frame completes.
  FrameSnapshot? getFramebuffer(ffi.Pointer<ffi.Uint8> p) {
    final wPtr = calloc<ffi.Int32>();
    final hPtr = calloc<ffi.Int32>();
    try {
      final fbPtr = _getFramebuffer(p, wPtr, hPtr);
      final w = wPtr.value;
      final h = hPtr.value;
      if (fbPtr == nullptr || w == 0 || h == 0) return null;
      final len = w * h;
      final list = Uint32List.fromList(fbPtr.asTypedList(len));
      return FrameSnapshot(width: w, height: h, argb: list);
    } finally {
      calloc.free(wPtr);
      calloc.free(hPtr);
    }
  }

  void setPeripheralType(ffi.Pointer<ffi.Uint8> p, int port,
          YmirPeripheralType type) =>
      _setPeripheralType(p, port, type.value);

  YmirPeripheralType getPeripheralType(ffi.Pointer<ffi.Uint8> p, int port) =>
      YmirPeripheralType.values[_getPeripheralType(p, port)];

  void setPadButton(
          ffi.Pointer<ffi.Uint8> p, int port, YmirButton button, bool pressed) =>
      _setPadButton(p, port, button.value, pressed ? 1 : 0);

  void setVirtuaGunInput(ffi.Pointer<ffi.Uint8> p, int port, int x, int y,
          bool trigger, bool start) {
    _setGun(p, port, x, y, trigger ? 1 : 0, start ? 1 : 0);
  }

  void setAnalogAxis(ffi.Pointer<ffi.Uint8> p, int port, int lx, int ly,
          int rx, int ry) {
    _setAnalog(p, port, lx, ly, rx, ry);
  }

  int saveState(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      return _saveState(p, cstr);
    } finally {
      calloc.free(cstr);
    }
  }

  int loadState(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      final raw = _loadState(p, cstr);
      return (raw is int) ? raw : 0;
    } finally {
      calloc.free(cstr);
    }
  }

  int swapState(ffi.Pointer<ffi.Uint8> p, String path) {
    final cstr = path.toNativeUtf8();
    try {
      final raw = _swapState(p, cstr);
      return (raw is int) ? raw : 0;
    } finally {
      calloc.free(cstr);
    }
  }
}

/// A snapshot of the emulator's current framebuffer.
class FrameSnapshot {
  final int width;
  final int height;
  /// XRGB8888 little-endian. The native pixel format 0xAARRGGBB is fed
  /// directly to `ui.decodeImageFromPixels(..., PixelFormat.rgba8888)`.
  final Uint32List argb;

  FrameSnapshot({required this.width, required this.height, required this.argb});
}