// ymir_pause_test — pausing must actually stop the machine.
//
// presentationPaused was set by the app and read back by the app and gated
// nothing at all: the worker loop never looked at it, so RunFrame kept going
// with the app in the background. On a phone that is a Saturn emulating at
// full speed, with audio, in someone's pocket.
//
// The second half matters as much as the first: the app saves a state, writes
// NVRAM and swaps discs WHILE paused, and all of those arrive as mailbox
// requests. A pause that stopped draining the mailbox would hang the caller
// waiting on the request's promise -- a worse bug than the one being fixed.

#include "ymir_bridge.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

static int failures = 0;

static void check(bool cond, const char *what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

int main() {
    namespace fs = std::filesystem;
    YmirInstance *inst = ymir_bridge_create();
    if (!inst) {
        std::fprintf(stderr, "FAIL: create\n");
        return 1;
    }

    // Running: the core reports a frame rate.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const int32_t runningFps = ymir_bridge_get_fps(inst);
    std::printf("fps while running: %d\n", runningFps);
    check(runningFps > 0, "the core runs frames when it is not paused");

    // Paused: it stops.
    ymir_bridge_set_presentation_paused(inst, 1);
    check(ymir_bridge_get_presentation_paused(inst) == 1, "pause is observable");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const int32_t pausedFps = ymir_bridge_get_fps(inst);
    std::printf("fps while paused: %d\n", pausedFps);
    check(pausedFps == 0, "a paused core runs no frames");

    // Still answering requests, or the app hangs the moment it tries to save
    // a state on the way to the background.
    const fs::path out = fs::temp_directory_path() / "ymir_pause_state.sav";
    fs::remove(out);
    const auto before = std::chrono::steady_clock::now();
    const int32_t rc = ymir_bridge_save_state(inst, out.c_str());
    const auto took = std::chrono::steady_clock::now() - before;
    const auto tookMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(took).count();
    std::printf("save while paused -> rc=%d in %lldms\n", rc, (long long)tookMs);
    check(rc == YMIR_OK, "a state can still be saved while paused");
    check(tookMs < 5000, "and the request is served promptly, not after a stall");

    // NVRAM too: the app writes it on the way out.
    const fs::path bup = fs::temp_directory_path() / "ymir_pause_bup.bin";
    check(ymir_bridge_save_internal_backup_memory(inst, bup.c_str()) == YMIR_OK,
          "NVRAM can still be written while paused");

    // Resuming starts it again, and does not try to catch up the lost time.
    ymir_bridge_set_presentation_paused(inst, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const int32_t resumedFps = ymir_bridge_get_fps(inst);
    std::printf("fps after resume: %d\n", resumedFps);
    check(resumedFps > 0, "resuming starts the core again");
    check(resumedFps < 200,
          "resume does not burst-run the frames missed while paused");

    ymir_bridge_destroy(inst);
    fs::remove(out);
    fs::remove(bup);

    if (failures) {
        std::printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: pause stops emulation and still serves requests\n");
    return 0;
}
