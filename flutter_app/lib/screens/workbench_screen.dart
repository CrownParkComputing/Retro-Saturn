// workbench_screen.dart — Main hub. Sidebar nav + content panel.
// Aligned with ViceMultiplatform's WorkbenchScreen so the two
// multiplatform shells (C64-Retro + ymir-android) read as sibling
// apps. Shared with Retro-C64, Retro-Amiga (uae4arm2026p) and
// Retro-Dosbox at the widgets/sidebar.dart level: the Sidebar is
// bytes-identical, the WorkbenchCategory enum is the per-app
// declaration of which destinations the rail exposes, and the
// status bar across the bottom is the same row of rail-toggle +
// session title + (when running) in-game toolbar.
//
// The runtime info that used to live in the sidebar footer (Core
// status, FPS, audio level) now lives in [_statusBar]'s middle slot
// while a session is running -- the rail stays a launcher, the
// bottom strip becomes the in-game status strip.

import 'dart:io';
import 'dart:async';

import 'package:path/path.dart' as p;
import 'package:flutter/material.dart';
import 'package:retro_saturn/services/app_log.dart';
import 'package:retro_saturn/data/category.dart';
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/screens/about_screen.dart';
import 'package:retro_saturn/screens/audio_settings_screen.dart';
import 'package:retro_saturn/screens/compliance_screen.dart';
import 'package:retro_saturn/screens/emulator_session_screen.dart';
import 'package:retro_saturn/screens/history_screen.dart';
import 'package:retro_saturn/screens/core_options_screen.dart';
import 'package:retro_saturn/screens/input_settings_screen.dart';
import 'package:retro_saturn/screens/save_states_screen.dart';
import 'package:retro_saturn/services/save_state_service.dart';
import 'package:retro_saturn/screens/library_grid.dart';
import 'package:retro_saturn/screens/paths_settings_screen.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';
import 'package:retro_saturn/theme/saturn_theme.dart';
import 'package:retro_saturn/widgets/sidebar.dart';
import 'package:retro_saturn/widgets/sidebar_style.dart';

class WorkbenchScreen extends StatefulWidget {
  final YmirCore core;
  final VoidCallback? onRerunSetup;

  const WorkbenchScreen({super.key, required this.core, this.onRerunSetup});

  @override
  State<WorkbenchScreen> createState() => _WorkbenchScreenState();
}

class _WorkbenchScreenState extends State<WorkbenchScreen> {
  WorkbenchCategory _category = WorkbenchCategory.games;
  String _biosPath = '';
  String _gamesFolder = '';

  /// The game the user tapped to launch. Held so the in-panel EmulatorScreen
  /// can call `loadDisc(entry.path)` -- the disc never loads if this is null
  /// (EmulatorScreen falls back to BIOS-only, which boots to the Saturn's
  /// CD Player "No Disc" screen). Set in the LibraryGrid `onLaunch` callback,
  /// cleared in `_onSessionExit`. Independent of [_pausedSession] so the X
  /// (kill) and Pause (snapshot) buttons leave the Dart side in
  /// distinguishable states.
  MediaEntry? _currentEntry;

  /// Save state the next EmulatorScreen should restore once its disc is
  /// mounted. Set by the States view, cleared as soon as it is handed over so
  /// a later plain launch of the same game starts from the BIOS.
  /// Whether the side rail is collapsed. The launcher button + the running
  /// tab live in the rail; collapsing it gives the emulator screen as much
  /// room as the device allows, which is the whole point of the
  /// collapsible-sidebar pattern Retro-C64 and Retro-Dosbox use. The
  /// hamburger in [_statusBar] toggles it back; the status bar itself
  /// never collapses, because the only way back from a fully-hidden
  /// rail would otherwise be the X button on the emulator toolbar.
  bool _sidebarHidden = false;

  /// The title that has been paused via the toolbar Pause button. Distinct
  /// from [_currentEntry] so the X (kill) and Pause (snapshot) buttons
  /// leave the Dart side in distinguishable states. A non-null value here
  /// shows the resume banner above the workbench content and blocks fresh
  /// launches until the user either resumes or discards.
  MediaEntry? _pausedSession;

  /// Snapshot file written by [YmirCore.saveState] on Pause. Resolved
  /// from [YmirCorePaths.saveStatePath] so the path is the same on
  /// Android, iOS and Linux. The SMPC state file lives in the same
  /// per-platform app-data dir; this is its sibling.
  String get _saveStatePath => YmirCorePaths.saveStatePath;

  @override
  void initState() {
    super.initState();
    _loadPaths();
  }

  /// Store-compliance mode: the library is not scanned or shown. Read
  /// here so the Games tab can honour it, and re-read when the switch on
  /// the Compliance page changes it.
  bool _complianceMode = false;

  Future<void> _loadPaths() async {
    _complianceMode = await AppPrefs.getComplianceMode();
    var b = await AppPrefs.getBiosPath() ?? '';
    final g = await AppPrefs.getGamesFolder() ?? '';
    // The BIOS pref can go stale -- the card gets reorganised, or moves to
    // another device -- and a machine booted with no BIOS is just a black
    // screen with nothing to say why. If the stored file is gone, look
    // where the family convention keeps it: a BIOS/ folder next to the
    // games folder. Found one, keep it; the repair is visible in Paths.
    if ((b.isEmpty || !File(b).existsSync()) && g.isNotEmpty) {
      final repaired = _findBiosNear(g);
      if (repaired != null) {
        b = repaired;
        await AppPrefs.setBiosPath(repaired);
        AppLog.log('bios path repaired: $repaired');
      }
    }
    if (!mounted) return;
    setState(() {
      _biosPath = b;
      _gamesFolder = g;
    });
  }

  /// A plausible Saturn BIOS in the BIOS/ folder beside [gamesFolder], or
  /// beside its parent. 512 KiB is the Saturn BIOS size; the name is not
  /// trusted because dumps are named every way imaginable.
  static String? _findBiosNear(String gamesFolder) {
    for (final dir in <String>[
      p.join(p.dirname(gamesFolder), 'BIOS'),
      p.join(gamesFolder, 'BIOS'),
    ]) {
      final d = Directory(dir);
      if (!d.existsSync()) continue;
      try {
        for (final f in d.listSync(followLinks: false)) {
          if (f is! File) continue;
          if (!f.path.toLowerCase().endsWith('.bin')) continue;
          if (f.lengthSync() == 524288) return f.path;
        }
      } on FileSystemException {
        continue;
      }
    }
    return null;
  }

  /// Tap on a card in the States view. The disc has to be mounted before the
  /// state can be restored, so this launches the game and hands the state to
  /// the emulator screen to apply once loadDisc has run.
  void _onResumeSlot(MediaEntry entry, int slot) {
    unawaited(_openSession(entry,
        resumePath: SaveStateService.statePathFor(entry, slot)));
  }

  /// Hands the session its own screen -- the family pattern from the Amiga
  /// rework. Every way into a game funnels through here, so pausing and
  /// closing land back on the workbench in exactly one place.
  Future<void> _openSession(MediaEntry entry, {String? resumePath}) async {
    if (!mounted) return;
    setState(() => _currentEntry = entry);
    final SessionExit? how = await Navigator.of(context).push<SessionExit>(
      MaterialPageRoute<SessionExit>(
        fullscreenDialog: true,
        builder: (BuildContext context) => EmulatorSessionScreen(
          core: widget.core,
          biosPath: _biosPath,
          gamesFolder: _gamesFolder,
          entry: entry,
          saveStatePath: _saveStatePath,
          resumeStatePath: resumePath,
        ),
      ),
    );
    if (!mounted) return;
    setState(() {
      _pausedSession = how == SessionExit.paused ? entry : null;
      if (how != SessionExit.paused) _currentEntry = null;
    });
  }

  /// Tap on the "Paused: <title>" banner. Restores the snapshot the
  /// Pause handler wrote, brings the emulator screen back.
  Future<void> _onResumePaused() async {
    final entry = _pausedSession;
    if (entry == null) return;
    final result = widget.core.loadState(_saveStatePath);
    if (!mounted) return;
    if (result != 0) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Could not restore your session (error $result).'),
      ));
      return;
    }
    setState(() => _pausedSession = null);
    unawaited(_openSession(entry, resumePath: null));
  }

  /// Drop the paused snapshot without resuming. The file stays on disk
  /// (next launch overwrites it) but the workbench no longer offers
  /// resume. Equivalent in spirit to Retro-C64's "Discard" button.
  void _discardPaused() {
    setState(() => _pausedSession = null);
  }

  Widget _contentForCategory() {
    switch (_category) {
      case WorkbenchCategory.games:
        if (_complianceMode) {
          return Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Icon(Icons.verified_outlined,
                    size: 40, color: Colors.white38),
                const SizedBox(height: 12),
                const Padding(
                  padding: EdgeInsets.symmetric(horizontal: 32),
                  child: Text(
                    'Store compliance mode is on: the app is using no user '
                    'content, and your games library is not scanned or '
                    'shown.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.white54, height: 1.4),
                  ),
                ),
                const SizedBox(height: 16),
                OutlinedButton(
                  onPressed: () => setState(
                      () => _category = WorkbenchCategory.compliance),
                  child: const Text('Open Compliance to switch it off'),
                ),
              ],
            ),
          );
        }
        if (_gamesFolder.isEmpty) {
          return const Center(
              child: Text('Pick a games folder in 📂 Paths',
                  style: TextStyle(color: Colors.white54)));
        }
        return LibraryGrid(
          folderPath: _gamesFolder,
          onLaunch: (entry) => _openSession(entry, resumePath: null),
        );
      case WorkbenchCategory.states:
        return SaveStatesScreen(
          gamesFolder: _gamesFolder,
          onResume: _onResumeSlot,
        );
      case WorkbenchCategory.paths:
        return const PathsSettingsScreen();
      case WorkbenchCategory.audio:
        return AudioSettingsScreen(core: widget.core);
      case WorkbenchCategory.input:
        return InputSettingsScreen(core: widget.core);
      case WorkbenchCategory.core:
        return CoreOptionsScreen(core: widget.core);
      case WorkbenchCategory.history:
        return const HistoryScreen();
      case WorkbenchCategory.compliance:
        return ComplianceScreen(
          onRerunSetup: widget.onRerunSetup,
          onModeChanged: () => unawaited(_loadPaths()),
        );
      case WorkbenchCategory.about:
        return const AboutScreen();
    }
  }

  @override
  Widget build(BuildContext context) {
    // The session has its own screen now (EmulatorSessionScreen); the
    // workbench is only ever the launcher.
    return Scaffold(
      backgroundColor: SaturnColors.rootBackground,
      body: Container(
        color: SaturnColors.rootBackground,
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(8),
            child: Column(
              children: [
                Expanded(
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      if (!_sidebarHidden) ...[
                        Sidebar(
                          destinations: [
                            for (final c in WorkbenchCategory.values)
                              SidebarDestination(
                                c.title,
                                icon: c.icon,
                                group: c.group,
                              ),
                          ],
                          selectedIndex: _category.index,
                          onSelected: (i) => setState(
                              () => _category = WorkbenchCategory.values[i]),
                          style: saturnSidebarStyle,
                          pinLastGroupToBottom: true,
                        ),
                        const SizedBox(width: 8),
                      ],
                      Expanded(child: _contentPanel()),
                    ],
                  ),
                ),
                _statusBar(),
              ],
            ),
          ),
        ),
      ),
    );
  }

  /// The bottom strip, outside both the sidebar and the content panel: the
  /// hamburger toggle on the left and the paused-session title (when one
  /// exists) in the middle. Sessions run on their own screen now, so this
  /// is launcher chrome only.
  ///
  /// The session title is a tap target for the paused-session case: tapping
  /// it loads the snapshot and brings the emulator back.
  Widget _statusBar() {
    final paused = _pausedSession;
    return Row(
      children: [
        IconButton(
          onPressed: () => setState(() => _sidebarHidden = !_sidebarHidden),
          icon: Icon(
            _sidebarHidden ? Icons.menu : Icons.menu_open,
            size: 20,
          ),
          color: SaturnColors.sidebarLabelIdle,
          tooltip: _sidebarHidden ? 'Show sidebar' : 'Hide sidebar',
          visualDensity: VisualDensity.compact,
        ),
        const SizedBox(width: 6),
        Expanded(
          child: paused != null
              ? GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: _onResumePaused,
                  child: Row(children: [
                    const Icon(Icons.history,
                        size: 14, color: SaturnColors.sidebarLabelIdle),
                    const SizedBox(width: 6),
                    Flexible(
                      child: Text(
                        'Paused -- ${paused.displayName} (tap to resume)',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                            fontSize: 12,
                            color: SaturnColors.sidebarLabelIdle),
                      ),
                    ),
                  ]),
                )
              : _currentEntry != null
                      ? Text(
                          _currentEntry!.displayName,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(
                              fontSize: 12,
                              color: SaturnColors.sidebarLabelIdle),
                        )
                      : const SizedBox.shrink(),
        ),
      ],
    );
  }

  Widget _contentPanel() {
    return Container(
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: SaturnColors.panelFill,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: SaturnColors.panelStroke),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (_pausedSession != null) _resumableBanner(),
          Expanded(child: _contentForCategory()),
        ],
      ),
    );
  }

  /// "Paused: <title>" banner above the workbench content: the way back
  /// into a session left with Save-and-exit. Same role as Retro-Dosbox's
  /// banner and Retro-C64's Resume screen.
  Widget _resumableBanner() {
    final entry = _pausedSession!;
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      decoration: BoxDecoration(
        color: SaturnColors.tabSelected.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: SaturnColors.tabSelected),
      ),
      child: Row(children: [
        const Icon(Icons.history, size: 16, color: SaturnColors.tabSelected),
        const SizedBox(width: 8),
        Expanded(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: _onResumePaused,
            child: Text(
              'Paused: ${entry.displayName} -- tap to resume',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                  color: SaturnColors.tabSelected, fontSize: 12),
            ),
          ),
        ),
        IconButton(
          onPressed: _discardPaused,
          icon: const Icon(Icons.close, size: 16),
          color: SaturnColors.tabSelected,
          visualDensity: VisualDensity.compact,
        ),
      ]),
    );
  }

}

/// Sidebar nav matching the C64-Retro layout. Width computed from
/// widest title; clamped to SaturnMetrics.sidebarMinWidth/Max.
