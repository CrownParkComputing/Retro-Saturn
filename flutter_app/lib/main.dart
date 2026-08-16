// main.dart — Ymir Multiplatform app entry. Loads libymircore.{so,dylib},
// creates the YmirCore, restores SMPC state + backup RAM, then routes
// to SetupWizardScreen or WorkbenchScreen based on whether setup is
// complete. Mirrors ViceMultiplatform's setup pattern.

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/ffi/ymir_bindings.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/ffi/ymir_native_paths.dart';
import 'package:ymir_multiplatform/screens/setup_wizard_screen.dart';
import 'package:ymir_multiplatform/screens/workbench_screen.dart';
import 'package:ymir_multiplatform/services/app_prefs.dart';
import 'package:ymir_multiplatform/services/backup_ram_service.dart';
import 'package:ymir_multiplatform/services/smpc_state_service.dart';
import 'package:ymir_multiplatform/services/smpc_state_service.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await AppPrefs.load();
  await BackupRamService.ensureInit();
  runApp(const YmirApp());
}

class YmirApp extends StatefulWidget {
  const YmirApp({super.key});

  @override
  State<YmirApp> createState() => _YmirAppState();
}

class _YmirAppState extends State<YmirApp> with WidgetsBindingObserver {
  YmirCore? _core;
  String? _loadError;
  bool? _setupCompleted;

  /// Whether the emulator core was paused before backgrounding, so
  /// coming back doesn't un-pause something the user paused deliberately.
  bool _corePausedBeforeBackground = false;

  /// Saturn BIOS SMPC persistent state — restored on launch to skip the
  /// Set Clock / Set Language wizard when the user has completed it.
  static const _smpcStatePath =
      '/sdcard/Android/data/com.crownpark.ymir_multiplatform/files/roms/smpc_state.bin';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _loadCore();
    _checkSetup();
  }

  @override
  void dispose() {
    final core = _core;
    if (core != null) {
      core.saveSmpcState(_smpcStatePath);
      core.dispose();
    }
    BackupRamService.stopAutoSave();
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    super.didChangeAppLifecycleState(state);
    final foreground = state == AppLifecycleState.resumed;
    final core = _core;
    if (!foreground) {
      _corePausedBeforeBackground = core?.presentationPaused ?? false;
      if (!_corePausedBeforeBackground) core?.setPresentationPaused(true);
    } else {
      if (!_corePausedBeforeBackground) core?.setPresentationPaused(false);
    }
  }

  Future<void> _loadCore() async {
    try {
      final libPath = YmirNativePaths.resolveLibrary();
      final bindings = YmirCoreBindings.load(libraryPath: libPath);
      final core = YmirCoreBindingsAdapter(bindings);
      core.create();

      // On first launch write a default SMPC state file so the BIOS
      // Set Clock + Set Language wizard skips even on first boot.
      await SmpcStateService.ensureDefaults(_smpcStatePath);

      // Tell ymir-core where to read the SMPC persistent data file from.
      // MUST be called BEFORE LoadIPL so ymir-core reads it during boot
      // and the BIOS wizard auto-skips on subsequent launches.
      core.setPersistentSmpcPath(_smpcStatePath);

      // Set the SMPC RTC to the device's current clock BEFORE LoadIPL.
      // The BIOS Set Clock wizard then shows the right date+time so the
      // user can just press A to confirm and skip it on subsequent boots.
      core.setRtcToHost(offsetSeconds: 0);
      if (!mounted) return;
      setState(() => _core = core);
    } catch (e) {
      if (!mounted) return;
      setState(() => _loadError = e.toString());
    }
  }

  Future<void> _checkSetup() async {
    final completed = await AppPrefs.isSetupCompleted();
    if (!mounted) return;
    setState(() => _setupCompleted = completed);
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Ymir — Sega Saturn',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: _loadError != null
          ? _ErrorScreen(message: _loadError!)
          : (_core == null || _setupCompleted == null)
              ? const _LoadingScreen()
              : (_setupCompleted == false
                  ? SetupWizardScreen(
                      onComplete: () => setState(() => _setupCompleted = true),
                    )
                  : WorkbenchScreen(
                      core: _core!,
                      onRerunSetup: () =>
                          setState(() => _setupCompleted = false),
                    )),
    );
  }
}

class _LoadingScreen extends StatelessWidget {
  const _LoadingScreen();
  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      backgroundColor: Color(0xFF050607),
      body: Center(child: CircularProgressIndicator()),
    );
  }
}

class _ErrorScreen extends StatelessWidget {
  final String message;
  const _ErrorScreen({required this.message});
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(
            'Failed to load libymircore:\n$message',
            style: const TextStyle(color: Colors.redAccent),
            textAlign: TextAlign.center,
          ),
        ),
      ),
    );
  }
}