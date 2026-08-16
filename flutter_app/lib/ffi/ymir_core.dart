// ymir_core.dart — abstract `YmirCore` interface + concrete
// `YmirCoreBindings` implementation. Mirrors ViceMultiplatform's
// `vice_core.dart` so screens never touch `dart:ffi` directly.

import 'dart:ffi';
import 'dart:ffi' as ffi;

import 'ymir_bindings.dart';

/// Abstract interface — everything screens need from the emulator.
/// Tests provide a `FakeYmirCore`; production uses `YmirCoreBindings`.
abstract class YmirCore {
  /// Native handle (opaque). Null until create() succeeds.
  Pointer<Uint8>? get handle;

  /// Lifecycle.
  void create();
  void dispose();

  // Media
  int loadBios(String path);
  int loadDisc(String path);
  int loadBackupMemory(String path, {bool copyOnWrite = false});
  int saveBackupMemory(String path);
  int loadSmpcState(String path);
  int saveSmpcState(String path);

  // Emulation
  int runFrame();
  void reset({bool hard = false});
  void setPresentationPaused(bool paused);
  bool get presentationPaused;

  // Audio
  void setAudioMuted(bool muted);
  bool get audioMuted;
  int get audioLevel;

  // Status
  int get fps;
  String get status;

  // Framebuffer
  FrameSnapshot? get framebuffer;

  // Peripherals
  void setPeripheralType(int port, YmirPeripheralType type);
  YmirPeripheralType getPeripheralType(int port);
  void setPadButton(int port, YmirButton button, bool pressed);
  void setVirtuaGunInput(int port, int x, int y, bool trigger, bool start);
  void setVirtuaGunFbSize(int width, int height);
  void setRtcToHost({int offsetSeconds = 0});
  void setAnalogAxis(int port, int lx, int ly, int rx, int ry);

  // Save state
  int saveState(String path);
  int loadState(String path);
  int swapState(String path);
}

/// Concrete production implementation backed by `dart:ffi`.
class YmirCoreBindingsAdapter implements YmirCore {
  final YmirCoreBindings _bindings;
  ffi.Pointer<ffi.Uint8>? _handle;

  YmirCoreBindingsAdapter(this._bindings);

  @override
  ffi.Pointer<ffi.Uint8>? get handle => _handle;

  @override
  void create() {
    _handle = _bindings.create();
  }

  @override
  void dispose() {
    final p = _handle;
    if (p != null) {
      _bindings.destroy(p);
      _handle = null;
    }
  }

  ffi.Pointer<ffi.Uint8> _h() {
    final p = _handle;
    if (p == null) {
      throw StateError('YmirCore.create() was never called');
    }
    return p;
  }

  @override
  int loadBios(String path) => _bindings.loadBios(_h(), path);

  @override
  int loadDisc(String path) => _bindings.loadDisc(_h(), path);

  @override
  int loadBackupMemory(String path, {bool copyOnWrite = false}) =>
      _bindings.loadBackupMemory(_h(), path, copyOnWrite: copyOnWrite);

  @override
  int saveBackupMemory(String path) =>
      _bindings.saveBackupMemory(_h(), path);

  @override
  int loadSmpcState(String path) => _bindings.loadSmpcState(_h(), path);

  @override
  int saveSmpcState(String path) => _bindings.saveSmpcState(_h(), path);

  @override
  int runFrame() => _bindings.runFrame(_h());

  @override
  void reset({bool hard = false}) => _bindings.reset(_h(), hard: hard);

  @override
  void setPresentationPaused(bool paused) =>
      _bindings.setPresentationPaused(_h(), paused);

  @override
  bool get presentationPaused => _bindings.getPresentationPaused(_h());

  @override
  void setAudioMuted(bool muted) => _bindings.setAudioMuted(_h(), muted);

  @override
  bool get audioMuted => _bindings.getAudioMuted(_h());

  @override
  int get audioLevel => _bindings.getAudioLevel(_h());

  @override
  int get fps => _bindings.getFps(_h());

  @override
  String get status => _bindings.getStatus(_h());

  @override
  FrameSnapshot? get framebuffer => _bindings.getFramebuffer(_h());

  @override
  void setPeripheralType(int port, YmirPeripheralType type) =>
      _bindings.setPeripheralType(_h(), port, type);

  @override
  YmirPeripheralType getPeripheralType(int port) =>
      _bindings.getPeripheralType(_h(), port);

  @override
  void setPadButton(int port, YmirButton button, bool pressed) =>
      _bindings.setPadButton(_h(), port, button, pressed);

  @override
  void setVirtuaGunInput(int port, int x, int y, bool trigger, bool start) =>
      _bindings.setVirtuaGunInput(_h(), port, x, y, trigger, start);

  @override
  void setVirtuaGunFbSize(int width, int height) =>
      _bindings.setVirtuaGunFbSize(_h(), width, height);

  @override
  void setRtcToHost({int offsetSeconds = 0}) =>
      _bindings.setRtcToHost(_h(), offsetSeconds);

  @override
  void setAnalogAxis(int port, int lx, int ly, int rx, int ry) =>
      _bindings.setAnalogAxis(_h(), port, lx, ly, rx, ry);

  @override
  int saveState(String path) => _bindings.saveState(_h(), path);

  @override
  int loadState(String path) => _bindings.loadState(_h(), path);

  @override
  int swapState(String path) => _bindings.swapState(_h(), path);
}