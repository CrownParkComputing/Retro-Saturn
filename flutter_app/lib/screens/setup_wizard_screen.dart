// setup_wizard_screen.dart — Auto-scans the default Saturn folder
// for BIOS + game files. Like ViceMultiplatform's wizard: one screen,
// what was found is listed, user can pick a different folder or finish.

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/data/media_entry.dart';
import 'package:ymir_multiplatform/services/app_prefs.dart';
import 'package:ymir_multiplatform/services/setup_scan_service.dart';

class SetupWizardScreen extends StatefulWidget {
  final VoidCallback onComplete;
  const SetupWizardScreen({super.key, required this.onComplete});

  @override
  State<SetupWizardScreen> createState() => _SetupWizardScreenState();
}

class _SetupWizardScreenState extends State<SetupWizardScreen> {
  bool _busy = false;
  bool _scanned = false;
  ScanResult? _result;
  String _folder = '';

  @override
  void initState() {
    super.initState();
    _runFirstScan();
  }

  Future<void> _runFirstScan() async {
    // Default = the auto-detected folder (e.g. /storage/FEDD-B1FF/Ymir),
    // or whatever the user previously picked.
    String? initial = await AppPrefs.getGamesFolder();
    initial ??= SetupScanService.autoDetectFolder();
    if (initial == null) {
      setState(() {
        _scanned = true;
        _folder = '(no folder selected)';
      });
      return;
    }
    await _scanFolder(initial);
  }

  Future<void> _scanFolder(String path) async {
    setState(() {
      _busy = true;
      _folder = path;
    });
    final r = await SetupScanService.scan(path);
    if (!mounted) return;
    setState(() {
      _result = r;
      _busy = false;
      _scanned = true;
    });
  }

  Future<void> _pickFolder() async {
    final p = await FilePicker.platform.getDirectoryPath();
    if (p != null) await _scanFolder(p);
  }

  Future<void> _finish() async {
    final r = _result;
    if (r != null && r.hasBios) {
      await AppPrefs.setBiosPath(r.biosCandidates.first);
    }
    if (r != null && r.hasGames) {
      await AppPrefs.setGamesFolder(r.folderPath);
    }
    await AppPrefs.setSetupCompleted(true);
    widget.onComplete();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      body: SafeArea(
        child: Column(children: [
          if (_busy) const LinearProgressIndicator(),
          Expanded(child: _buildBody()),
          if (_scanned && !_busy) _buildActions(context),
        ]),
      ),
    );
  }

  Widget _buildBody() {
    if (!_scanned) {
      return const Center(child: CircularProgressIndicator());
    }
    final r = _result;
    if (r == null || r.isEmpty) {
      return Padding(
        padding: const EdgeInsets.all(24),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text('Ymir — Sega Saturn',
              style: Theme.of(context).textTheme.headlineMedium),
          const SizedBox(height: 8),
          Text('Scanned: $_folder',
              style: const TextStyle(fontSize: 12, color: Colors.white54)),
          const Spacer(),
          const Text('No Saturn BIOS or game files found in this folder.',
              style: TextStyle(fontSize: 14)),
          const SizedBox(height: 8),
          const Text(
              'Layout expected: <folder>/BIOS/saturn_bios.bin (512 KiB) + '
              '<folder>/Games/<title>.chd',
              style: TextStyle(fontSize: 12, color: Colors.white54)),
          const Spacer(),
          OutlinedButton.icon(
            onPressed: _busy ? null : _pickFolder,
            icon: const Icon(Icons.folder),
            label: const Text('Pick a different folder'),
          ),
        ]),
      );
    }

    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('Ymir — Sega Saturn',
            style: Theme.of(context).textTheme.headlineMedium),
        const SizedBox(height: 4),
        Text('Scanned: $_folder',
            style: const TextStyle(fontSize: 11, color: Colors.white54)),
        const SizedBox(height: 16),
        Text('Found:',
            style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        _BiosSection(result: r),
        const SizedBox(height: 16),
        Expanded(child: SingleChildScrollView(child: _GamesSection(result: r))),
      ]),
    );
  }

  Widget _buildActions(BuildContext context) {
    final r = _result;
    final canFinish = r != null && r.hasBios && r.hasGames;
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 8, 24, 24),
      child: Row(children: [
        OutlinedButton.icon(
          onPressed: _busy ? null : _pickFolder,
          icon: const Icon(Icons.folder),
          label: const Text('Pick different folder'),
        ),
        const Spacer(),
        FilledButton(
          onPressed: canFinish ? _finish : null,
          child: const Text('Finish'),
        ),
      ]),
    );
  }
}

class _BiosSection extends StatelessWidget {
  final ScanResult result;
  const _BiosSection({required this.result});

  @override
  Widget build(BuildContext context) {
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Icon(Icons.memory, size: 18),
        const SizedBox(width: 8),
        Text('BIOS (${result.biosCandidates.length} found)',
            style: Theme.of(context).textTheme.titleSmall),
      ]),
      const SizedBox(height: 4),
      if (result.biosCandidates.isEmpty)
        const Text('  No 512 KiB *.bin found',
            style: TextStyle(color: Colors.redAccent, fontSize: 12))
      else
        ...result.biosCandidates.map((p) => Padding(
              padding: const EdgeInsets.only(left: 26, top: 2),
              child: Text('• ${p.split('/').last}',
                  style: const TextStyle(fontSize: 12)),
            )),
    ]);
  }
}

class _GamesSection extends StatelessWidget {
  final ScanResult result;
  const _GamesSection({required this.result});

  @override
  Widget build(BuildContext context) {
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        const Icon(Icons.videogame_asset, size: 18),
        const SizedBox(width: 8),
        Text('Games (${result.games.length} found)',
            style: Theme.of(context).textTheme.titleSmall),
      ]),
      const SizedBox(height: 4),
      if (result.games.isEmpty)
        const Text('  No CHD/CUE/ISO/MDS/CCD files found',
            style: TextStyle(color: Colors.redAccent, fontSize: 12))
      else
        ...result.games.take(20).map((g) => Padding(
              padding: const EdgeInsets.only(left: 26, top: 2),
              child: Text('• ${g.displayName}',
                  style: const TextStyle(fontSize: 12)),
            )),
    ]);
  }
}