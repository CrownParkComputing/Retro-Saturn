// setup_wizard_screen.dart — 4-page first-run wizard: pick the
// Saturn BIOS, pick the games folder, optional bezel pack URL,
// ready. Mirrors the ViceMultiplatform SetupWizardScreen structure
// (welcome → rom dir → artwork URL → ready).

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:ymir_multiplatform/services/app_prefs.dart';

class SetupWizardScreen extends StatefulWidget {
  final VoidCallback onComplete;
  const SetupWizardScreen({super.key, required this.onComplete});

  @override
  State<SetupWizardScreen> createState() => _SetupWizardScreenState();
}

class _SetupWizardScreenState extends State<SetupWizardScreen> {
  final PageController _controller = PageController();
  int _page = 0;

  String? _biosPath;
  String? _gamesFolder;

  void _next() {
    if (_page < 3) {
      _controller.nextPage(
          duration: const Duration(milliseconds: 220), curve: Curves.easeOut);
    } else {
      _finish();
    }
  }

  void _back() {
    if (_page > 0) {
      _controller.previousPage(
          duration: const Duration(milliseconds: 220), curve: Curves.easeOut);
    }
  }

  Future<void> _pickBios() async {
    final r = await FilePicker.platform.pickFiles(
      type: FileType.any,
      allowMultiple: false,
      withData: false,
    );
    if (r != null && r.files.single.path != null) {
      setState(() => _biosPath = r.files.single.path);
    }
  }

  Future<void> _pickGames() async {
    final p = await FilePicker.platform.getDirectoryPath();
    if (p != null) setState(() => _gamesFolder = p);
  }

  Future<void> _finish() async {
    if (_biosPath != null) await AppPrefs.setBiosPath(_biosPath!);
    if (_gamesFolder != null) await AppPrefs.setGamesFolder(_gamesFolder!);
    await AppPrefs.setSetupCompleted(true);
    widget.onComplete();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      body: SafeArea(
        child: Column(children: [
          LinearProgressIndicator(value: (_page + 1) / 4),
          Expanded(
            child: PageView(
              controller: _controller,
              physics: const NeverScrollableScrollPhysics(),
              onPageChanged: (i) => setState(() => _page = i),
              children: [
                _WelcomePage(onNext: _next),
                _BiosPage(path: _biosPath, onPick: _pickBios, onNext: _next, onBack: _back),
                _GamesPage(path: _gamesFolder, onPick: _pickGames, onNext: _next, onBack: _back),
                _ReadyPage(onFinish: _next, onBack: _back),
              ],
            ),
          ),
        ]),
      ),
    );
  }
}

class _WelcomePage extends StatelessWidget {
  final VoidCallback onNext;
  const _WelcomePage({required this.onNext});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Spacer(),
        Text('Ymir Multiplatform',
            style: Theme.of(context).textTheme.headlineMedium),
        const SizedBox(height: 12),
        const Text(
            'Sega Saturn emulator. You will need a BIOS file (saturn_bios.bin) '
            'and at least one game disc image (CHD / CUE / ISO / MDS).',
            style: TextStyle(fontSize: 14)),
        const Spacer(),
        Align(
          alignment: Alignment.centerRight,
          child: FilledButton(onPressed: onNext, child: const Text('Start setup')),
        ),
      ]),
    );
  }
}

class _BiosPage extends StatelessWidget {
  final String? path;
  final VoidCallback onPick;
  final VoidCallback onNext;
  final VoidCallback onBack;
  const _BiosPage({required this.path, required this.onPick, required this.onNext, required this.onBack});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('Saturn BIOS', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 8),
        const Text(
            'Pick a 512 KiB Saturn BIOS file. A regular US/JP/EU BIOS will show '
            'a one-time Set Clock + Set Language wizard; an auto-confirm BIOS '
            'skips it.', style: TextStyle(fontSize: 13)),
        const SizedBox(height: 16),
        OutlinedButton.icon(
          onPressed: onPick,
          icon: const Icon(Icons.description),
          label: Text(path == null ? 'Pick BIOS file' : 'Change: ${path!.split('/').last}'),
        ),
        if (path != null) Padding(
          padding: const EdgeInsets.only(top: 4),
          child: Text(path!, style: const TextStyle(fontSize: 11, color: Colors.white54)),
        ),
        const Spacer(),
        Row(children: [
          OutlinedButton(onPressed: onBack, child: const Text('Back')),
          const Spacer(),
          FilledButton(
              onPressed: path == null ? null : onNext,
              child: const Text('Next')),
        ]),
      ]),
    );
  }
}

class _GamesPage extends StatelessWidget {
  final String? path;
  final VoidCallback onPick;
  final VoidCallback onNext;
  final VoidCallback onBack;
  const _GamesPage({required this.path, required this.onPick, required this.onNext, required this.onBack});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('Games folder', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 8),
        const Text(
            'Pick the folder that contains your Saturn disc images. CHD / CUE / '
            'ISO / MDS files in this folder and its subfolders will appear in '
            'the library.', style: TextStyle(fontSize: 13)),
        const SizedBox(height: 16),
        OutlinedButton.icon(
          onPressed: onPick,
          icon: const Icon(Icons.folder),
          label: Text(path == null ? 'Pick games folder' : 'Change: ${path!.split('/').last}'),
        ),
        const Spacer(),
        Row(children: [
          OutlinedButton(onPressed: onBack, child: const Text('Back')),
          const Spacer(),
          FilledButton(
              onPressed: path == null ? null : onNext,
              child: const Text('Next')),
        ]),
      ]),
    );
  }
}

class _ReadyPage extends StatelessWidget {
  final VoidCallback onFinish;
  final VoidCallback onBack;
  const _ReadyPage({required this.onFinish, required this.onBack});
  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('Ready', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 12),
        const Text('Setup is complete. You can re-run it any time from the Workbench.',
            style: TextStyle(fontSize: 13)),
        const Spacer(),
        Row(children: [
          OutlinedButton(onPressed: onBack, child: const Text('Back')),
          const Spacer(),
          FilledButton(onPressed: onFinish, child: const Text('Finish')),
        ]),
      ]),
    );
  }
}