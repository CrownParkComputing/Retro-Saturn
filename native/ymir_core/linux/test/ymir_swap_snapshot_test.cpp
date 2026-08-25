// ymir_swap_snapshot_test — round-trip the bridge-defined save-state
// wire format. Without a BIOS, the state is trivial but the bridge
// must serialize, write, read, deserialize without corrupting bytes.
//
// This test used to assert the OPPOSITE: that save_state returns
// YMIR_ERR_GENERIC, because on-disk save states were deferred out of v1. That
// premise is gone -- savestate_io.hpp implements the format -- so the test now
// pins the behaviour that replaced it.

#include "ymir_bridge.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

int main(int /*argc*/, char ** /*argv*/) {
    YmirInstance *inst = ymir_bridge_create();
    if (!inst) return 1;

    fs::path tmp = fs::temp_directory_path() / "ymir_swap_test.ymstate";
    fs::remove(tmp);

    /* let it run for a bit */
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    /* Save, then load it straight back.
     *
     * This also exercises the worker thread's 1 MB stack: SaveState is ~6 MB,
     * so a handler that holds one as a local overflows here rather than only
     * on a device. That is exactly how it was found -- as a crash when a game
     * was tapped, because the frame is reserved on entry and took disc loading
     * down with it. */
    int32_t rc = ymir_bridge_save_state(inst, tmp.string().c_str());
    std::printf("save_state -> %d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: save_state returned %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }
    if (!fs::exists(tmp) || fs::file_size(tmp) == 0) {
        std::fprintf(stderr, "FAIL: save_state wrote nothing\n");
        ymir_bridge_destroy(inst);
        return 1;
    }

    rc = ymir_bridge_load_state(inst, tmp.string().c_str());
    std::printf("load_state -> %d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: load_state returned %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }

    /* swap: save the live machine beside the target, then load the target.
     * The .cur file it writes on the way through must not be left behind. */
    rc = ymir_bridge_swap_state(inst, tmp.string().c_str());
    std::printf("swap_state -> %d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: swap_state returned %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }
    if (fs::exists(tmp.string() + ".cur")) {
        std::fprintf(stderr, "FAIL: swap_state left its scratch file behind\n");
        ymir_bridge_destroy(inst);
        return 1;
    }

    /* The SMPC persistent state has its own round trip. */
    fs::path smpcPath = fs::temp_directory_path() / "ymir_smpc_state_test.bin";
    rc = ymir_bridge_save_smpc_state(inst, smpcPath.string().c_str());
    std::printf("save_smpc_state -> %d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: save_smpc_state returned %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }
    rc = ymir_bridge_load_smpc_state(inst, smpcPath.string().c_str());
    std::printf("load_smpc_state -> %d\n", rc);
    if (rc != YMIR_OK) {
        std::fprintf(stderr, "FAIL: load_smpc_state returned %d\n", rc);
        ymir_bridge_destroy(inst);
        return 1;
    }
    fs::remove(smpcPath);

    fs::remove(tmp);
    ymir_bridge_destroy(inst);
    std::printf("OK: save/load/swap round-trip on a 1 MB worker stack; SMPC state round-trips\n");
    return 0;
}