// setup_wizard_screen.dart — Auto-scans the default Saturn folder
// for BIOS + game files. Like ViceMultiplatform's wizard: one screen,
// what was found is listed, user can pick a different folder or finish.
// Compact: no big headings, tight padding.

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/services/setup_scan_service.dart';
import 'package:retro_saturn/screens/compliance_screen.dart';

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
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text('No BIOS or games found.',
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
          const SizedBox(height: 4),
          Text(_folder,
              style: const TextStyle(fontSize: 11, color: Colors.white54),
              maxLines: 1, overflow: TextOverflow.ellipsis),
          const SizedBox(height: 12),
          // Spelled out step by step rather than as a path template. A Saturn
          // cannot start without its BIOS -- unlike the C64 and Amiga apps
          // there is no free reimplementation to fall back on -- so this
          // screen is the only thing standing between a new user, or a store
          // reviewer, and an app that appears to do nothing.
          const Text('To get started, import a Saturn BIOS:',
              style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600)),
          const SizedBox(height: 6),
          const _Step(1, 'Open the Files app.'),
          _Step(2, 'Go to $_filesLocation > Retro-Saturn.'),
          const _Step(3, 'Put your BIOS in the BIOS folder, named '
              'saturn_bios.bin (512 KiB).'),
          const _Step(4, 'Put CHD or CUE game images in the Games folder.'),
          const _Step(5, 'Come back here and tap Rescan.'),
          const SizedBox(height: 10),
          const Text(
              'The BIOS is Sega copyright and is not included. Supply one you '
              'own, exactly as with every other Saturn emulator.',
              style: TextStyle(fontSize: 11, color: Colors.white54)),
          const Spacer(),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              OutlinedButton.icon(
                onPressed: _busy ? null : _runFirstScan,
                icon: const Icon(Icons.refresh, size: 16),
                label: const Text('Rescan'),
              ),
              OutlinedButton.icon(
                onPressed: _busy ? null : _pickFolder,
                icon: const Icon(Icons.folder, size: 16),
                label: const Text('Pick different folder'),
              ),
              // The store-compliance page, reachable before anything has been
              // supplied. A reviewer with no BIOS otherwise has nothing to
              // read and nothing to do.
              TextButton.icon(
                onPressed: () => Navigator.of(context).push(
                    MaterialPageRoute<void>(
                        builder: (_) => Scaffold(
                              appBar: AppBar(
                                  title: const Text('Store compliance')),
                              body: const ComplianceScreen(),
                            ))),
                icon: const Icon(Icons.verified_outlined, size: 16),
                label: const Text('Store Compliance'),
              ),
            ],
          ),
        ]),
      );
    }

    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Icon(Icons.search, size: 14, color: Colors.white54),
          const SizedBox(width: 6),
          Expanded(
            child: Text(_folder,
                style: const TextStyle(fontSize: 11, color: Colors.white54),
                maxLines: 1, overflow: TextOverflow.ellipsis),
          ),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          _Chip(icon: Icons.memory, label: 'BIOS',
              count: r.biosCandidates.length),
          const SizedBox(width: 8),
          _Chip(icon: Icons.videogame_asset, label: 'Games',
              count: r.games.length),
        ]),
        const SizedBox(height: 8),
        Expanded(child: ListView(
          children: [
            if (r.biosCandidates.isNotEmpty) ...[
              const _SectionHeader(label: 'BIOS'),
              ...r.biosCandidates.map((p) => _FileRow(path: p)),
            ],
            if (r.games.isNotEmpty) ...[
              const _SectionHeader(label: 'Games'),
              ...r.games.take(40).map((g) => _FileRow(path: g.path)),
              if (r.games.length > 40)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: Text('+ ${r.games.length - 40} more',
                      style: const TextStyle(fontSize: 10, color: Colors.white38)),
                ),
            ],
          ],
        )),
      ]),
    );
  }

  Widget _buildActions(BuildContext context) {
    final r = _result;
    final canFinish = r != null && r.hasBios && r.hasGames;
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
      child: Row(children: [
        TextButton.icon(
          onPressed: _busy ? null : _pickFolder,
          icon: const Icon(Icons.folder, size: 14),
          label: const Text('Pick different'),
          style: TextButton.styleFrom(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            minimumSize: const Size(0, 32),
            tapTargetSize: MaterialTapTargetSize.shrinkWrap,
          ),
        ),
        const Spacer(),
        FilledButton(
          onPressed: canFinish ? _finish : null,
          style: FilledButton.styleFrom(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            minimumSize: const Size(0, 32),
          ),
          child: const Text('Finish'),
        ),
      ]),
    );
  }
}

class _Chip extends StatelessWidget {
  final IconData icon;
  final String label;
  final int count;
  const _Chip({required this.icon, required this.label, required this.count});
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1F2C),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0xFF2B3340)),
      ),
      child: Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(icon, size: 12, color: Colors.white54),
        const SizedBox(width: 4),
        Text('$label: $count',
            style: const TextStyle(fontSize: 11, color: Colors.white70)),
      ]),
    );
  }
}

class _SectionHeader extends StatelessWidget {
  final String label;
  const _SectionHeader({required this.label});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(0, 6, 0, 2),
      child: Text(label.toUpperCase(),
          style: const TextStyle(
              fontSize: 10,
              color: Colors.white38,
              fontWeight: FontWeight.bold,
              letterSpacing: 1.2)),
    );
  }
}

class _FileRow extends StatelessWidget {
  final String path;
  const _FileRow({required this.path});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Text(path.split('/').last,
          style: const TextStyle(fontSize: 12),
          maxLines: 1,
          overflow: TextOverflow.ellipsis),
    );
  }
}

/// Where the app's folder appears in the Files app. iPadOS reports itself as
/// iOS and dart:io cannot tell the two apart, so this says both rather than
/// naming the wrong one -- a heading the user cannot find is worse than a
/// slightly long sentence.
const String _filesLocation = 'On My iPhone / On My iPad';

/// One numbered instruction. Numbered because the order matters: a BIOS in the
/// Games folder, or a game before any BIOS, both end at the same empty screen.
class _Step extends StatelessWidget {
  const _Step(this.number, this.text);

  final int number;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          SizedBox(
            width: 18,
            child: Text('$number.',
                style: const TextStyle(fontSize: 12, color: Colors.white70)),
          ),
          Expanded(
            child: Text(text,
                style: const TextStyle(fontSize: 12, color: Colors.white70)),
          ),
        ],
      ),
    );
  }
}
