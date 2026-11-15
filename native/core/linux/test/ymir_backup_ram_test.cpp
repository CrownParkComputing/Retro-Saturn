// ymir_backup_ram_test — a save with no NVRAM file present must not crash.
//
// This reproduces a SIGSEGV that killed the app on the Flip2 roughly 60
// seconds into every game: the Dart side starts a 60s NVRAM auto-save after a
// disc loads, and ymir-core's BackupMemory::Size() dereferences a null
// container when no backup image was ever loaded. "NVRAM load: false" is the
// normal first-run path, so this was reachable on a fresh install of any
// title, and it landed on the emulation thread where there is no Dart
// exception to catch and nothing in the log but the process ending.
//
// The fix fits the 32 KiB internal backup RAM at create time, the way a real
// Saturn always has it. Asserted here rather than on a device: the crash took
// a minute of play to reach and left no trace in the app's own log.

#include "ymir_bridge.h"

#include <cstdio>
#include <filesystem>
#include <thread>

int main() {
    namespace fs = std::filesystem;
    const fs::path out = fs::temp_directory_path() / "ymir_bup_test.bin";
    fs::remove(out);

    YmirInstance *inst = ymir_bridge_create();
    if (!inst) {
        std::fprintf(stderr, "FAIL: create\n");
        return 1;
    }

    // No load_internal_backup_memory first: that is the whole point. This is
    // the state of a fresh install, with no save file to restore from.
    const int32_t rc = ymir_bridge_save_internal_backup_memory(inst, out.c_str());
    std::printf("save with no prior load -> rc=%d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: expected YMIR_OK, got %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }

    std::error_code ec;
    const auto size = fs::file_size(out, ec);
    std::printf("wrote %zu bytes\n", (size_t)size);
    if (ec || size != 32u * 1024u) {
        std::fprintf(stderr, "FAIL: expected 32768 bytes of internal backup RAM\n");
        ymir_bridge_destroy(inst);
        return 1;
    }

    // And again, the way the auto-save timer would: repeated saves must stay
    // safe, not just the first.
    for (int i = 0; i < 3; ++i) {
        if (ymir_bridge_save_internal_backup_memory(inst, out.c_str()) != YMIR_OK) {
            std::fprintf(stderr, "FAIL: repeat save %d\n", i);
            ymir_bridge_destroy(inst);
            return 1;
        }
    }

    // A load that cannot succeed must leave the fitted RAM alone rather than
    // dropping the container and re-arming the crash.
    const fs::path missing = fs::temp_directory_path() / "ymir_bup_does_not_exist.bin";
    fs::remove(missing);
    ymir_bridge_load_internal_backup_memory(inst, missing.c_str(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    if (ymir_bridge_save_internal_backup_memory(inst, out.c_str()) != YMIR_OK) {
        std::fprintf(stderr, "FAIL: save after a failed load\n");
        ymir_bridge_destroy(inst);
        return 1;
    }

    ymir_bridge_destroy(inst);
    fs::remove(out);
    std::printf("OK: NVRAM save is safe with no image loaded\n");
    return 0;
}
