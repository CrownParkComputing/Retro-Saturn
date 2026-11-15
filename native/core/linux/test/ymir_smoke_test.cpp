// ymir_smoke_test — host-side proof of the C ABI.
//
// Creates a YmirInstance, runs N frames in a busy-wait, asserts that
// the instance survives, the framebuffer shape is sane, and the FPS
// counter advances. Does NOT require a Saturn BIOS — without one,
// ymir-core produces no rendered output but still ticks the emulation
// clock so we can prove the bridge is wired correctly.

#include "ymir_bridge.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char **argv) {
    YmirInstance *inst = ymir_bridge_create();
    if (!inst) {
        std::fprintf(stderr, "FAIL: ymir_bridge_create returned NULL\n");
        return 1;
    }
    std::printf("OK: instance created\n");
    std::printf("    status = %s\n", ymir_bridge_get_status(inst));

    /* Let the worker thread tick for 1 second */
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    int32_t fps = ymir_bridge_get_fps(inst);
    std::printf("    fps after 1.1s = %d\n", fps);
    if (fps <= 0) {
        std::fprintf(stderr, "FAIL: worker thread not producing frames (fps=%d)\n", fps);
        ymir_bridge_destroy(inst);
        return 1;
    }

    /* Without a BIOS, get_framebuffer should return null dims */
    int32_t w = -1, h = -1;
    const uint32_t *fb = ymir_bridge_get_framebuffer(inst, &w, &h);
    std::printf("    framebuffer = %p (%dx%d)\n", (const void *)fb, w, h);
    if (fb != nullptr && (w <= 0 || h <= 0)) {
        std::fprintf(stderr, "FAIL: fb pointer with bad dims\n");
        ymir_bridge_destroy(inst);
        return 1;
    }

    /* Reset */
    ymir_bridge_reset(inst, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::printf("    status after reset = %s\n", ymir_bridge_get_status(inst));

    ymir_bridge_destroy(inst);
    std::printf("OK: instance destroyed cleanly\n");
    return 0;
}