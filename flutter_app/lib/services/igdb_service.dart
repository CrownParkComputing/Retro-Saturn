// igdb_service.dart — Twitch OAuth client_credentials + IGDB search for
// the Sega Saturn (platform id 46).
//
// Why both halves live in this file: the token endpoint and the games
// endpoint are paired -- the games endpoint insists on a fresh bearer
// token and the same `Client-ID` header that was used to mint it. The
// pair would always travel together, so they belong on one object.
//
// Caching: every search is mirrored to `<app docs>/igdb_cache/<key>.json`
// keyed by a short hash of the lowercased, trimmed query. Cached results
// stay fresh for [cacheTtl] before being re-fetched.
//
// Credentials: IGDB requires Twitch dev credentials. Real builds must
// provide them; missing credentials degrade to "no results" rather than
// throwing so an unconfigured developer build still renders an empty
// library tile grid.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

/// IGDB platform id for Sega Saturn. Spec says 46; the legacy Java port
/// used 32 (out-of-date platform table). 46 is the ID the IGDB team
/// currently publishes.
const int kIgdbSaturnPlatformId = 46;

const String _twitchTokenUrl = 'https://id.twitch.tv/oauth2/token';
const String _igdbGamesUrl = 'https://api.igdb.com/v4/games';

/// How long a cached IGDB result stays valid. 7 days is plenty for box
/// art -- the data barely changes -- and short enough that a wrong
/// initial match self-heals on the next refresh.
const Duration cacheTtl = Duration(days: 7);

/// A single hit from the IGDB `/games` endpoint. Only the fields the
/// library tile cares about are decoded -- the JSON gives plenty more
/// (websites, genres, themes, ...) but the rest is noise in this UI.
class IgdbGame {
  final int id;
  final String name;
  final String? summary;
  final int? firstReleaseDate;
  final String? coverUrl;
  final List<String> screenshotUrls;

  const IgdbGame({
    required this.id,
    required this.name,
    this.summary,
    this.firstReleaseDate,
    this.coverUrl,
    this.screenshotUrls = const [],
  });

  factory IgdbGame.fromJson(Map<String, dynamic> json) {
    String? coverUrl;
    final cover = json['cover'];
    if (cover is Map) {
      final u = cover['url'];
      if (u is String && u.isNotEmpty) {
        // IGDB returns protocol-relative URLs ("//images.igdb.com/...").
        // Coerce to https so an Image.network() call later doesn't have
        // to guess.
        coverUrl = u.startsWith('//') ? 'https:$u' : u;
        // Bump thumbnail size to cover_big; IGDB exposes t_thumb,
        // t_cover_small, t_cover_big, t_1080p etc. via this swap.
        coverUrl = coverUrl.replaceFirst('t_thumb', 't_cover_big');
      }
    }
    final screenshots = <String>[];
    final ss = json['screenshots'];
    if (ss is List) {
      for (final item in ss) {
        if (item is Map) {
          final u = item['url'];
          if (u is String && u.isNotEmpty) {
            final fixed = u.startsWith('//') ? 'https:$u' : u;
            screenshots.add(fixed.replaceFirst('t_thumb', 't_screenshot_big'));
          }
        }
      }
    }
    final rel = json['first_release_date'];
    return IgdbGame(
      id: (json['id'] as num?)?.toInt() ?? 0,
      name: (json['name'] as String?) ?? '',
      summary: json['summary'] as String?,
      firstReleaseDate: (rel as num?)?.toInt(),
      coverUrl: coverUrl,
      screenshotUrls: screenshots,
    );
  }
}

/// Lightweight Twitch OAuth token: holds the bearer and the expiry so we
/// don't re-mint for every search.
class _TwitchToken {
  final String accessToken;
  final DateTime expiresAt;
  _TwitchToken(this.accessToken, this.expiresAt);
  bool get isFresh =>
      DateTime.now().add(const Duration(minutes: 1)).isBefore(expiresAt);
}

/// The IGDB service. Stateless singleton -- credentials live in static
/// fields so a host test can swap them per-test, and the bearer token is
/// lazy + cached.
class IgdbService {
  IgdbService._();

  /// Twitch client_id. Must be supplied by the embedding app (e.g. via
  /// `--dart-define`). Empty disables network calls.
  static String clientId = const String.fromEnvironment('IGDB_CLIENT_ID');

  /// Twitch client_secret. Must be supplied by the embedding app (e.g. via
  /// `--dart-define`). Empty disables network calls.
  static String clientSecret =
      const String.fromEnvironment('IGDB_CLIENT_SECRET');

  /// Override the HTTP client (used by tests to inject a fake that does
  /// not require Twitch credentials).
  static http.Client Function()? httpClientFactory;

  static _TwitchToken? _token;

  /// Where the JSON cache lives. Defaults to `<app docs>/igdb_cache/`.
  /// Override by patching this in tests.
  static Future<Directory> cacheDir() async {
    final docs = await getApplicationSupportDirectory();
    final dir = Directory(p.join(docs.path, 'igdb_cache'));
    if (!dir.existsSync()) await dir.create(recursive: true);
    return dir;
  }

  /// Look up Saturn games matching [query]. The cache is checked first;
  /// on miss, IGDB is consulted and the response is mirrored to disk.
  ///
  /// Returns an empty list (never throws) when credentials are missing,
  /// the network is down, or the query is empty.
  static Future<List<IgdbGame>> search(String query) async {
    final cleaned = query.trim();
    if (cleaned.isEmpty) return const [];

    final cacheKey = _cacheKeyFor(cleaned);
    final cached = await _readCache(cacheKey);
    if (cached != null) return cached;

    final fetched = await _fetchFromIgdb(cleaned);
    await _writeCache(cacheKey, fetched);
    return fetched;
  }

  /// 8-hex-digit FNV-1a of the trimmed lowercased query. Lets us
  /// hash a long title without worrying about filesystem-illegal
  /// characters or the upper/lower case of "Sonic CD" vs "SONIC CD"
  /// producing the same key (which they should).
  static String _cacheKeyFor(String query) => _fnv1a(query.trim().toLowerCase());

  /// 32-bit FNV-1a, rendered as 8 lowercase hex digits. Stable across
  /// runs (no Object.hashCode randomness), no extra package dependency.
  static String _fnv1a(String input) {
    var hash = 0x811c9dc5 & 0xffffffff;
    for (final code in input.codeUnits) {
      hash ^= code & 0xff;
      hash = (hash * 0x01000193) & 0xffffffff;
    }
    return hash.toRadixString(16).padLeft(8, '0');
  }

  static Future<List<IgdbGame>?> _readCache(String key) async {
    try {
      final file = File(p.join((await cacheDir()).path, '$key.json'));
      if (!file.existsSync()) return null;
      final raw = await file.readAsString();
      if (raw.isEmpty) return null;
      final map = jsonDecode(raw);
      if (map is! Map || map['savedAt'] is! num) return null;
      final savedAt = DateTime.fromMillisecondsSinceEpoch(
        ((map['savedAt'] as num).toInt()) * 1000,
      );
      if (DateTime.now().difference(savedAt) > cacheTtl) return null;
      final results = map['results'];
      if (results is! List) return null;
      return results
          .whereType<Map>()
          .map((m) => IgdbGame.fromJson(Map<String, dynamic>.from(m)))
          .toList(growable: false);
    } catch (_) {
      return null;
    }
  }

  static Future<void> _writeCache(String key, List<IgdbGame> games) async {
    if (games.isEmpty) return;
    try {
      final dir = await cacheDir();
      final file = File(p.join(dir.path, '$key.json'));
      final payload = jsonEncode({
        'savedAt':
            DateTime.now().toUtc().millisecondsSinceEpoch ~/ 1000,
        'results': games.map((g) => _gameToJson(g)).toList(),
      });
      await file.writeAsString(payload, flush: true);
    } catch (_) {
      // Cache write is best-effort. Don't fail the search because the
      // disk was full.
    }
  }

  static Map<String, dynamic> _gameToJson(IgdbGame g) => {
        'id': g.id,
        'name': g.name,
        'summary': g.summary,
        'first_release_date': g.firstReleaseDate,
        'cover': g.coverUrl == null
            ? null
            : {'url': g.coverUrl!.replaceFirst('t_cover_big', 't_thumb')},
        'screenshots': g.screenshotUrls
            .map((u) => {'url': u.replaceFirst('t_screenshot_big', 't_thumb')})
            .toList(),
      };

  static Future<List<IgdbGame>> _fetchFromIgdb(String query) async {
    if (clientId.isEmpty || clientSecret.isEmpty) {
      // No creds = degrade to no results rather than throw. An
      // unconfigured developer build still shows a working library grid.
      return const [];
    }
    final client = (httpClientFactory ?? http.Client.new)();
    try {
      final token = await _ensureToken(client);
      final body =
          'fields name,first_release_date,cover.url,screenshots.url,summary; '
          'where platforms=($kIgdbSaturnPlatformId); '
          'search "${_escapeQuery(query)}"; '
          'limit 25;';
      final resp = await client.post(
        Uri.parse(_igdbGamesUrl),
        headers: {
          'Client-ID': clientId,
          'Authorization': 'Bearer $token',
          'Accept': 'application/json',
          'Content-Type': 'text/plain',
        },
        body: body,
      );
      if (resp.statusCode != 200) return const [];
      final raw = resp.bodyBytes.isEmpty ? '' : utf8.decode(resp.bodyBytes);
      if (raw.isEmpty) return const [];
      final list = jsonDecode(raw);
      if (list is! List) return const [];
      return list
          .whereType<Map>()
          .map((m) => IgdbGame.fromJson(Map<String, dynamic>.from(m)))
          .toList(growable: false);
    } catch (_) {
      return const [];
    } finally {
      // The injected http.Client must be closed by the injector; otherwise
      // a fresh one is owned by this call.
      final factory = httpClientFactory;
      if (factory == null) client.close();
    }
  }

  static Future<String> _ensureToken(http.Client client) async {
    final t = _token;
    if (t != null && t.isFresh) return t.accessToken;
    final resp = await client.post(
      Uri.parse(_twitchTokenUrl),
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: {
        'client_id': clientId,
        'client_secret': clientSecret,
        'grant_type': 'client_credentials',
      },
    );
    if (resp.statusCode != 200) {
      throw StateError('IGDB auth failed: HTTP ${resp.statusCode}');
    }
    final raw = utf8.decode(resp.bodyBytes);
    final map = jsonDecode(raw) as Map;
    final token = map['access_token'] as String;
    final expiresIn = (map['expires_in'] as num?)?.toInt() ?? 3600;
    final fresh = _TwitchToken(
      token,
      DateTime.now().add(Duration(seconds: expiresIn)),
    );
    _token = fresh;
    return token;
  }

  /// The IGDB search grammar is a tiny Prolog-ish; quotes around the
  /// user-supplied query protect against a value that itself contains a
  /// double-quote (rare in game names but not impossible).
  static String _escapeQuery(String query) {
    final escaped = query.replaceAll('"', r'\"');
    return '"$escaped"';
  }

  /// Test hook: drop the cached token.
  static void clearToken() => _token = null;

  /// Test hook: where the cache file for [query] would land.
  static Future<String> cachePathFor(String query) async =>
      p.join((await cacheDir()).path, '${_cacheKeyFor(query)}.json');
}
