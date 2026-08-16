// main.dart — minimal app entry that loads libymircore.{so,dylib},
// creates the YmirCore, and shows the emulator screen. The full
// library + IGDB + bezel + gamepad UI lands in Phase 3.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/ffi/ymir_native_paths.dart';
import 'package:ymir_multiplatform/screens/emulator_screen.dart';

void main() {
  runApp(const YmirApp());
}

class YmirApp extends StatefulWidget {
  const YmirApp({super.key});

  @override
  State<YmirApp> createState() => _YmirAppState();
}

class _YmirAppState extends State<YmirApp> {
  YmirCore? _core;
  String? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _initCore());
  }

  void _initCore() {
    try {
      final libPath = YmirNativePaths.resolveLibrary();
      final bindings = YmirCoreBindings.load(libraryPath: libPath);
      final core = YmirCoreBindingsAdapter(bindings);
      core.create();
      setState(() {
        _core = core;
      });
    } catch (e) {
      setState(() {
        _error = 'Failed to load libymircore: $e';
      });
    }
  }

  @override
  void dispose() {
    _core?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Ymir Multiplatform',
      theme: ThemeData.dark(useMaterial3: true),
      home: Builder(builder: (context) {
        if (_error != null) {
          return Scaffold(
            backgroundColor: Colors.black,
            body: Center(
              child: Padding(
                padding: const EdgeInsets.all(24),
                child: Text(_error!,
                    style: const TextStyle(color: Colors.redAccent),
                    textAlign: TextAlign.center),
              ),
            ),
          );
        }
        if (_core == null) {
          return const Scaffold(
            backgroundColor: Colors.black,
            body: Center(child: CircularProgressIndicator()),
          );
        }
        return EmulatorScreen(core: _core!);
      }),
    );
  }
}