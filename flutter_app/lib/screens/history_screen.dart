// history_screen.dart — Recent games played. Backed by AppPrefs
// (SharedPreferences) as a JSON list of {path, lastPlayed, title}.

import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

class HistoryEntry {
  final String path;
  final String title;
  final DateTime lastPlayed;
  HistoryEntry(this.path, this.title, this.lastPlayed);
  Map<String, dynamic> toJson() => {
        'path': path,
        'title': title,
        'lastPlayed': lastPlovedMs = lastPlayed.millisecondsSinceEpoch,
      };
  late final int lastPlovedMs;
  static HistoryEntry fromJson(Map<String, dynamic> j) => HistoryEntry(
        j['path'] as String,
        j['title'] as String,
        DateTime.fromMillisecondsSinceEpoch(j['lastPlayedMs'] as int),
      );
}

class HistoryService {
  static const _key = 'history_entries';
  static Future<List<HistoryEntry>> all() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString(_key);
    if (raw == null || raw.isEmpty) return [];
    final list = (jsonDecode(raw) as List).cast<Map<String, dynamic>>();
    return list.map(HistoryEntry.fromJson).toList();
  }
  static Future<void> record(String path, String title) async {
    final p = await SharedPreferences.getInstance();
    final entries = await all();
    entries.removeWhere((e) => e.path == path);
    entries.insert(0, HistoryEntry(path, title, DateTime.now()));
    if (entries.length > 20) entries.removeRange(20, entries.length);
    await p.setString(_key, jsonEncode(entries.map((e) => e.toJson()).toList()));
  }
}

class HistoryScreen extends StatefulWidget {
  const HistoryScreen({super.key});
  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  List<HistoryEntry> _entries = [];

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final e = await HistoryService.all();
    if (!mounted) return;
    setState(() => _entries = e);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF050607),
      appBar: AppBar(title: const Text('📜 History')),
      body: _entries.isEmpty
          ? const Center(child: Text('No games played yet', style: TextStyle(color: Colors.white54)))
          : ListView.builder(
              itemCount: _entries.length,
              itemBuilder: (_, i) {
                final e = _entries[i];
                return ListTile(
                  leading: const Icon(Icons.history),
                  title: Text(e.title),
                  subtitle: Text(e.path, maxLines: 1, overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 10, color: Colors.white54)),
                  trailing: Text(_ago(e.lastPlayed),
                      style: const TextStyle(fontSize: 10, color: Colors.white54)),
                );
              }),
    );
  }

  String _ago(DateTime t) {
    final d = DateTime.now().difference(t);
    if (d.inMinutes < 60) return '${d.inMinutes}m';
    if (d.inHours < 24) return '${d.inHours}h';
    return '${d.inDays}d';
  }
}