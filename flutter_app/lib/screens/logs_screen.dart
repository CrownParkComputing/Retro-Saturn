// logs_screen.dart — Tail of the AppLog file, with a "Clear" button.
// Useful for debugging BIOS boot / disc load / NVRAM save crashes
// without needing adb logcat.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:ymir_multiplatform/services/app_log.dart';

class LogsScreen extends StatefulWidget {
  const LogsScreen({super.key});

  @override
  State<LogsScreen> createState() => _LogsScreenState();
}

class _LogsScreenState extends State<LogsScreen> {
  String _text = '(loading…)';
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    setState(() => _busy = true);
    final text = await AppLog.read();
    if (!mounted) return;
    setState(() {
      _text = text;
      _busy = false;
    });
  }

  Future<void> _clear() async {
    await AppLog.clear();
    await _refresh();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(
        title: const Text('📝 Logs', style: TextStyle(fontSize: 14)),
        toolbarHeight: 44,
        actions: [
          IconButton(
            tooltip: 'Refresh',
            icon: const Icon(Icons.refresh, size: 18),
            onPressed: _busy ? null : _refresh,
          ),
          IconButton(
            tooltip: 'Copy to clipboard',
            icon: const Icon(Icons.copy, size: 18),
            onPressed: _text.isEmpty ? null : () => Clipboard.setData(
                ClipboardData(text: _text)),
          ),
          IconButton(
            tooltip: 'Clear log',
            icon: const Icon(Icons.delete_outline, size: 18),
            onPressed: _clear,
          ),
        ],
      ),
      body: Stack(children: [
        Positioned.fill(
          child: SingleChildScrollView(
            reverse: true,
            padding: const EdgeInsets.all(8),
            child: SelectableText(
              _text.isEmpty ? '(empty)' : _text,
              style: const TextStyle(
                fontFamily: 'monospace',
                fontSize: 10,
                color: Color(0xFFC7C7C7),
                height: 1.3,
              ),
            ),
          ),
        ),
        if (_busy)
          const Positioned(
            top: 8, right: 8,
            child: SizedBox(
              width: 16, height: 16,
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
          ),
        Positioned(
          left: 8, bottom: 8,
          child: Text(
            'path: ${AppLog.filePath ?? "(not initialized)"}',
            style: const TextStyle(fontSize: 9, color: Colors.white38),
          ),
        ),
      ]),
    );
  }
}