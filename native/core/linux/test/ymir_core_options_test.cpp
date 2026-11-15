// ymir_core_options_test — the core options the app exposes.
//
// Every one of these is a knob a user can move mid-game, so the two things
// worth proving are that a set survives a read back (the app renders what the
// core actually holds, not what it last sent) and that a bad value is refused
// rather than clamped silently -- a CD read speed of 0 or an overclock of 5
// would be a Saturn that does not run, reported as success.

#include "ymir_bridge.h"

#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

static void check(bool cond, const char *what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

/* Sets are applied on the emulator thread, so a read back has to let the
 * mailbox drain first. */
static int32_t set_and_read(YmirInstance *inst, YmirCoreOption opt, int32_t v) {
    ymir_bridge_set_core_option(inst, opt, v);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return ymir_bridge_get_core_option(inst, opt);
}

int main() {
    YmirInstance *inst = ymir_bridge_create();
    if (!inst) {
        std::fprintf(stderr, "FAIL: create\n");
        return 1;
    }

    // Defaults match what ymir-core documents, so the app's catalogue and the
    // core cannot drift apart silently.
    check(ymir_bridge_get_core_option(inst, YMIR_OPT_CD_READ_SPEED) == 2,
          "CD read speed defaults to the real drive's 2x");
    check(ymir_bridge_get_core_option(inst, YMIR_OPT_SH2_OVERCLOCK) == 100,
          "SH-2 overclock defaults to 100% (no overclock)");
    check(ymir_bridge_get_core_option(inst, YMIR_OPT_AUDIO_INTERPOLATION) == 1,
          "audio interpolation defaults to linear, as the SCSP does");
    check(ymir_bridge_get_core_option(inst, YMIR_OPT_VIDEO_STANDARD) == 0,
          "video standard defaults to NTSC");

    // Round trips.
    check(set_and_read(inst, YMIR_OPT_CD_READ_SPEED, 8) == 8,
          "CD read speed round-trips");
    check(set_and_read(inst, YMIR_OPT_SH2_OVERCLOCK, 150) == 150,
          "SH-2 overclock round-trips");
    check(set_and_read(inst, YMIR_OPT_VIDEO_STANDARD, 1) == 1,
          "video standard switches to PAL");
    check(set_and_read(inst, YMIR_OPT_EMULATE_SH2_CACHE, 1) == 1,
          "SH-2 cache emulation round-trips");
    check(set_and_read(inst, YMIR_OPT_THREADED_VDP1, 0) == 0,
          "threaded VDP1 can be turned off");
    check(set_and_read(inst, YMIR_OPT_THREADED_VDP2, 0) == 0,
          "threaded VDP2 can be turned off");
    check(set_and_read(inst, YMIR_OPT_THREADED_DEINTERLACE, 0) == 0,
          "threaded deinterlacer can be turned off");
    check(set_and_read(inst, YMIR_OPT_AUDIO_INTERPOLATION, 0) == 0,
          "audio interpolation switches to nearest neighbour");
    check(set_and_read(inst, YMIR_OPT_AUTODETECT_REGION, 0) == 0,
          "region autodetect can be turned off");
    check(set_and_read(inst, YMIR_OPT_CDBLOCK_LLE, 1) == 1,
          "CD block LLE round-trips");

    // Refusals. A rejected set must leave the previous value alone.
    ymir_bridge_set_core_option(inst, YMIR_OPT_CD_READ_SPEED, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int32_t bad : {0, 1, 201, -5}) {
        const int32_t rc =
            ymir_bridge_set_core_option(inst, YMIR_OPT_CD_READ_SPEED, bad);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        char msg[96];
        std::snprintf(msg, sizeof(msg), "CD read speed %d is refused", bad);
        check(rc == YMIR_ERR_INVALID_ARG, msg);
    }
    check(ymir_bridge_get_core_option(inst, YMIR_OPT_CD_READ_SPEED) == 8,
          "a refused set leaves the previous value in place");

    for (int32_t bad : {0, 49, 501}) {
        const int32_t rc =
            ymir_bridge_set_core_option(inst, YMIR_OPT_SH2_OVERCLOCK, bad);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        char msg[96];
        std::snprintf(msg, sizeof(msg), "SH-2 overclock %d is refused", bad);
        check(rc == YMIR_ERR_INVALID_ARG, msg);
    }

    check(ymir_bridge_set_core_option(inst, (YmirCoreOption)99, 1) ==
              YMIR_ERR_INVALID_ARG,
          "an unknown option is refused");
    check(ymir_bridge_get_core_option(inst, (YmirCoreOption)99) == -1,
          "an unknown option reads back as -1");

    ymir_bridge_destroy(inst);

    if (failures) {
        std::printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: core options round-trip and refuse bad values\n");
    return 0;
}
