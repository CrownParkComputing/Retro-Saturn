// workbench_screen.dart — Main hub. Sidebar nav + content panel.
// Aligned with ViceMultiplatform's WorkbenchScreen so the two
// multiplatform shells (C64-Retro + ymir-android) read as sibling
// apps. See /home/jon/ViceMultiplatform/flutter_app/lib/screens/
// workbench_screen.dart for the reference; the shared patterns are:
//   - sidebar computed from the widest entry title (SaturnMetrics
//     + SaturnColors, mirrors ViceColors / ViceMetrics)
//   - core status footer (status / FPS / audio level) pinned to the
//     bottom of the sidebar
//   - content panel dispatched by WorkbenchCategory

import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/category.dart';
import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/ffi/ymir_core.dart';
import 'package:ymir_multiplatform/screens/about_screen.dart';
import 'package:ymir_multiplatform/screens/emulator_screen.dart';
import 'package:ymir_multiplatform/screens/history_screen.dart';
import 'package:ymir_multiplatform/screens/input_settings_screen.dart';
import 'package:ymir_multiplatform/screens/library_grid.dart';
import 'package:ymir_multiplatform/screens/logs_screen.dart';
import 'package:ymir_multiplatform/screens/paths_settings_screen.dart';
import 'package:ymir_multiplatform/screens/setup_wizard_screen.dart';
import 'package:ymir_multiplatform/services/app_prefs.dart';
import 'package:ymir_multiplatform/theme/saturn_theme.dart';

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
      case WorkbenchCategory.logs:
        return const LogsScreen();
      case WorkbenchCategory.about:
        return const AboutScreen();
    }
  }

  @override
  Widget build(BuildContext context) {
    final canLaunch = _biosPath.isNotEmpty && _gamesFolder.isNotEmpty;
    return Scaffold(
      backgroundColor: SaturnColors.rootBackground,
      body: !_pathsLoaded
          ? const Center(child: CircularProgressIndicator())
          : Row(children: [
              _Sidebar(
                selected: _category,
                onSelect: (c) => setState(() => _category = c),
                core: widget.core,
                canLaunch: canLaunch,
                onLaunch: _launchEmulator,
                onRerunSetup: () => Navigator.of(context).push(
                    MaterialPageRoute(
                        builder: (_) => SetupWizardScreen(onComplete: () {
                              _loadPaths();
                              Navigator.of(context).pop();
                            }))),
              ),
              const VerticalDivider(width: 1, color: SaturnColors.panelStroke),
              Expanded(child: _contentForCategory()),
            ]),
    );
  }
}

/// Sidebar nav matching the C64-Retro layout. Width computed from
/// widest title; clamped to SaturnMetrics.sidebarMinWidth/Max.
class _Sidebar extends StatelessWidget {
  final WorkbenchCategory selected;
  final ValueChanged<WorkbenchCategory> onSelect;
  final YmirCore core;
  final bool canLaunch;
  final VoidCallback onLaunch;
  final VoidCallback onRerunSetup;

  const _Sidebar({
    required this.selected,
    required this.onSelect,
    required this.core,
    required this.canLaunch,
    required this.onLaunch,
    required this.onRerunSetup,
  });

  @override
  Widget build(BuildContext context) {
    final scaler = MediaQuery.textScalerOf(context);
    final titleSize = scaler.scale(SaturnMetrics.sidebarButtonTextSize);
    final style = TextStyle(
      fontSize: titleSize,
      height: 1.15,
      color: SaturnColors.sidebarLabelIdle,
    );

    double widest = 0;
    for (final cat in WorkbenchCategory.values) {
      final painter = TextPainter(
        text: TextSpan(text: cat.title, style: style),
        textDirection: Directionality.of(context),
        maxLines: 1,
      )..layout();
      if (painter.width > widest) widest = painter.width;
    }

    const iconColumn = 22.0;
    const iconGap = 10.0;
    final hPadding = SaturnMetrics.sidebarButtonSidePadding * 2;
    final content = iconColumn + iconGap + widest;
    final screenWidth = MediaQuery.sizeOf(context).width;
    final railWidth = (content + hPadding + SaturnMetrics.sideNavPadding * 2)
        .clamp(SaturnMetrics.sidebarMinWidth,
            SaturnMetrics.sidebarMaxWidth(screenWidth));

    final rowHeight = (titleSize * 1.15 +
            SaturnMetrics.sidebarButtonVerticalPadding * 2)
        .clamp(SaturnMetrics.sidebarButtonHeight, 72.0);

    return Container(
      width: railWidth,
      padding: const EdgeInsets.all(SaturnMetrics.sideNavPadding),
      decoration: BoxDecoration(
        color: SaturnColors.panelFill,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: SaturnColors.panelStroke),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.symmetric(vertical: 4),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                for (final cat in WorkbenchCategory.values)
                  _SidebarItem(
                    icon: cat.icon,
                    title: cat.title,
                    selected: selected == cat,
                    onTap: () => onSelect(cat),
                    height: rowHeight,
                  ),
              ],
            ),
          ),
        ),
        const Divider(color: SaturnColors.panelStroke, height: 1),
        Padding(
          padding: const EdgeInsets.all(8),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text('Core',
                  style: TextStyle(
                      fontSize: 10,
                      color: SaturnColors.sectionLabel,
                      fontWeight: FontWeight.bold)),
              Text(core.status,
                  style: const TextStyle(fontSize: 11),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis),
              const SizedBox(height: 4),
              Text('FPS: ${core.fps}',
                  style: const TextStyle(fontSize: 11)),
              Text('Audio: ${core.audioLevel}/100',
                  style: const TextStyle(fontSize: 11)),
              if (canLaunch) ...[
                const SizedBox(height: 6),
                SizedBox(
                  width: double.infinity,
                  height: 28,
                  child: FilledButton.icon(
                    onPressed: onLaunch,
                    icon: const Icon(Icons.play_arrow, size: 14),
                    label: const Text('Launch',
                        style: TextStyle(fontSize: 11)),
                    style: FilledButton.styleFrom(
                      padding: EdgeInsets.zero,
                      minimumSize: const Size(0, 28),
                    ),
                  ),
                ),
              ],
              const SizedBox(height: 4),
              TextButton(
                onPressed: onRerunSetup,
                style: TextButton.styleFrom(
                  padding: EdgeInsets.zero,
                  minimumSize: const Size(0, 28),
                ),
                child: const Text('Re-run setup',
                    style: TextStyle(fontSize: 11)),
              ),
            ],
          ),
        ),
      ]),
    );
  }
}

class _SidebarItem extends StatelessWidget {
  final String icon;
  final String title;
  final bool selected;
  final VoidCallback onTap;
  final double height;
  const _SidebarItem({
    required this.icon,
    required this.title,
    required this.selected,
    required this.onTap,
    required this.height,
  });
  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Container(
        height: height,
        margin: const EdgeInsets.symmetric(vertical: 1),
        padding: const EdgeInsets.symmetric(
            horizontal: SaturnMetrics.sidebarButtonSidePadding),
        decoration: BoxDecoration(
          color: selected ? SaturnColors.selectedFill : Colors.transparent,
          borderRadius: BorderRadius.circular(4),
          border: selected
              ? Border.all(color: SaturnColors.selectedBorder, width: 1)
              : null,
        ),
        child: Row(children: [
          Text(icon, style: const TextStyle(fontSize: 14)),
          const SizedBox(width: 8),
          Text(title,
              style: TextStyle(
                  fontSize: 12,
                  color: selected
                      ? SaturnColors.sidebarLabelSelected
                      : SaturnColors.sidebarLabelIdle,
                  fontWeight:
                      selected ? FontWeight.w600 : FontWeight.normal)),
        ]),
      ),
    );
  }
}