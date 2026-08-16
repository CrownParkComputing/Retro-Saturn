// fake_ymir_core.dart — In-memory fake used by widget tests so they can
// run on the host without libymircore.so / no device / no ROM.
//
// Records every call so tests can assert. Generates a tiny solid-color
// framebuffer so framebuffer_view tests have something to render.

import 'dart:ffi';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';

class FakeYmirCore extends ChangeNotifier implements YmirCore {
  @override
  Pointer<Uint8>? get handle => null; // no native handle

  // Recorded calls
  final List<String> calls = [];
  int _fps = 60;
  int _audioLevel = 0;
  String _status = 'fake';
  bool _paused = false;
  bool _muted = false;
  int _width = 320;
  int _height = 224;
  Uint32List? _frame;
  final List<({int port, YmirButton button, bool pressed})> padEvents = [];
  final List<({int port, YmirPeripheralType type})> peripheralEvents = [];

  // Settable behaviour
  bool throwOnCreate = false;
  int saveStateReturn = 0;

  @override
  void create() {
    calls.add('create');
    if (throwOnCreate) throw StateError('fake create failed');
    _frame = Uint32List(_width * _height);
    for (var i = 0; i < _frame!.length; i++) {
      _frame![i] = 0xFF202040; // dark blue
    }
  }

  @override
  void dispose() => calls.add('dispose');

  @override
  int loadBios(String path) { calls.add('loadBios:$path'); return 0; }
  @override
  int loadDisc(String path) { calls.add('loadDisc:$path'); return 0; }
  @override
  int loadBackupMemory(String path, {bool copyOnWrite = false}) {
    calls.add('loadBup:$path'); return 0;
  }
  @override
  int saveBackupMemory(String path) {
    calls.add('saveBup:$path'); return 0;
  }
  @override
  int loadSmpcState(String path) {
    calls.add('loadSmpc:$path'); return 0;
  }
  @override
  int saveSmpcState(String path) {
    calls.add('saveSmpc:$path'); return 0;
  }

  @override
  int runFrame() { calls.add('runFrame'); return _fps; }
  @override
  void reset({bool hard = false}) => calls.add('reset:hard=$hard');
  @override
  void setPresentationPaused(bool paused) { _paused = paused; }
  @override
  bool get presentationPaused => _paused;

  @override
  void setAudioMuted(bool muted) { _muted = muted; }
  @override
  bool get audioMuted => _muted;
  @override
  int get audioLevel => _audioLevel;

  @override
  int get fps => _fps;
  @override
  String get status => _status;

  @override
  FrameSnapshot? get framebuffer {
    if (_frame == null) return null;
    return FrameSnapshot(width: _width, height: _height, argb: _frame!);
  }

  @override
  void setPeripheralType(int port, YmirPeripheralType type) {
    peripheralEvents.add((port: port, type: type));
  }
  @override
  YmirPeripheralType getPeripheralType(int port) =>
      YmirPeripheralType.controlPad;
  @override
  void setPadButton(int port, YmirButton button, bool pressed) {
    padEvents.add((port: port, button: button, pressed: pressed));
  }
  @override
  void setVirtuaGunInput(int port, int x, int y, bool trigger, bool start) {
    calls.add('gun:$port:$x,$y:$trigger,$start');
  }
  @override
  void setAnalogAxis(int port, int lx, int ly, int rx, int ry) {
    calls.add('analog:$port:$lx,$ly,$rx,$ry');
  }

  @override
  int saveState(String path) {
    calls.add('saveState:$path'); return saveStateReturn;
  }
  @override
  int loadState(String path) { calls.add('loadState:$path'); return 0; }
  @override
  int swapState(String path) { calls.add('swapState:$path'); return 0; }
}