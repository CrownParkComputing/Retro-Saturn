// setup_wizard_screen.dart — First-run setup, the Retro-Amiga way: a phased
// walkthrough rather than one dense screen. Welcome (what did I just open),
// two teaching pages (what a Saturn needs, where files can live on THIS
// platform), then the choice, then the scan with the folder named, then the
// results — and only then a Finish button.

import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:path_provider/path_provider.dart';
import 'package:retro_saturn/data/friendly_path.dart';
import 'package:retro_saturn/services/setup_scan_service.dart';
import 'package:retro_saturn/screens/compliance_screen.dart';
import 'package:retro_saturn/screens/getting_started.dart';
import 'package:retro_saturn/services/storage_permission.dart';

class SetupWizardScreen extends StatefulWidget {
  final VoidCallback onComplete;

  /// Skip the walkthrough and go straight to re-checking the folder
  /// already set. Used when a new build re-triggers setup at launch --
  /// the questions have been answered, and asking them again would
  /// suggest the answers had been lost. Someone who ASKS for setup from
  /// Paths gets the full walkthrough, exactly like Retro-Amiga.
  final bool verifyOnly;

  const SetupWizardScreen({
    super.key,
    required this.onComplete,
    this.verifyOnly = false,
  });

  @override
  State<SetupWizardScreen> createState() => _SetupWizardScreenState();
}

/// Where the walkthrough is up to. See Retro-Amiga's onboarding for why the
/// phases exist: the old single screen showed the results, the picker and
/// Finish all at once, before anything had said what the app was about to
/// ask for.
enum _Phase { welcome, primer, gate, scanning, results }

class _SetupWizardScreenState extends State<SetupWizardScreen> {
  _Phase _phase = _Phase.welcome;
  bool _busy = false;
  bool _scanned = false;
  String? _notice;

  /// The scan folder as a Files-app location. The real path stays in [_folder]
  /// because the scanner needs it; this is the only thing shown. A container
  /// path is somewhere the user cannot go, and printing it also put the build
  /// machine's directory layout -- account name and all -- into a screenshot
  /// of this screen.
  String _folderShown = '';
  ScanResult? _result;

  @override
  void initState() {
    super.initState();
    _maybeResumeExistingSetup();
  }

  /// On a verify-only run (new build), a folder already chosen skips the
  /// teaching and goes straight to re-checking it. An explicit re-run from
  /// Paths starts at the welcome page like a first meeting.
  Future<void> _maybeResumeExistingSetup() async {
    if (!widget.verifyOnly) return;
    final String? existing = await AppPrefs.getGamesFolder();
    if (existing == null || !mounted) return;
    await _scanFolder(existing);
  }

  Future<void> _runFirstScan() async {
    String? initial = await AppPrefs.getGamesFolder();
    initial ??= await SetupScanService.autoDetectFolderAsync();
    if (initial == null) {
      setState(() {
        _phase = _Phase.results;
        _scanned = true;
        _folderShown = '(no folder selected)';
      });
      return;
    }
    await _scanFolder(initial);
  }

  Future<void> _scanFolder(String path) async {
    // Disc images are read in place, so the scan needs the same access the
    // emulator will: ask BEFORE walking, because a scan that silently finds
    // nothing reads as "the app is broken", not "it was never allowed to
    // look".
    if (!await StoragePermission.ensure()) {
      if (!mounted) return;
      setState(() {
        _phase = _Phase.gate;
        _notice = 'Without "All files access" the app cannot read a games '
            'folder in place. Grant it and try again.';
      });
      return;
    }
    final docs = await getApplicationDocumentsDirectory();
    if (!mounted) return;
    setState(() {
      _busy = true;
      _phase = _Phase.scanning;
      _notice = null;
      _folderShown = friendlyPath(path, docs.path);
    });
    final r = await SetupScanService.scan(path);
    if (!mounted) return;
    setState(() {
      _result = r;
      _busy = false;
      _scanned = true;
      _phase = _Phase.results;
    });
  }

  /// The store-compliance route: the app uses no user content, setup is
  /// done, and the workbench opens with the library hidden and the
  /// compliance statement one tap away. The switch on the Compliance page
  /// turns it back off.
  Future<void> _storeCompliance() async {
    await AppPrefs.setComplianceMode(true);
    if (!mounted) return;
    await _finish();
  }

  Future<void> _pickFolder() async {
    // Permission before the picker: the system picker will happily hand
    // back an SD-card path the app then cannot read.
    if (!await StoragePermission.ensure()) {
      if (!mounted) return;
      setState(() => _notice =
          'Without "All files access" the app cannot read a games folder '
          'in place. Grant it and try again.');
      return;
    }
    final p = await FilePicker.platform.getDirectoryPath();
    if (p != null) await _scanFolder(p);
  }

  Future<void> _finish() async {
    final r = _result;
    // Finishing with results means "my own BIOS and games" -- leave
    // compliance mode, or the next launch would hide the library that was
    // just scanned.
    if (r != null && (r.hasBios || r.hasGames)) {
      await AppPrefs.setComplianceMode(false);
    }
    if (r != null && r.hasBios) {
      await AppPrefs.setBiosPath(r.biosCandidates.first);
    }
    if (r != null && r.hasGames) {
      // The canonical layout is one folder -- usually called Saturn --
      // holding BIOS/ and Games/ subfolders. When the picked folder has a
      // Games/ subfolder, the library points at THAT, so it never walks
      // the BIOS, bezels or save folders beside it.
      final gamesSub = Directory(p.join(r.folderPath, 'Games'));
      await AppPrefs.setGamesFolder(
          gamesSub.existsSync() ? gamesSub.path : r.folderPath);
    }
    await AppPrefs.setSetupCompleted(true);
    widget.onComplete();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      body: SafeArea(
        child: switch (_phase) {
          _Phase.welcome => _welcomeView(),
          _Phase.primer => _primerView(),
          _Phase.gate => _gateView(),
          _Phase.scanning => _scanningView(),
          _Phase.results => Column(children: [
              if (_busy) const LinearProgressIndicator(),
              Expanded(child: _buildBody()),
              if (_scanned && !_busy) _buildActions(context),
            ]),
        },
      ),
    );
  }

  Widget _welcomeView() {
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: <Widget>[
            Center(
              child: ClipRRect(
                borderRadius: BorderRadius.circular(22),
                child: Image.asset(
                  'assets/images/app_icon.png',
                  height: 104,
                  width: 104,
                  fit: BoxFit.cover,
                  filterQuality: FilterQuality.medium,
                  errorBuilder: (BuildContext c, Object e, StackTrace? s) =>
                      const Icon(Icons.videogame_asset, size: 72),
                ),
              ),
            ),
            const SizedBox(height: 20),
            Text(
              'Retro-Saturn',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineMedium,
            ),
            const SizedBox(height: 12),
            const Text(
              'A Sega Saturn, running on this device. Setup takes a couple '
              'of minutes: point the app at your BIOS and disc images and '
              'it reads them where they are.',
              textAlign: TextAlign.center,
              style: TextStyle(color: Colors.white54, height: 1.5),
            ),
            const SizedBox(height: 32),
            FilledButton(
              onPressed: () => setState(() => _phase = _Phase.primer),
              child: const Padding(
                padding: EdgeInsets.symmetric(vertical: 14),
                child: Text('Get started'),
              ),
            ),
            const SizedBox(height: 8),
            TextButton(
              onPressed: () => setState(() => _phase = _Phase.gate),
              child: const Text('I have done this before'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _primerView() {
    return GettingStartedGuide(
      steps: <GuideStep>[
        GettingStartedSteps.whatYouNeed(),
        GettingStartedSteps.whereFilesGo(),
      ],
      closeLabel: 'Choose how to start',
      onClose: () => setState(() => _phase = _Phase.gate),
      onBack: () => setState(() => _phase = _Phase.welcome),
    );
  }

  Widget _gateView() {
    return ListView(
      padding: const EdgeInsets.symmetric(horizontal: 24),
      children: <Widget>[
        const SizedBox(height: 16),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: <Widget>[
            ClipRRect(
              borderRadius: BorderRadius.circular(10),
              child: Image.asset(
                'assets/images/app_icon.png',
                height: 44,
                width: 44,
                fit: BoxFit.cover,
                filterQuality: FilterQuality.medium,
                errorBuilder: (BuildContext c, Object e, StackTrace? st) =>
                    const Icon(Icons.videogame_asset, size: 30),
              ),
            ),
            const SizedBox(width: 12),
            Text('Retro-Saturn',
                style: Theme.of(context).textTheme.headlineSmall),
          ],
        ),
        const SizedBox(height: 24),
        if (_notice != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: Text(
              _notice!,
              style: const TextStyle(color: Colors.orangeAccent, height: 1.4),
            ),
          ),
        // The choice, with both routes explained in full -- the route that
        // needs nothing from the user comes first, because a reviewer (or
        // anyone with no BIOS yet) should not have to read instructions
        // aimed at somebody else to find it.
        Card(
          color: const Color(0xFF13161F),
          shape: RoundedRectangleBorder(
            side: const BorderSide(color: Color(0xFF2B3340)),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: <Widget>[
                Text('Two ways in',
                    style: Theme.of(context).textTheme.titleMedium),
                const SizedBox(height: 10),
                const Text(
                  'JUST LOOK AROUND FIRST\n'
                  'The app ships no BIOS -- Sega\'s is still copyrighted, '
                  'and there is no free replacement the way the Amiga and '
                  'C64 have one. Store Compliance mode uses no user content '
                  'at all: the library stays unscanned, and the compliance '
                  'statement explains everything a store review needs, '
                  'offline. You can switch it off later from the '
                  'Compliance page.',
                  style: TextStyle(color: Colors.white54, height: 1.4),
                ),
                const SizedBox(height: 10),
                OutlinedButton(
                  onPressed: _busy ? null : _storeCompliance,
                  child: const Padding(
                    padding: EdgeInsets.symmetric(vertical: 10),
                    child: Text('Store Compliance'),
                  ),
                ),
                TextButton(
                  onPressed: () => Navigator.of(context).push(
                      MaterialPageRoute<void>(
                          builder: (_) => Scaffold(
                                appBar: AppBar(
                                    title: const Text('Store compliance')),
                                body: const ComplianceScreen(),
                              ))),
                  child: const Text('Read the compliance statement'),
                ),
                const Divider(height: 28, color: Color(0xFF2B3340)),
                const Text(
                  'SET UP MY OWN SATURN\n'
                  'Keep one folder -- call it Saturn -- with two folders '
                  'inside: BIOS (your dumped BIOS .bin) and Games (your '
                  'disc images). Choose that Saturn folder and everything '
                  'is read in place: nothing is copied or moved, an SD '
                  'card works like any other folder, and you see what was '
                  'found before anything starts.',
                  style: TextStyle(color: Colors.white54, height: 1.4),
                ),
                const SizedBox(height: 10),
                FilledButton(
                  onPressed: _busy ? null : _pickFolder,
                  child: const Padding(
                    padding: EdgeInsets.symmetric(vertical: 10),
                    child: Text('Choose my Saturn folder…'),
                  ),
                ),
                TextButton(
                  onPressed: _busy ? null : _runFirstScan,
                  child: const Text('Scan the usual places instead'),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 8),
        Center(
          child: TextButton(
            onPressed: () => setState(() => _phase = _Phase.primer),
            child: const Text('Back to the guide'),
          ),
        ),
        const SizedBox(height: 24),
      ],
    );
  }

  Widget _scanningView() {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: <Widget>[
          const CircularProgressIndicator(),
          const SizedBox(height: 16),
          const Text('Scanning…'),
          const SizedBox(height: 6),
          Text(
            _folderShown,
            style: const TextStyle(fontSize: 11, color: Colors.white54),
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          ),
        ],
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
          Text(_folderShown,
              style: const TextStyle(fontSize: 11, color: Colors.white54),
              maxLines: 2, overflow: TextOverflow.ellipsis),
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
              TextButton.icon(
                onPressed: () => setState(() => _phase = _Phase.gate),
                icon: const Icon(Icons.arrow_back, size: 16),
                label: const Text('Back'),
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
            child: Text(_folderShown,
                style: const TextStyle(fontSize: 11, color: Colors.white54),
                maxLines: 2, overflow: TextOverflow.ellipsis),
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
