// workbench_screen.dart — Main hub. Sidebar nav + content panel.
// Compact: small sidebar, small appbar, tight padding.

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
          return const Center(
              child: Text('Pick a games folder in 📂 Paths',
                  style: TextStyle(color: Colors.white54)));
        }
        return LibraryGrid(
          folderPath: _gamesFolder,
          onLaunch: (entry) async {
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
      body: !_pathsLoaded
          ? const Center(child: CircularProgressIndicator())
          : Row(children: [
              Container(
                width: 180,
                color: const Color(0xFF101113),
                child: ListView(padding: const EdgeInsets.all(2), children: [
                  for (final cat in WorkbenchCategory.values)
                    _SidebarItem(
                      icon: cat.icon,
                      title: cat.title,
                      selected: _category == cat,
                      onTap: () => setState(() => _category = cat),
                    ),
                  const SizedBox(height: 4),
                  const Divider(color: Color(0xFF1A1F2C), height: 1),
                  Padding(
                    padding: const EdgeInsets.all(8),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        _StatLine(label: 'Status', value: widget.core.status),
                        _StatLine(label: 'FPS', value: '${widget.core.fps}'),
                        _StatLine(
                            label: 'Audio',
                            value: '${widget.core.audioLevel}/100'),
                        if (canLaunch)
                          Padding(
                            padding: const EdgeInsets.only(top: 6),
                            child: SizedBox(
                              width: double.infinity,
                              height: 28,
                              child: FilledButton.icon(
                                onPressed: _launchEmulator,
                                icon: const Icon(Icons.play_arrow, size: 14),
                                label: const Text('Launch',
                                    style: TextStyle(fontSize: 11)),
                                style: FilledButton.styleFrom(
                                  padding: EdgeInsets.zero,
                                  minimumSize: const Size(0, 28),
                                ),
                              ),
                            ),
                          ),
                      ],
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    child: TextButton(
                      onPressed: () => Navigator.of(context).push(
                          MaterialPageRoute(
                              builder: (_) => SetupWizardScreen(onComplete: () {
                                    _loadPaths();
                                    Navigator.of(context).pop();
                                  }))),
                      style: TextButton.styleFrom(
                        padding: EdgeInsets.zero,
                        minimumSize: const Size(0, 28),
                      ),
                      child: const Text('Re-run setup',
                          style: TextStyle(fontSize: 11)),
                    ),
                  ),
                ]),
              ),
              const VerticalDivider(width: 1, color: Color(0xFF1A1F2C)),
              Expanded(child: _contentForCategory()),
            ]),
    );
  }
}

class _SidebarItem extends StatelessWidget {
  final String icon;
  final String title;
  final bool selected;
  final VoidCallback onTap;
  const _SidebarItem({
    required this.icon,
    required this.title,
    required this.selected,
    required this.onTap,
  });
  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(
          color: selected ? const Color(0xFF1F2632) : null,
          borderRadius: BorderRadius.circular(4),
        ),
        child: Row(children: [
          Text(icon, style: const TextStyle(fontSize: 14)),
          const SizedBox(width: 8),
          Text(title,
              style: TextStyle(
                  fontSize: 12,
                  color: selected ? Colors.white : const Color(0xFFB9C2CE),
                  fontWeight: selected ? FontWeight.w600 : FontWeight.normal)),
        ]),
      ),
    );
  }
}

class _StatLine extends StatelessWidget {
  final String label;
  final String value;
  const _StatLine({required this.label, required this.value});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Row(children: [
        SizedBox(
            width: 46,
            child: Text(label,
                style: const TextStyle(fontSize: 10, color: Colors.white38))),
        Expanded(
          child: Text(value,
              style: const TextStyle(fontSize: 10, color: Colors.white70),
              maxLines: 1,
              overflow: TextOverflow.ellipsis),
        ),
      ]),
    );
  }
}