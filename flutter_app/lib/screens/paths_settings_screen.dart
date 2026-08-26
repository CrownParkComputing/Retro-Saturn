// paths_settings_screen.dart — Edit the Saturn BIOS path + games folder
// + trigger an auto-scan + re-run the guided setup. Mirrors
// ViceMultiplatform's paths_settings_screen.dart pattern. The "Re-run
// setup" entry used to live in the sidebar footer; it is now here so
// the rail stays a launcher.

import 'package:retro_saturn/services/storage_permission.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:retro_saturn/screens/setup_wizard_screen.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/setup_scan_service.dart';

class PathsSettingsScreen extends StatefulWidget {
  const PathsSettingsScreen({super.key});

  @override
  State<PathsSettingsScreen> createState() => _PathsSettingsScreenState();
}

class _PathsSettingsScreenState extends State<PathsSettingsScreen> {
  String _biosPath = '';
  String _gamesFolder = '';
  ScanResult? _scan;

  @override
  void initState() {
    super.initState();
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
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _section('Saturn BIOS'),
        ListTile(
          leading: const Icon(Icons.memory),
          title: Text(
            _biosPath.isEmpty ? '(unset)' : _biosPath.split('/').last,
            style: const TextStyle(fontFamily: 'monospace'),
          ),
          subtitle: _biosPath.isEmpty
              ? null
              : Text(
                  _biosPath,
                  style: const TextStyle(fontSize: 10, color: Colors.white54),
                ),
          trailing: const Icon(Icons.edit),
          onTap: _pickBios,
        ),
        const SizedBox(height: 16),
        _section('Games folder'),
        ListTile(
          leading: const Icon(Icons.folder),
          title: Text(
            _gamesFolder.isEmpty ? '(unset)' : _gamesFolder.split('/').last,
            style: const TextStyle(fontFamily: 'monospace'),
          ),
          subtitle: _gamesFolder.isEmpty
              ? null
              : Text(
                  _gamesFolder,
                  style: const TextStyle(fontSize: 10, color: Colors.white54),
                ),
          trailing: const Icon(Icons.edit),
          onTap: _pickGames,
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            OutlinedButton.icon(
              onPressed: _gamesFolder.isEmpty ? null : _rescan,
              icon: const Icon(Icons.refresh),
              label: const Text('Re-scan folder'),
            ),
          ],
        ),
        const SizedBox(height: 24),
        _section('Library'),
      _ConfirmDeleteSwitch(),
      const SizedBox(height: 16),
      _section('Setup'),
        Card(
          margin: EdgeInsets.zero,
          child: ListTile(
            leading: const Icon(Icons.replay),
            title: const Text('Re-run setup wizard'),
            subtitle: const Text(
              'Re-pick the BIOS, games folder and core setup from scratch. '
              'Useful after switching devices or restoring an Android backup.',
              style: TextStyle(fontSize: 11, color: Colors.white54),
            ),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => Navigator.of(context).push(
              MaterialPageRoute(
                builder: (_) => SetupWizardScreen(
                  onComplete: () {
                    _load();
                    Navigator.of(context).pop();
                  },
                ),
              ),
            ),
          ),
        ),
        const SizedBox(height: 24),
        if (_scan != null) _scanSummary(),
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


/// "Confirm before deleting files" -- guards the library's long-press
/// Delete. On by default.
class _ConfirmDeleteSwitch extends StatefulWidget {
  @override
  State<_ConfirmDeleteSwitch> createState() => _ConfirmDeleteSwitchState();
}

class _ConfirmDeleteSwitchState extends State<_ConfirmDeleteSwitch> {
  bool _confirm = true;

  @override
  void initState() {
    super.initState();
    AppPrefs.getConfirmDelete().then((v) {
      if (mounted) setState(() => _confirm = v);
    });
  }

  @override
  Widget build(BuildContext context) {
    return SwitchListTile(
      title: const Text('Confirm before deleting files'),
      subtitle: const Text(
          "Guards the library's long-press Delete. Off means one tap "
          'deletes the file from disk.',
          style: TextStyle(fontSize: 11, color: Colors.white54)),
      value: _confirm,
      onChanged: (v) {
        AppPrefs.setConfirmDelete(v);
        setState(() => _confirm = v);
      },
    );
  }
}
