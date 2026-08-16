// workbench_screen.dart — Main hub. Sidebar nav + library grid +
// emulator screen route. Mirrors ViceMultiplatform's WorkbenchScreen.

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/screens/emulator_screen.dart';
import 'package:ymir_multiplatform/screens/library_grid.dart';
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
  String _biosPath = '';
  String _gamesFolder = '';
  bool _loaded = false;

  @override
  void initState() {
    super.initState();
    _loadPaths();
  }

  Future<void> _loadPaths() async {
    final b = await AppPrefs.getBiosPath();
    final g = await AppPrefs.getGamesFolder();
    if (!mounted) return;
    setState(() {
      _biosPath = b ?? '';
      _gamesFolder = g ?? '';
      _loaded = true;
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

  @override
  Widget build(BuildContext context) {
    final canLaunch = _biosPath.isNotEmpty && _gamesFolder.isNotEmpty;
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(
        title: const Text('Ymir — Sega Saturn'),
        actions: [
          IconButton(
            tooltip: 'Rerun setup',
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.of(context).push(MaterialPageRoute(
              builder: (_) => SetupWizardScreen(onComplete: () {
                _loadPaths();
                Navigator.of(context).pop();
              }),
            )),
          ),
        ],
      ),
      body: !_loaded
          ? const Center(child: CircularProgressIndicator())
          : Row(children: [
              Container(
                width: 220,
                color: const Color(0xFF101113),
                child: ListView(padding: const EdgeInsets.all(12), children: [
                  const ListTile(
                    leading: Icon(Icons.videogame_asset),
                    title: Text('Library'),
                    selected: true,
                  ),
                  const Divider(),
                  const Padding(
                    padding: EdgeInsets.symmetric(vertical: 8),
                    child: Text('Status',
                        style: TextStyle(fontWeight: FontWeight.bold)),
                  ),
                  Text('BIOS: ${_biosPath.isEmpty ? "(unset)" : _biosPath.split("/").last}',
                      style: const TextStyle(fontSize: 11)),
                  Text('Games: ${_gamesFolder.isEmpty ? "(unset)" : _gamesFolder.split("/").last}',
                      style: const TextStyle(fontSize: 11)),
                  const SizedBox(height: 12),
                  Text('Core: ${widget.core.status}',
                      style: const TextStyle(fontSize: 11)),
                  const SizedBox(height: 12),
                  FilledButton.icon(
                    onPressed: canLaunch ? _launchEmulator : null,
                    icon: const Icon(Icons.play_arrow),
                    label: const Text('Launch emulator'),
                  ),
                ]),
              ),
              const VerticalDivider(width: 1),
              Expanded(
                child: canLaunch
                    ? LibraryGrid(
                        folderPath: _gamesFolder,
                        onLaunch: (entry) => Navigator.of(context).push(
                            MaterialPageRoute(
                                builder: (_) => EmulatorScreen(
                                      core: widget.core,
                                      biosPath: _biosPath,
                                      entry: entry,
                                    ))),
                      )
                    : const Center(
                        child: Padding(
                          padding: EdgeInsets.all(24),
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(Icons.settings, size: 48, color: Colors.white24),
                              SizedBox(height: 16),
                              Text('Setup required', style: TextStyle(fontSize: 18)),
                              SizedBox(height: 8),
                              Text(
                                  'Tap the gear icon to pick your Saturn BIOS and games folder.',
                                  textAlign: TextAlign.center),
                            ],
                          ),
                        ),
                      ),
              ),
            ]),
    );
  }
}