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

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:retro_saturn/data/category.dart';
import 'package:retro_saturn/data/media_entry.dart';
import 'package:retro_saturn/data/peripheral_type.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/screens/about_screen.dart';
import 'package:retro_saturn/screens/audio_settings_screen.dart';
import 'package:retro_saturn/screens/emulator_screen.dart';
import 'package:retro_saturn/screens/history_screen.dart';
import 'package:retro_saturn/screens/input_settings_screen.dart';
import 'package:retro_saturn/screens/library_grid.dart';
import 'package:retro_saturn/screens/paths_settings_screen.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/ymir_core_paths.dart';
import 'package:retro_saturn/theme/saturn_theme.dart';
import 'package:retro_saturn/widgets/peripheral_selector.dart';
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
  bool _pathsLoaded = false;

  /// The game the user tapped to launch. Held so the in-panel EmulatorScreen
  /// can call `loadDisc(entry.path)` -- the disc never loads if this is null
  /// (EmulatorScreen falls back to BIOS-only, which boots to the Saturn's
  /// CD Player "No Disc" screen). Set in the LibraryGrid `onLaunch` callback,
  /// cleared in `_onSessionExit`. Independent of [_pausedSession] so the X
  /// (kill) and Pause (snapshot) buttons leave the Dart side in
  /// distinguishable states.
  MediaEntry? _currentEntry;

  /// On-screen Saturn pad overlay toggle. Lifted out of EmulatorScreen so
  /// the toolbar in [_statusBar] (this screen's bottom row) and the
  /// EmulatorScreen's overlay render see the same source of truth. Reset
  /// when a session ends.
  bool _padVisible = false;

  /// Scaffold key so the in-game Settings button can open the drawer that
  /// lives on the workbench's Scaffold (EmulatorScreen no longer owns a
  /// Scaffold of its own).
  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();

  /// True when the emulator screen is on top of the workbench.
  bool _inEmulator = false;

  /// Whether the in-game chrome is on screen. It hides itself a few seconds
  /// after the last touch: a Saturn frame on a widescreen handheld has no
  /// height to lend to furniture that is only occasionally wanted.
  bool _chromeVisible = true;
  Timer? _chromeTimer;

  /// Whether the system bars are currently hidden. Tracked so the mode is set
  /// once per transition rather than on every rebuild.
  bool _immersive = false;

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

  Future<void> _loadPaths() async {
    final b = await AppPrefs.getBiosPath() ?? '';
    final g = await AppPrefs.getGamesFolder() ?? '';
    if (!mounted) return;
    setState(() {
      _biosPath = b;
      _gamesFolder = g;
      _pathsLoaded = true;
    });
  }

  /// Toolbar Pause on the emulator screen. Snapshots the running machine
  /// via [YmirCore.saveState] then drops back to the workbench so a fresh
  /// library grid + a "Paused: <title>" banner are visible.
  Future<void> _onSessionPause() async {
    final result = widget.core.saveState(_saveStatePath);
    if (!mounted) return;
    setState(() {
      _inEmulator = false;
      _padVisible = false;
      // The library is not a fullscreen game.
      _sidebarHidden = false;
      // Snapshot the title so the resume banner has something to label and
      // the resume handler can re-enter the emulator screen on top.
      _pausedSession = _currentEntry;
    });
    _stopRuntimeTicker();
    if (result != 0) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Could not save your session (error $result).'),
      ));
    }
  }

  /// Toolbar X on the emulator screen. Closes the running session and
  /// returns to the workbench with no resume snapshot. The core stays
  /// alive because [YmirCore] is shared with the launcher (see
  /// _RetroSaturnAppState.dispose); calling dispose() here would break
  /// the bare-launcher mode.
  void _onSessionExit() {
    setState(() {
      _inEmulator = false;
      _padVisible = false;
      _sidebarHidden = false;
      _pausedSession = null;
      _currentEntry = null;
    });
    _stopRuntimeTicker();
  }

  /// Tap on the "Paused: <title>" banner. Restores the snapshot the
  /// Pause handler wrote, brings the emulator screen back.
  Future<void> _onResumePaused() async {
    final result = widget.core.loadState(_saveStatePath);
    if (!mounted) return;
    if (result != 0) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Could not restore your session (error $result).'),
      ));
      return;
    }
    setState(() {
      _pausedSession = null;
      _inEmulator = true;
      _sidebarHidden = true;
    });
    _startRuntimeTicker();
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
        if (_gamesFolder.isEmpty) {
          return const Center(
              child: Text('Pick a games folder in 📂 Paths',
                  style: TextStyle(color: Colors.white54)));
        }
        return LibraryGrid(
          folderPath: _gamesFolder,
          onLaunch: (entry) async {
            if (!mounted) return;
            setState(() {
              // Store the entry so the in-panel EmulatorScreen can
              // loadDisc() it. Without this, the BIOS boots to the CD
              // Player's "No Disc" screen.
              _currentEntry = entry;
              _inEmulator = true;
              // A launched title goes fullscreen; the rail comes back from
              // the floating hamburger, or when the session ends.
              _sidebarHidden = true;
            });
            _startRuntimeTicker();
          },
        );
      case WorkbenchCategory.paths:
        return const PathsSettingsScreen();
      case WorkbenchCategory.audio:
        return AudioSettingsScreen(core: widget.core);
      case WorkbenchCategory.input:
        return InputSettingsScreen(core: widget.core);
      case WorkbenchCategory.history:
        return const HistoryScreen();
      case WorkbenchCategory.about:
        return const AboutScreen();
    }
  }

  /// Shows the in-game chrome and restarts its countdown.
  ///
  /// Driven from a Listener that does not consume the event, so the tap that
  /// brings the toolbar back is still the tap the game receives -- otherwise
  /// finding the controls would cost you a shot.
  void _pokeChrome() {
    _chromeTimer?.cancel();
    if (!_chromeVisible) setState(() => _chromeVisible = true);
    _chromeTimer = Timer(const Duration(seconds: 3), () {
      if (mounted) setState(() => _chromeVisible = false);
    });
  }

  /// Hides the system bars while a machine is on screen, and gives them back
  /// when it is not. Sticky, because an edge swipe on a handheld is easy to
  /// do by accident mid-game.
  void _syncImmersive(bool wanted) {
    if (wanted == _immersive) return;
    _immersive = wanted;
    SystemChrome.setEnabledSystemUIMode(
      wanted ? SystemUiMode.immersiveSticky : SystemUiMode.edgeToEdge,
    );
    if (wanted) {
      // Visible on arrival, then gone: the player has to see the controls
      // exist before they can learn that a tap brings them back.
      _pokeChrome();
    } else {
      _chromeTimer?.cancel();
      _chromeVisible = true;
    }
  }

  @override
  Widget build(BuildContext context) {
    // A running machine takes the whole screen, and it is the EXISTING
    // hide-the-rail switch that says so rather than a second one of its own.
    // Launching hides the rail, the floating status row's hamburger brings it
    // back along with the panel, and hiding it again returns to fullscreen --
    // the game running throughout.
    final fullscreen = _inEmulator && _sidebarHidden;
    WidgetsBinding.instance
        .addPostFrameCallback((_) => _syncImmersive(fullscreen));
    if (fullscreen) return _fullscreenSession();

    return Scaffold(
      key: _scaffoldKey,
      backgroundColor: SaturnColors.rootBackground,
      drawer: _buildDrawer(context),
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
  /// hamburger toggle on the left, the paused-session title (when one
  /// exists) in the middle, and the in-game toolbar on the right -- matching
  /// Retro-C64's EmulatorControlStrip pattern. It always renders, even with
  /// the rail hidden, because the hamburger is the only way back once the
  /// rail is gone.
  ///
  /// The in-game toolbar buttons (pad toggle, settings, pause, close) only
  /// show while a session is running. Paused / no-session hides them so the
  /// bar is just the launcher chrome.
  ///
  /// The session title is a tap target for the paused-session case: tapping
  /// it loads the snapshot and brings the emulator back.
  ///
  /// [floating] is the fullscreen case, where this row is drawn over the foot
  /// of the picture rather than below it. Everything about it is the same
  /// except that touching it restarts the countdown that hides it -- or the
  /// row would fade out from under the thumb reaching for it.
  Widget _statusBar({bool floating = false}) {
    final paused = _pausedSession;
    return Row(
      children: [
        IconButton(
          onPressed: () {
            if (floating) _pokeChrome();
            setState(() => _sidebarHidden = !_sidebarHidden);
          },
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
              : _inEmulator
                  ? _runtimeStrip()
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
        if (_inEmulator) ..._inGameToolbar(floating: floating),
      ],
    );
  }

  /// The middle of the in-emulator status bar: title + FPS + audio meter.
  /// Replaces the sidebar footer's Core/FPS/Audio block, which used to be
  /// reachable without a game running (and so mostly sat empty). The
  /// render lives here, where it has meaning only when something is
  /// playing; the values re-poll twice a second via [_runtimeTicker].
  /// A Timer for the poll is enough -- the values move slowly enough
  /// that driving them off the framebuffer's per-frame rebuild would
  /// be overkill.
  Widget _runtimeStrip() {
    final core = widget.core;
    return Row(children: [
      Flexible(
        child: Text(
          _currentEntry?.displayName ?? '',
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(
              fontSize: 12, color: SaturnColors.sidebarLabelIdle),
        ),
      ),
      const SizedBox(width: 8),
      Text('FPS ${core.fps}',
          style: const TextStyle(
              fontSize: 11, color: SaturnColors.sidebarLabelIdle)),
      const SizedBox(width: 8),
      SizedBox(
        width: 48,
        height: 8,
        child: ClipRRect(
          borderRadius: BorderRadius.circular(2),
          child: Stack(children: [
            Container(color: Colors.white12),
            FractionallySizedBox(
              widthFactor: (core.audioLevel.clamp(0, 100)) / 100.0,
              heightFactor: 1,
              child: Container(
                color: core.audioMuted
                    ? Colors.redAccent
                    : const Color(0xFF60A0FF),
              ),
            ),
          ]),
        ),
      ),
      if (core.audioMuted) ...[
        const SizedBox(width: 4),
        const Icon(Icons.volume_off,
            size: 12, color: SaturnColors.sidebarLabelIdle),
      ],
    ]);
  }

  /// Twice-a-second refresh for the runtime strip. Started when a
  /// session opens, stopped when it ends. FPS and audio level don't
  /// move fast enough to justify a per-frame poll, and the framebuffer
  /// view's own rebuild storm would burn battery for no UI benefit.
  Timer? _runtimeTicker;

  void _startRuntimeTicker() {
    _runtimeTicker?.cancel();
    _runtimeTicker = Timer.periodic(
      const Duration(milliseconds: 500),
      (_) {
        if (mounted) setState(() {}); // _runtimeStrip reads core live
      },
    );
  }

  void _stopRuntimeTicker() {
    _runtimeTicker?.cancel();
    _runtimeTicker = null;
  }

  @override
  void dispose() {
    _runtimeTicker?.cancel();
    super.dispose();
  }

  /// Pad toggle / settings / pause / close, right-aligned in the status bar.
  /// Lives outside the emulator screen so chrome is drawn under the picture,
  /// not on it -- a 4:3 frame status panel is exactly where these buttons
  /// would be covering game UI otherwise.
  List<Widget> _inGameToolbar({bool floating = false}) {
    // Using the toolbar has to count as interaction, or it would be the one
    // thing you can do that does not keep the toolbar on screen.
    void act(VoidCallback action) {
      if (floating) _pokeChrome();
      action();
    }

    return [
      IconButton(
        tooltip: _padVisible
            ? 'Hide on-screen pad'
            : 'Show on-screen pad',
        icon: Icon(
          _padVisible ? Icons.gamepad : Icons.gamepad_outlined,
          size: 18,
        ),
        color: SaturnColors.sidebarLabelIdle,
        onPressed: () => act(() => setState(() => _padVisible = !_padVisible)),
        visualDensity: VisualDensity.compact,
        padding: EdgeInsets.zero,
      ),
      const SizedBox(width: 6),
      IconButton(
        tooltip: 'Settings',
        icon: const Icon(Icons.settings, size: 18),
        color: SaturnColors.sidebarLabelIdle,
        onPressed: () => act(() => _scaffoldKey.currentState?.openDrawer()),
        visualDensity: VisualDensity.compact,
        padding: EdgeInsets.zero,
      ),
      const SizedBox(width: 6),
      IconButton(
        tooltip: 'Pause and return to library (snapshot saved)',
        icon: const Icon(Icons.pause, size: 18),
        color: SaturnColors.sidebarLabelIdle,
        onPressed: () => act(_onSessionPause),
        visualDensity: VisualDensity.compact,
        padding: EdgeInsets.zero,
      ),
      const SizedBox(width: 6),
      IconButton(
        tooltip: 'Close game (kills the core)',
        icon: const Icon(Icons.close, size: 18),
        color: SaturnColors.sidebarLabelIdle,
        onPressed: () => act(_onSessionExit),
        visualDensity: VisualDensity.compact,
        padding: EdgeInsets.zero,
      ),
    ];
  }

  /// The in-game settings drawer. Lives on the workbench Scaffold so the
  /// toolbar's Settings button can open it; EmulatorScreen no longer owns a
  /// Scaffold of its own.
  Widget _buildDrawer(BuildContext context) {
    return Drawer(
      child: SafeArea(
        child: ListView(padding: const EdgeInsets.all(16), children: [
          Text('Settings', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 16),
          PeripheralSelector(core: widget.core, port: 1),
          const SizedBox(height: 12),
          PeripheralSelector(core: widget.core, port: 2),
          const SizedBox(height: 16),
          Text('Bridge status', style: Theme.of(context).textTheme.titleSmall),
          Text(widget.core.status),
          Text('FPS: ${widget.core.fps}'),
          Text('Audio: ${widget.core.audioLevel}/100'),
          Text('Muted: ${widget.core.audioMuted}'),
          Text('Port 1: ${widget.core.getPeripheralType(1).displayName}'),
          Text('Port 2: ${widget.core.getPeripheralType(2).displayName}'),
          if (_currentEntry != null)
            Text('NVRAM: auto-save every 60s'),
        ]),
      ),
    );
  }

  /// The right-hand pane. Renders the live emulator when a session is
  /// running, the resumable-session card when paused, or the current
  /// category content otherwise. When a session is running the sidebar stays
  /// visible by default ("big window" mode -- emulator renders in the right
  /// pane alongside the categories); the user can collapse it via the
  /// hamburger in [_statusBar] for more room. The emulator is no longer a
  /// push-modal route -- it is embedded, matching Retro-C64 and Retro-Dosbox.
  Widget _contentPanel() {
    if (_inEmulator) {
      return _emulatorPanel();
    }
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

  /// The in-emulator panel. Extracted so [_contentPanel] and the
  /// Resume destination can both render the same embedded framebuffer
  /// view -- the rail's auto-resume on the Resume entry flips
  /// _inEmulator true, and the user then sees this same panel rather
  /// than a separate "resumed" screen.
  /// The running machine, edge to edge, with the status row floating over the
  /// foot of the picture.
  ///
  /// It is the status row ITSELF that floats, not a second copy of half of
  /// it: that row already carries the hamburger, the title, the FPS and audio
  /// meters and the in-game toolbar, so reusing it keeps the fullscreen
  /// chrome and the windowed chrome the same chrome, with one hamburger that
  /// has one behaviour.
  ///
  /// Bottom rather than top, matching where the row sits everywhere else in
  /// this app and in the Amiga, C64 and DOSBox front ends.
  Widget _fullscreenSession() {
    return Scaffold(
      key: _scaffoldKey,
      backgroundColor: Colors.black,
      drawer: _buildDrawer(context),
      body: Listener(
        // Translucent, and onPointerDown rather than a tap: this observes the
        // touch without eating it, so the game still gets it.
        behavior: HitTestBehavior.translucent,
        onPointerDown: (_) => _pokeChrome(),
        child: Stack(
          children: [
            Positioned.fill(child: _emulatorPanel(bare: true)),
            Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              child: AnimatedOpacity(
                opacity: _chromeVisible ? 1 : 0,
                duration: const Duration(milliseconds: 220),
                // Faded-out chrome must not still be catching taps meant for
                // the game underneath it.
                child: IgnorePointer(
                  ignoring: !_chromeVisible,
                  child: DecoratedBox(
                    // Legible over whatever the game is drawing, fading
                    // upwards so it does not read as a bar bolted across the
                    // picture.
                    decoration: const BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.bottomCenter,
                        end: Alignment.topCenter,
                        colors: [Colors.black87, Colors.transparent],
                      ),
                    ),
                    child: SafeArea(
                      top: false,
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 8),
                        child: _statusBar(floating: true),
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// [bare] drops the panel's padding, fill and border: fullscreen has no
  /// panel to be inside, and a border drawn at the edge of the display is a
  /// frame around nothing.
  Widget _emulatorPanel({bool bare = false}) {
    return Container(
      padding: EdgeInsets.all(bare ? 0 : 8),
      decoration: bare
          ? null
          : BoxDecoration(
              color: SaturnColors.panelFill,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: SaturnColors.panelStroke),
            ),
      child: EmulatorScreen(
        core: widget.core,
        biosPath: _biosPath,
        gamesFolder: _gamesFolder,
        entry: _currentEntry,
        showPadOverlay: _padVisible,
      ),
    );
  }

  /// "Paused: <title>" banner above the workbench. Same role as
  /// Retro-Dosbox's _resumableBanner and Retro-C64's Running-tab card:
  /// surfaces the resumable session even when the user has navigated
  /// somewhere other than Running. Saturn's accent is the same blue the
  /// sidebar selection uses (SaturnColors.tabSelected), so a paused state
  /// reads as "still the app you were in", not as a foreign warning.
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

  /// The Core status block that used to be baked into the rail's own class.
  /// It lives in the canonical Sidebar's footer slot now -- which is exactly
}

/// Sidebar nav matching the C64-Retro layout. Width computed from
/// widest title; clamped to SaturnMetrics.sidebarMinWidth/Max.
