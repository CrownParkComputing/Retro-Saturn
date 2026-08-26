// main.dart — Retro-Saturn (formerly Ymir Multiplatform) app entry. Loads
// creates the YmirCore, restores SMPC state + backup RAM, then routes
// to SetupWizardScreen or WorkbenchScreen based on whether setup is
// complete. Mirrors ViceMultiplatform's setup pattern.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/ffi/ymir_native_paths.dart';
import 'package:retro_saturn/screens/setup_wizard_screen.dart';
import 'package:retro_saturn/screens/workbench_screen.dart';
import 'package:retro_saturn/services/app_log.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/backup_ram_service.dart';
import 'package:retro_saturn/services/core_options_service.dart';
import 'package:retro_saturn/services/core_pause_coordinator.dart';
import 'package:retro_saturn/services/smpc_state_service.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';
import 'package:package_info_plus/package_info_plus.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await YmirCorePaths.ensureDirs();
  await AppPrefs.load();
  await BackupRamService.ensureInit();
  await AppLog.init();
  AppLog.log('app start');
  runApp(const RetroSaturnApp());
}

class RetroSaturnApp extends StatefulWidget {
  const RetroSaturnApp({super.key});

  @override
  State<RetroSaturnApp> createState() => _RetroSaturnAppState();
}

class _RetroSaturnAppState extends State<RetroSaturnApp> with WidgetsBindingObserver {
  YmirCore? _core;
  String? _loadError;
  bool? _setupCompleted;

  final CorePauseCoordinator _pauseCoordinator = CorePauseCoordinator();


  /// Saturn BIOS SMPC persistent state — restored on launch to skip the
  /// Set Clock / Set Language wizard when the user has completed it.
  /// Resolved from [YmirCorePaths.smpcStatePath] so the path is the
  /// same on Android, iOS and Linux.
  String get _smpcStatePath => YmirCorePaths.smpcStatePath;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _loadCore();
    _checkSetup();
  }

  /// Saves what is worth keeping and tears the core down, once.
  ///
  /// Idempotent because both dispose() and a detached lifecycle callback can
  /// reach it, and destroying twice would be a use-after-free rather than a
  /// no-op.
  void _shutdownCore() {
    final core = _core;
    if (core == null) return;
    _core = null;
    try {
      core.saveSmpcState(_smpcStatePath);
    } on Object catch (e) {
      // Losing the clock/language state is not a reason to skip the teardown
      // below, which is the part that stops a thread.
      debugPrint('saturn: SMPC state not saved on shutdown ($e)');
    }
    BackupRamService.stopAutoSave();
    core.dispose();
  }

  @override
  void dispose() {
    _shutdownCore();
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    super.didChangeAppLifecycleState(state);

    // detached is the app being destroyed, and it is the ONLY reliable moment
    // to shut the core down.
    //
    // State.dispose() on the root widget is not called when iOS terminates an
    // app, so the emulator's worker thread was still running as the process
    // went away -- which is a crash at exit, after everything the user did had
    // worked. It cost a crash report per session and nothing else, which is
    // why it looked like the emulator was killing the app when the emulator
    // was fine.
    //
    // ymir_bridge_destroy stops the worker and joins it, so doing this here
    // means the thread is gone before the runtime is.
    if (state == AppLifecycleState.detached) {
      _shutdownCore();
      return;
    }

    // Everything else is CorePauseCoordinator's decision -- see that file for
    // why "pause unless resumed" is wrong in both directions.
    final core = _core;
    _pauseCoordinator.onLifecycle(
        state, core == null ? null : _CorePauseAdapter(core));
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

      // The user's core options, before anything boots. Applying them after a
      // disc had loaded would mean the first seconds of every session ran on
      // ymir-core's defaults instead of the chosen settings.
      await CoreOptionsService.load();
      CoreOptionsService.applyAll(core);

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
    // Version-keyed: a new build re-triggers the wizard so the store
    // review team and any new user always see the latest BIOS-folder
    // contract on first launch. See AppPrefs.isSetupCompletedFor().
    final version = await _currentAppVersion();
    final completed = await AppPrefs.isSetupCompletedFor(version);
    if (!mounted) return;
    setState(() => _setupCompleted = completed);
  }

  /// Build+version of the running app, used as the key for "did this user
  /// finish the setup wizard for THIS build?". Falls back to a sentinel
  /// on read failure so the wizard at least runs once -- a failing
  /// PackageInfo lookup should not brick the first-run experience.
  static Future<String> _currentAppVersion() async {
    try {
      final info = await PackageInfo.fromPlatform();
      return '${info.version}+${info.buildNumber}';
    } catch (_) {
      return 'unknown';
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Retro-Saturn',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: _loadError != null
          ? _ErrorScreen(message: _loadError!)
          : (_core == null || _setupCompleted == null)
              ? const _LoadingScreen()
              : (_setupCompleted == false
                  ? SetupWizardScreen(
                      // At-launch setup is either a first run (no folder,
                      // so the walkthrough shows anyway) or a new build
                      // re-check -- verify, don't re-teach.
                      verifyOnly: true,
                      onComplete: () async {
                        // Stamp the wizard as done FOR THIS BUILD. A new
                        // version will set this back to false; the same
                        // build keeps it on.
                        final v = await _currentAppVersion();
                        await AppPrefs.setSetupCompletedFor(v);
                        if (!mounted) return;
                        setState(() => _setupCompleted = true);
                      },
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

/// Bridges the concrete core to the narrow interface the pause coordinator
/// needs, so that logic has no dependency on dart:ffi and can be tested.
class _CorePauseAdapter implements PausableCore {
  final YmirCore _core;
  _CorePauseAdapter(this._core);

  @override
  bool get presentationPaused => _core.presentationPaused;

  @override
  void setPresentationPaused(bool paused) => _core.setPresentationPaused(paused);
}
