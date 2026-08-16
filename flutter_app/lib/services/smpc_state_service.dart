// smpc_state_service.dart — Write a default SMPC persistent state
// file on first launch so the BIOS Set Clock + Set Language wizard
// auto-skips.
//
// The Saturn SMPC persistent state is a 25-byte file (per ymir-core's
// WritePersistentData):
//   byte 0:        version (must be 0x01)
//   bytes 1-3:     reserved (0x00)
//   bytes 4-7:     SMEM (4 bytes, default 0)
//   byte 8:         m_STE flag (1 = BIOS persists config; 0 = wizard on boot)
//   bytes 9-16:    RTC host offset (int64 little-endian, 0 = use host time)
//   bytes 17-24:   RTC last-set timestamp (int64 little-endian, Unix s)
//
// A pre-populated file with m_STE=1 + a current timestamp tells the
// BIOS "you've already been through the wizard, just go" on boot.

import 'dart:io';
import 'dart:typed_data';

class SmpcStateService {
  /// Write a default "wizard already completed" state file if one
  /// doesn't exist yet. Idempotent — never overwrites.
  static Future<void> ensureDefaults(String path) async {
    if (File(path).existsSync()) return;

    final nowMs = DateTime.now().millisecondsSinceEpoch;
    final nowSec = nowMs ~/ 1000;

    final bytes = <int>[
      0x01,                       // version
      0x00, 0x00, 0x00,            // reserved
      0x00, 0x00, 0x00, 0x00,      // SMEM (defaults)
      0x01,                        // m_STE = 1 → BIOS keeps config, no wizard
    ];
    // RTC offset = 0 (int64 LE) — host time used directly
    bytes.addAll(_i64le(0));
    // RTC timestamp = now (int64 LE) — Unix seconds
    bytes.addAll(_i64le(nowSec));

    final f = File(path);
    await f.parent.create(recursive: true);
    await f.writeAsBytes(bytes);
  }

  /// Serialize a signed 64-bit integer as little-endian bytes.
  static List<int> _i64le(int v) {
    final b = ByteData(8);
    b.setInt64(0, v, Endian.little);
    return b.buffer.asUint8List();
  }
}