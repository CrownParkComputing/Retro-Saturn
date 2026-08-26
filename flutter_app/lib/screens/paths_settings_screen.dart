// paths_settings_screen.dart — Edit the Saturn BIOS path + games folder
// + trigger an auto-scan + re-run the guided setup. Mirrors
// ViceMultiplatform's paths_settings_screen.dart pattern. The "Re-run
// setup" entry used to live in the sidebar footer; it is now here so
// the rail stays a launcher.

import 'package:retro_saturn/services/storage_permission.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:retro_saturn/screens/setup_wizard_screen.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/setup_scan_service.dart';

class PathsSettingsScreen extends StatefulWidget {
  /// For the audio mute switch, folded in here from the old A/V page.
  final YmirCore core;

  const PathsSettingsScreen({super.key, required this.core});

  @override
  State<PathsSettingsScreen> createState() => _PathsSettingsScreenState();
}

class _PathsSettingsScreenState extends State<PathsSettingsScreen> {
  String _biosPath = '';
  String _gamesFolder = '';
  ScanResult? _scan;

  bool _fill = AppPrefs.screenFill;
  bool _padDefault = false;
  bool _muted = false;
  bool _confirmDelete = true;

  @override
  void initState() {
    super.initState();
    _muted = widget.core.audioMuted;
    AppPrefs.getShowPadDefault().then((v) {
      if (mounted) setState(() => _padDefault = v);
    });
    AppPrefs.getConfirmDelete().then((v) {
      if (mounted) setState(() => _confirmDelete = v);
    });
    _load();
  }

  Future<void> _load() async {
    final b = await AppPrefs.getBiosPath() ?? '';
    final g = await AppPrefs.getGamesFolder() ?? '';
    if (!mounted) return;
    setState(() {
      _biosPath = b;
      _gamesFolder = g;
    });
    if (g.isNotEmpty) await _rescan();
  }

  Future<void> _pickBios() async {
    final r = await FilePicker.platform.pickFiles(type: FileType.any);
    if (r != null && r.files.single.path != null) {
      final p = r.files.single.path!;
      await AppPrefs.setBiosPath(p);
      if (!mounted) return;
      setState(() => _biosPath = p);
    }
  }

  Future<void> _pickGames() async {
    // The system picker will happily hand back an SD-card path the app
    // then cannot read; ask for the access first.
    if (!await StoragePermission.ensure()) return;
    final p = await FilePicker.platform.getDirectoryPath();
    if (p != null) {
      await AppPrefs.setGamesFolder(p);
      if (!mounted) return;
      setState(() => _gamesFolder = p);
      await _rescan();
    }
  }

  Future<void> _rescan() async {
    if (_gamesFolder.isEmpty) return;
    final r = await SetupScanService.scan(_gamesFolder);
    if (!mounted) return;
    setState(() => _scan = r);
  }

  @override
  Widget build(BuildContext context) {
    // Everything on ONE page, no scrolling: the folders and setup on the
    // left, the display/audio/library switches on the right. The old A/V
    // tab is folded in here; Input went entirely -- the ports are wired
    // from the in-game rail now.
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(child: _foldersColumn()),
          const SizedBox(width: 24),
          Expanded(child: _switchesColumn()),
        ],
      ),
    );
  }

  Widget _foldersColumn() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _section('Saturn BIOS'),
        ListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          leading: const Icon(Icons.memory, size: 20),
          title: Text(
            _biosPath.isEmpty ? '(unset)' : _biosPath.split('/').last,
            style: const TextStyle(fontFamily: 'monospace', fontSize: 13),
          ),
          subtitle: _biosPath.isEmpty
              ? null
              : Text(_biosPath,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style:
                      const TextStyle(fontSize: 10, color: Colors.white54)),
          trailing: const Icon(Icons.edit, size: 18),
          onTap: _pickBios,
        ),
        const SizedBox(height: 8),
        _section('Games folder'),
        ListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          leading: const Icon(Icons.folder, size: 20),
          title: Text(
            _gamesFolder.isEmpty ? '(unset)' : _gamesFolder.split('/').last,
            style: const TextStyle(fontFamily: 'monospace', fontSize: 13),
          ),
          subtitle: _gamesFolder.isEmpty
              ? null
              : Text(_gamesFolder,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style:
                      const TextStyle(fontSize: 10, color: Colors.white54)),
          trailing: const Icon(Icons.edit, size: 18),
          onTap: _pickGames,
        ),
        const SizedBox(height: 12),
        Row(children: [
          OutlinedButton.icon(
            onPressed: _gamesFolder.isEmpty ? null : _rescan,
            icon: const Icon(Icons.refresh, size: 16),
            label: const Text('Re-scan'),
          ),
          const SizedBox(width: 8),
          OutlinedButton.icon(
            onPressed: () => Navigator.of(context).push(
              MaterialPageRoute(
                builder: (_) => SetupWizardScreen(
                  onComplete: () {
                    _load();
                    Navigator.of(context).pop();
                  },
                ),
              ),
            ),
            icon: const Icon(Icons.replay, size: 16),
            label: const Text('Re-run setup wizard'),
          ),
        ]),
        const SizedBox(height: 12),
        if (_scan != null) _scanSummary(),
      ],
    );
  }

  Widget _switchesColumn() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _section('Screen'),
        SwitchListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          title: const Text('Stretch to fill (16:9)'),
          subtitle: const Text('The in-game Fill tool changes this too.',
              style: TextStyle(fontSize: 11, color: Colors.white54)),
          value: _fill,
          onChanged: (v) {
            AppPrefs.setScreenFill(v);
            setState(() => _fill = v);
          },
        ),
        SwitchListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          title: const Text('Start sessions with the on-screen pad'),
          subtitle: const Text('The in-game Pad tool toggles it mid-game.',
              style: TextStyle(fontSize: 11, color: Colors.white54)),
          value: _padDefault,
          onChanged: (v) {
            AppPrefs.setShowPadDefault(v);
            setState(() => _padDefault = v);
          },
        ),
        const SizedBox(height: 8),
        _section('Audio'),
        SwitchListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          title: const Text('Mute'),
          subtitle: const Text(
              'The core keeps running; un-muting snaps back to now.',
              style: TextStyle(fontSize: 11, color: Colors.white54)),
          value: _muted,
          onChanged: (v) {
            widget.core.setAudioMuted(v);
            setState(() => _muted = v);
          },
        ),
        const SizedBox(height: 8),
        _section('Library'),
        SwitchListTile(
          dense: true,
          contentPadding: EdgeInsets.zero,
          title: const Text('Confirm before deleting files'),
          subtitle: const Text("Guards the library's long-press Delete.",
              style: TextStyle(fontSize: 11, color: Colors.white54)),
          value: _confirmDelete,
          onChanged: (v) {
            AppPrefs.setConfirmDelete(v);
            setState(() => _confirmDelete = v);
          },
        ),
      ],
    );
  }

  Widget _section(String label) => Padding(
    padding: const EdgeInsets.only(bottom: 4),
    child: Text(
      label,
      style: const TextStyle(
        fontSize: 11,
        color: Colors.white54,
        fontWeight: FontWeight.bold,
      ),
    ),
  );

  Widget _scanSummary() {
    final s = _scan!;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Last scan: ${s.folderPath.split('/').last}',
              style: const TextStyle(fontSize: 11, color: Colors.white54),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                const Icon(Icons.memory, size: 16),
                const SizedBox(width: 6),
                Text(
                  '${s.biosCandidates.length} BIOS',
                  style: const TextStyle(fontSize: 13),
                ),
                const SizedBox(width: 16),
                const Icon(Icons.videogame_asset, size: 16),
                const SizedBox(width: 6),
                Text(
                  '${s.games.length} games',
                  style: const TextStyle(fontSize: 13),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
