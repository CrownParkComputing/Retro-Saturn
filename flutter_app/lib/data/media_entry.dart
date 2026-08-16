// media_entry.dart — One scanned Saturn game on disk.
//
// Saturn has exactly one disc format in practice (the Ymir core accepts
// CHD / CUE / MDS / CCD / ISO), so the format filter is a flat enum
// rather than a hierarchy. The interesting per-title metadata is the
// normalized bezel key -- the library scanner derives it once at scan
// time and both the library tile and the bezel index share it later.

/// The disc-image kinds the Ymir core can mount. `unknown` is what any
/// extension we don't recognise yields; we filter those out in the
/// scanner, but the value still exists so a stray entry can be displayed
/// (with a "UNSUPPORTED" badge) rather than crashing the grid.
enum MediaFormat {
  unknown,
  chd,
  cue,
  mds,
  ccd,
  iso;

  /// Short label for the corner badge (upper-cased extension).
  String get extensionLabel {
    switch (this) {
      case MediaFormat.chd:
        return 'CHD';
      case MediaFormat.cue:
        return 'CUE';
      case MediaFormat.mds:
        return 'MDS';
      case MediaFormat.ccd:
        return 'CCD';
      case MediaFormat.iso:
        return 'ISO';
      case MediaFormat.unknown:
        return '?';
    }
  }

  /// True for the formats the core can actually mount.
  bool get isSupported =>
      this == MediaFormat.chd ||
      this == MediaFormat.cue ||
      this == MediaFormat.mds ||
      this == MediaFormat.ccd ||
      this == MediaFormat.iso;

  /// Map a filename extension to a [MediaFormat]. The matching is
  /// case-insensitive and ignores a leading dot, so `.cue` and `CUE`
  /// both resolve.
  static MediaFormat fromExtension(String ext) {
    switch (ext.toLowerCase()) {
      case 'chd':
        return MediaFormat.chd;
      case 'cue':
        return MediaFormat.cue;
      case 'mds':
        return MediaFormat.mds;
      case 'ccd':
        return MediaFormat.ccd;
      case 'iso':
        return MediaFormat.iso;
      default:
        return MediaFormat.unknown;
    }
  }
}

/// One game as discovered on disk. Immutable: the library grid and the
/// bezel index both key off [path] + [bezelKey], neither of which changes
/// once the entry has been scanned.
class MediaEntry {
  /// The filename as the user sees it (e.g. `"Panzer Dragoon (USA).cue"`).
  final String displayName;

  /// Absolute path on the device -- what the core's loadDisc() wants.
  final String path;

  /// Format derived from the filename extension.
  final MediaFormat format;

  /// Display title with the extension stripped ("Panzer Dragoon (USA)").
  /// Kept on the entry so grid cells that want to show a cleaned-up name
  /// don't have to re-derive it (and so we agree on what "cleaned up"
  /// means).
  final String baseName;

  /// Normalized bezel key -- the form the bezel index uses for both
  /// exact and loose lookups. Empty when the display name has no usable
  /// characters; the bezel index falls back to a hash in that case.
  final String bezelKey;

  const MediaEntry({
    required this.displayName,
    required this.path,
    required this.format,
    required this.baseName,
    required this.bezelKey,
  });

  /// Upper-case extension for the small badge under the title.
  String get extensionLabel => format.extensionLabel;

  /// Two entries are the same game if they share path + format (covers
  /// the case where the user has the same disc under different parent
  /// folders -- the scanner dedupes against this).
  @override
  bool operator ==(Object other) =>
      other is MediaEntry && other.path == path && other.format == format;

  @override
  int get hashCode => Object.hash(path, format);
}
