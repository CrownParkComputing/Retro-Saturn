import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import 'package:flutter/services.dart';

import '../data/media_entry.dart';
import '../ffi/ymir_bindings.dart';
import '../ffi/ymir_core.dart';
import '../services/app_prefs.dart';
import '../services/save_state_service.dart';
import '../theme/saturn_theme.dart';
import 'emulator_screen.dart';

/// How a session ended, so the workbench knows what to show next.
enum SessionExit {
  /// Snapshot written; offer the "Paused" banner / resume.
  paused,

  /// Ended; the automatic slot was refreshed but nothing else is offered.
  closed,
}

/// The Saturn, with the whole screen and its own controls.
///
/// This is the pattern the whole Retro family converged on after the Amiga
/// rework. The machine used to live inside the workbench, beside the rail,
/// with its controls in a status strip that auto-hid -- which put the way out
/// of a game in the one place a handheld's thumbs cannot reliably reach. Here
/// the picture owns the screen and the controls sit ON it:
///
///   * a corner button opens the PAUSE MENU: the machine stops, the picture
///     dims, and the choices are Resume, Save and exit, or Close;
///   * a labelled rail down the right edge carries the in-game tools --
///     labelled, because an icon-only control whose meaning is a guess ends
///     sessions by accident.
class EmulatorSessionScreen extends StatefulWidget {
  const EmulatorSessionScreen({
    super.key,
    required this.core,
    required this.biosPath,
    required this.gamesFolder,
    required this.entry,
    required this.saveStatePath,
    this.resumeStatePath,
  });

  final YmirCore core;
  final String? biosPath;
  final String? gamesFolder;
  final MediaEntry? entry;

  /// Where "Save and exit" puts its snapshot -- the workbench's session path,
  /// so the "Paused" banner and this screen agree on the file.
  final String saveStatePath;

  /// A state to restore once the disc is mounted, or null. Handed straight
  /// through to [EmulatorScreen], which knows why it must wait for loadDisc.
  final String? resumeStatePath;

  @override
  State<EmulatorSessionScreen> createState() => _EmulatorSessionScreenState();
}

class _EmulatorSessionScreenState extends State<EmulatorSessionScreen> {
  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();

  bool _padVisible = false;


  /// Stretch to fill, persisted like the Amiga and ST front ends.
  bool _fillScreen = AppPrefs.screenFill;

  /// Layout mode: the pad's clusters drag instead of press, and moves are
  /// remembered. Session-only state, like the rest of the family.
  bool _editingLayout = false;

  /// Controls shown, fading after a few seconds. The corner button is always
  /// reachable; this only governs the rail.
  bool _controlsVisible = true;
  Timer? _controlsTimer;

  /// The pause menu: machine stopped, picture dimmed, choices pinned up.
  bool _menuOpen = false;

  @override
  void initState() {
    super.initState();
    // The session owns the whole screen: hide the system bars for the
    // duration and give them back on the way out. Sticky, because an edge
    // swipe on a handheld is easy to do by accident mid-game.
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    // The core is shared with the launcher and stays paused between
    // sessions; entering one is what sets it running.
    widget.core.setPresentationPaused(false);
    AppPrefs.getShowPadDefault().then((bool v) {
      if (mounted && v) setState(() => _padVisible = true);
    });
    _findSiblingDiscs();
    _restartControlsTimer();
  }

  /// Other discs of the SAME game, for the rail's Disk tool. Multi-disc
  /// Saturn titles ship as "Title (Disc 1)", "Title (Disc 2)"...; strip
  /// the disc marker and everything in the folder that shares the stem is
  /// this game's set.
  List<String> _siblingDiscs = const [];

  static final RegExp _discMarker = RegExp(
      r'[\(\[]\s*(disc|disk|cd)\s*\d+[^\)\]]*[\)\]]',
      caseSensitive: false);

  static String _discStem(String path) => p
      .basenameWithoutExtension(path)
      .replaceAll(_discMarker, '')
      .replaceAll(RegExp(r'\s+'), ' ')
      .trim()
      .toLowerCase();

  void _findSiblingDiscs() {
    final MediaEntry? entry = widget.entry;
    if (entry == null) return;
    if (!_discMarker.hasMatch(p.basename(entry.path))) return;
    final stem = _discStem(entry.path);
    try {
      final dir = Directory(p.dirname(entry.path));
      final discs = <String>[];
      for (final f in dir.listSync(followLinks: false)) {
        if (f is! File) continue;
        final ext = p.extension(f.path).toLowerCase();
        if (!const {'.chd', '.cue', '.iso', '.mds', '.ccd'}.contains(ext)) {
          continue;
        }
        if (_discStem(f.path) == stem) discs.add(f.path);
      }
      discs.sort();
      if (discs.length > 1 && mounted) {
        setState(() => _siblingDiscs = discs);
      }
    } on FileSystemException {
      // A folder that cannot be listed just means no swap tool.
    }
  }

  /// Tapping a port socket: choose what is plugged into it. The chooser
  /// lists every Saturn peripheral with a one-line description, current
  /// one ticked.
  Future<void> _choosePeripheral(int port) async {
    final YmirPeripheralType current = widget.core.getPeripheralType(port);
    final YmirPeripheralType? chosen =
        await showModalBottomSheet<YmirPeripheralType>(
      context: context,
      backgroundColor: const Color(0xFF13161F),
      builder: (BuildContext context) => SafeArea(
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              ListTile(title: Text('Port $port peripheral')),
              const Divider(height: 1),
              for (final t in YmirPeripheralType.values)
                ListTile(
                  leading: Icon(_peripheralIcon(t),
                      color:
                          t == current ? const Color(0xFF3D8BFF) : null),
                  title: Text(_peripheralName(t) == 'nothing'
                      ? 'None'
                      : _peripheralName(t)),
                  subtitle: Text(
                    switch (t) {
                      YmirPeripheralType.none => 'No peripheral connected',
                      YmirPeripheralType.controlPad =>
                        'Standard 12-button Saturn pad',
                      YmirPeripheralType.analogPad =>
                        'Analog stick + L/R triggers',
                      YmirPeripheralType.arcadeRacer =>
                        'Wheel + face buttons',
                      YmirPeripheralType.missionStick =>
                        'Two analog sticks + throttle',
                      YmirPeripheralType.virtuaGun =>
                        'Light gun (touch overlay)',
                      YmirPeripheralType.shuttleMouse => '2-button mouse',
                    },
                    style: const TextStyle(
                        fontSize: 11, color: Colors.white54),
                  ),
                  trailing:
                      t == current ? const Icon(Icons.check, size: 18) : null,
                  onTap: () => Navigator.pop(context, t),
                ),
            ],
          ),
        ),
      ),
    );
    if (chosen == null || chosen == current || !mounted) return;
    widget.core.setPeripheralType(port, chosen);
    setState(() {}); // the socket icon reflects the new wiring
  }

  /// The rail's Disk tool: pick another disc of this game and put it in
  /// the drive -- the machine keeps running, exactly like swapping a CD.
  Future<void> _swapDisc() async {
    final String? chosen = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: const Color(0xFF13161F),
      builder: (BuildContext context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const ListTile(title: Text('Swap disc')),
            const Divider(height: 1),
            for (final d in _siblingDiscs)
              ListTile(
                leading: Icon(
                  d == widget.entry?.path
                      ? Icons.album
                      : Icons.album_outlined,
                ),
                title: Text(p.basename(d),
                    maxLines: 1, overflow: TextOverflow.ellipsis),
                onTap: () => Navigator.pop(context, d),
              ),
          ],
        ),
      ),
    );
    if (chosen == null || !mounted) return;
    final int rc = widget.core.loadDisc(chosen);
    if (rc != 0 && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Could not load that disc (error $rc).')),
      );
    }
  }

  @override
  void dispose() {
    _controlsTimer?.cancel();
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    super.dispose();
  }

  void _restartControlsTimer() {
    _controlsTimer?.cancel();
    _controlsTimer = Timer(const Duration(seconds: 6), () {
      if (mounted && !_menuOpen) setState(() => _controlsVisible = false);
    });
  }

  void _showControls() {
    if (_menuOpen) return;
    if (!_controlsVisible) setState(() => _controlsVisible = true);
    _restartControlsTimer();
  }

  void _toggleMenu() {
    final bool open = !_menuOpen;
    _controlsTimer?.cancel();
    widget.core.setPresentationPaused(open);
    setState(() {
      _menuOpen = open;
      _controlsVisible = true;
    });
    if (!open) _restartControlsTimer();
  }

  /// Save and exit: snapshot to the workbench's session path, then leave.
  Future<void> _saveAndExit() async {
    // Keep the machine frozen (the menu already paused it) -- the worker
    // serves the save request while paused.
    widget.core.setPresentationPaused(true);
    final int result = widget.core.saveState(widget.saveStatePath);
    if (!mounted) return;
    if (result != 0) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('Could not save your session (error $result).'),
      ));
      return; // stay: leaving anyway would silently lose their place
    }
    // The machine must actually STOP -- the core is shared with the
    // launcher, and leaving it running meant the game kept playing
    // (audio and all) behind the workbench. Presentation pause stops
    // both the emulation and the audio stream.
    widget.core.setPresentationPaused(true);
    Navigator.of(context).pop(SessionExit.paused);
  }

  /// Close: refresh the automatic slot -- leaving a game is the moment people
  /// most expect to be able to pick it up again -- then leave with nothing
  /// else offered.
  void _close() {
    // Pause FIRST: the machine must actually stop -- the core is shared
    // with the launcher, and leaving it running meant the game kept
    // playing (audio and all) behind the workbench. Presentation pause
    // stops both the emulation and the audio stream, and the worker
    // still serves the save request below while paused.
    widget.core.setPresentationPaused(true);
    final MediaEntry? leaving = widget.entry;
    if (leaving != null) {
      final snapshot = widget.core.framebuffer;
      unawaited(SaveStateService.save(widget.core, leaving, kAutoSlot,
          snapshot: snapshot));
    }
    Navigator.of(context).pop(SessionExit.closed);
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      // Back is a way out of the game, not a way to leave it running behind
      // the launcher with no picture and no controls.
      canPop: false,
      onPopInvokedWithResult: (bool didPop, Object? result) {
        if (!didPop) _close();
      },
      child: Scaffold(
        key: _scaffoldKey,
        backgroundColor: Colors.black,
        body: Stack(
          fit: StackFit.expand,
          children: <Widget>[
            Listener(
              behavior: HitTestBehavior.translucent,
              onPointerDown: (_) => _showControls(),
              child: EmulatorScreen(
                core: widget.core,
                biosPath: widget.biosPath,
                gamesFolder: widget.gamesFolder,
                entry: widget.entry,
                resumeStatePath: widget.resumeStatePath,
                showPadOverlay: _padVisible,
                fillScreen: _fillScreen,
                onMenuButton: _toggleMenu,
                editingLayout: _editingLayout,
              ),
            ),
            if (_menuOpen)
              Positioned.fill(
                child: GestureDetector(
                  onTap: _toggleMenu,
                  child: Container(
                    color: const Color(0x99000000),
                    alignment: Alignment.center,
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: <Widget>[
                        _ResumeButton(onPressed: _toggleMenu),
                        const SizedBox(height: 14),
                        // Occasional actions live HERE, not on the rail:
                        // the rail keeps only the tools touched mid-play.
                        _MenuChoice(
                          icon: _fillScreen
                              ? Icons.fit_screen
                              : Icons.aspect_ratio,
                          label: _fillScreen
                              ? 'Screen: 16:9 stretch'
                              : 'Screen: 4:3 shape',
                          detail: 'Tap to switch',
                          onPressed: () {
                            setState(() => _fillScreen = !_fillScreen);
                            AppPrefs.setScreenFill(_fillScreen);
                          },
                        ),
                        if (_siblingDiscs.length > 1) ...[
                          const SizedBox(height: 8),
                          _MenuChoice(
                            icon: Icons.album,
                            label: 'Swap disc',
                            detail:
                                'Put another disc of this game in the drive',
                            onPressed: () {
                              _toggleMenu();
                              unawaited(_swapDisc());
                            },
                          ),
                        ],
                        const SizedBox(height: 8),
                        _MenuChoice(
                          icon: Icons.bookmark_add_outlined,
                          label: 'Save and exit',
                          detail: 'Comes back from the Paused banner',
                          onPressed: _saveAndExit,
                        ),
                        const SizedBox(height: 8),
                        _MenuChoice(
                          icon: Icons.close,
                          label: 'Close without saving',
                          detail: 'The automatic slot still catches it',
                          onPressed: _close,
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            _rail(),
            _menuHandle(),
          ],
        ),
      ),
    );
  }

  /// One control, always in the same corner: a hamburger while the game runs,
  /// a play arrow while it is stopped.
  Widget _menuHandle() {
    return Align(
      alignment: Alignment.topLeft,
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(6),
          child: Opacity(
            opacity: _controlsVisible || _menuOpen ? 1 : 0.35,
            child: Material(
              color: _menuOpen
                  ? SaturnColors.tabSelected
                  : const Color(0xCC12151A),
              shape: const CircleBorder(),
              clipBehavior: Clip.antiAlias,
              child: InkWell(
                onTap: _toggleMenu,
                child: SizedBox(
                  width: 34,
                  height: 34,
                  child: Icon(
                    _menuOpen ? Icons.play_arrow : Icons.menu,
                    color: Colors.white,
                    size: 18,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _rail() {
    return AnimatedOpacity(
      opacity: _controlsVisible || _menuOpen ? 1 : 0,
      duration: const Duration(milliseconds: 200),
      child: IgnorePointer(
        ignoring: !_controlsVisible && !_menuOpen,
        child: Align(
          alignment: Alignment.centerRight,
          child: SafeArea(
            child: Padding(
              padding: const EdgeInsets.only(right: 8),
              child: Material(
                color: const Color(0xCC12151A),
                borderRadius: BorderRadius.circular(24),
                child: Padding(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 4, vertical: 8),
                  child: SingleChildScrollView(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: <Widget>[
                        _tool(
                          icon: Icons.videogame_asset,
                          label: 'Pad',
                          tip: _padVisible
                              ? 'Hide the on-screen pad'
                              : 'Show the on-screen pad',
                          active: _padVisible,
                          onPressed: () => setState(() {
                            _padVisible = !_padVisible;
                            if (!_padVisible) _editingLayout = false;
                          }),
                        ),
                        // Only while the pad is up: moving controls that
                        // are not on screen is a mode with nothing in it.
                        if (_padVisible)
                          _tool(
                            icon: _editingLayout
                                ? Icons.check
                                : Icons.open_with,
                            label: 'Layout',
                            tip: _editingLayout
                                ? 'Finish moving controls'
                                : 'Move the on-screen controls',
                            active: _editingLayout,
                            onPressed: () => setState(
                                () => _editingLayout = !_editingLayout),
                          ),
                        // The two sockets, each showing what is in it --
                        // the Amiga rail's model. Tapping a socket opens
                        // the chooser for THAT port.
                        _tool(
                          icon: _peripheralIcon(
                              widget.core.getPeripheralType(1)),
                          label: 'Port 1',
                          tip:
                              'Port 1: ${_peripheralName(widget.core.getPeripheralType(1))}'
                              ' — tap to change',
                          active: widget.core.getPeripheralType(1) !=
                              YmirPeripheralType.none,
                          onPressed: () => unawaited(_choosePeripheral(1)),
                        ),
                        _tool(
                          icon: _peripheralIcon(
                              widget.core.getPeripheralType(2)),
                          label: 'Port 2',
                          tip:
                              'Port 2: ${_peripheralName(widget.core.getPeripheralType(2))}'
                              ' — tap to change',
                          active: widget.core.getPeripheralType(2) !=
                              YmirPeripheralType.none,
                          onPressed: () => unawaited(_choosePeripheral(2)),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _tool({
    required IconData icon,
    required String label,
    required String tip,
    required VoidCallback onPressed,
    bool active = false,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Tooltip(
        message: tip,
        child: InkWell(
          borderRadius: BorderRadius.circular(10),
          onTap: () {
            _showControls();
            onPressed();
          },
          child: SizedBox(
            width: 52,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: <Widget>[
                Container(
                  width: 34,
                  height: 34,
                  decoration: BoxDecoration(
                    color: active
                        ? SaturnColors.tabSelected
                        : const Color(0xFF24292E),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(icon, color: Colors.white, size: 18),
                ),
                const SizedBox(height: 2),
                Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 9,
                    height: 1.1,
                    color: active ? Colors.white : Colors.white70,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

}

/// What each Saturn peripheral looks like on the rail's port sockets.
IconData _peripheralIcon(YmirPeripheralType t) => switch (t) {
      YmirPeripheralType.none => Icons.power_off,
      YmirPeripheralType.controlPad => Icons.videogame_asset,
      YmirPeripheralType.analogPad => Icons.sports_esports,
      YmirPeripheralType.arcadeRacer => Icons.sports_motorsports,
      YmirPeripheralType.missionStick => Icons.flight,
      YmirPeripheralType.virtuaGun => Icons.gps_fixed,
      YmirPeripheralType.shuttleMouse => Icons.mouse,
    };

String _peripheralName(YmirPeripheralType t) => switch (t) {
      YmirPeripheralType.none => 'nothing',
      YmirPeripheralType.controlPad => 'Control Pad',
      YmirPeripheralType.analogPad => '3D Control Pad',
      YmirPeripheralType.arcadeRacer => 'Arcade Racer',
      YmirPeripheralType.missionStick => 'Mission Stick',
      YmirPeripheralType.virtuaGun => 'Virtua Gun',
      YmirPeripheralType.shuttleMouse => 'Shuttle Mouse',
    };

/// The way back into the game, over the dimmed picture.
class _ResumeButton extends StatelessWidget {
  const _ResumeButton({required this.onPressed});

  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: const Color(0xF21B2030),
      borderRadius: BorderRadius.circular(28),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onPressed,
        child: const Padding(
          padding: EdgeInsets.symmetric(horizontal: 22, vertical: 14),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              Icon(Icons.play_arrow, color: Colors.white, size: 22),
              SizedBox(width: 8),
              Text('Resume',
                  style: TextStyle(
                      color: Colors.white,
                      fontSize: 15,
                      fontWeight: FontWeight.w600)),
              SizedBox(width: 10),
              Text('Paused',
                  style: TextStyle(color: Colors.white54, fontSize: 11)),
            ],
          ),
        ),
      ),
    );
  }
}

/// A secondary choice on the pause menu.
class _MenuChoice extends StatelessWidget {
  const _MenuChoice({
    required this.icon,
    required this.label,
    required this.detail,
    required this.onPressed,
  });

  final IconData icon;
  final String label;
  final String detail;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: const Color(0xCC12151A),
      borderRadius: BorderRadius.circular(20),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onPressed,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: <Widget>[
              Icon(icon, color: Colors.white70, size: 18),
              const SizedBox(width: 10),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: <Widget>[
                  Text(label,
                      style:
                          const TextStyle(color: Colors.white, fontSize: 13)),
                  Text(detail,
                      style: const TextStyle(
                          color: Colors.white38, fontSize: 10)),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
