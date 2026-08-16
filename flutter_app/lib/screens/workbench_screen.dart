// workbench_screen.dart — Main hub. Sidebar nav + content panel.
// Mirrors ViceMultiplatform's WorkbenchScreen: 🚀/🎮/📂/🕹️/📜/ℹ️
// categories on the left, content (library grid, settings, etc.) on
// the right.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/category.dart';
import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/screens/about_screen.dart';
import 'package:ymir_multiplatform/screens/emulator_screen.dart';
import 'package:ymir_multiplatform/screens/history_screen.dart';
import 'package:ymir_multiplatform/screens/input_settings_screen.dart';
import 'package:ymir_multiplatform/screens/library_grid.dart';
import 'package:ymir_multiplatform/screens/paths_settings_screen.dart';
import 'package:ymir_multiplatform/screens/setup_wizard_screen.dart';
import 'package:ymir_multiplatform/services/app_prefs.dart';

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

  Future<void> _launchEmulator() async {
    if (_biosPath.isEmpty || _gamesFolder.isEmpty) return;
    await Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => EmulatorScreen(
        core: widget.core,
        biosPath: _biosPath,
      ),
    ));
  }

  Widget _contentForCategory() {
    switch (_category) {
      case WorkbenchCategory.games:
        if (_gamesFolder.isEmpty) {
          return const Center(child: Text('Pick a games folder in 📂 Paths',
              style: TextStyle(color: Colors.white54)));
        }
        return LibraryGrid(
          folderPath: _gamesFolder,
          onLaunch: (entry) async {
            await HistoryService.record(entry.path, entry.displayName);
            if (!mounted) return;
            await Navigator.of(context).push(MaterialPageRoute(
              builder: (_) => EmulatorScreen(
                core: widget.core,
                biosPath: _biosPath,
                entry: entry,
              ),
            ));
          },
        );
      case WorkbenchCategory.paths:
        return const PathsSettingsScreen();
      case WorkbenchCategory.input:
        return InputSettingsScreen(core: widget.core);
      case WorkbenchCategory.history:
        return const HistoryScreen();
      case WorkbenchCategory.about:
        return const AboutScreen();
    }
  }

  @override
  Widget build(BuildContext context) {
    final canLaunch = _biosPath.isNotEmpty && _gamesFolder.isNotEmpty;
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(
        title: Text('Ymir — Sega Saturn'),
        actions: [
          IconButton(
            tooltip: 'Rerun setup',
            icon: const Icon(Icons.refresh),
            onPressed: () => Navigator.of(context).push(MaterialPageRoute(
              builder: (_) => SetupWizardScreen(onComplete: () {
                _loadPaths();
                Navigator.of(context).pop();
              }),
            )),
          ),
        ],
      ),
      body: !_pathsLoaded
          ? const Center(child: CircularProgressIndicator())
          : Row(children: [
              Container(
                width: 220,
                color: const Color(0xFF101113),
                child: ListView(padding: const EdgeInsets.all(4), children: [
                  for (final cat in WorkbenchCategory.values)
                    ListTile(
                      leading: Text(cat.icon, style: const TextStyle(fontSize: 18)),
                      title: Text(cat.title),
                      selected: _category == cat,
                      onTap: () => setState(() => _category = cat),
                      dense: true,
                    ),
                  const Divider(),
                  Padding(
                    padding: const EdgeInsets.all(8),
                    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                      const Text('Core', style: TextStyle(fontSize: 10, color: Colors.white54, fontWeight: FontWeight.bold)),
                      Text(widget.core.status,
                          style: const TextStyle(fontSize: 11), maxLines: 2, overflow: TextOverflow.ellipsis),
                      const SizedBox(height: 4),
                      Text('FPS: ${widget.core.fps}',
                          style: const TextStyle(fontSize: 11)),
                      Text('Audio: ${widget.core.audioLevel}/100',
                          style: const TextStyle(fontSize: 11)),
                    ]),
                  ),
                ]),
              ),
              const VerticalDivider(width: 1),
              Expanded(
                child: Column(children: [
                  if (_category == WorkbenchCategory.games)
                    Padding(
                      padding: const EdgeInsets.all(8),
                      child: Row(children: [
                        Expanded(
                          child: Text(
                              _gamesFolder.isEmpty ? '(no folder)' : _gamesFolder,
                              style: const TextStyle(fontSize: 11, color: Colors.white54),
                              overflow: TextOverflow.ellipsis),
                        ),
                        FilledButton.icon(
                          onPressed: canLaunch ? _launchEmulator : null,
                          icon: const Icon(Icons.play_arrow, size: 18),
                          label: const Text('Launch'),
                        ),
                      ]),
                    ),
                  Expanded(child: _contentForCategory()),
                ]),
              ),
            ]),
    );
  }
}