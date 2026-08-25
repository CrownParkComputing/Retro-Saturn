// ymir_peripheral_test — cycle every peripheral type × both ports.
//
// Doesn't load a BIOS so no game actually responds, but the bridge
// must accept every type and round-trip ymir_bridge_get_peripheral_type.

#include "ymir_bridge.h"

#include <array>
#include <cstdio>
#include <thread>

int main(int /*argc*/, char ** /*argv*/) {
    YmirInstance *inst = ymir_bridge_create();
    if (!inst) {
        std::fprintf(stderr, "FAIL: create\n");
        return 1;
    }

    constexpr std::array<YmirPeripheralType, 7> kTypes = {
        YMIR_PERIPHERAL_NONE,
        YMIR_PERIPHERAL_CONTROL_PAD,
        YMIR_PERIPHERAL_ANALOG_PAD,
        YMIR_PERIPHERAL_ARCADE_RACER,
        YMIR_PERIPHERAL_MISSION_STICK,
        YMIR_PERIPHERAL_VIRTUA_GUN,
        YMIR_PERIPHERAL_SHUTTLE_MOUSE,
    };

    for (int32_t port = 1; port <= 2; ++port) {
        for (auto t : kTypes) {
            ymir_bridge_set_peripheral_type(inst, port, t);
            /* give the worker a chance to apply */
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            YmirPeripheralType got = ymir_bridge_get_peripheral_type(inst, port);
            std::printf("port %d -> type %d, get returns %d %s\n",
                        port, (int)t, (int)got,
                        got == t ? "OK" : "MISMATCH");
            if (got != t) {
                ymir_bridge_destroy(inst);
                return 1;
            }

            /* also exercise button/axis setters without crashing */
            for (int b = 0; b < YMIR_BUTTON_COUNT; ++b) {
                ymir_bridge_set_pad_button(inst, port, b, 1);
                ymir_bridge_set_pad_button(inst, port, b, 0);
            }
            ymir_bridge_set_analog_axis(inst, port, 128, 128, 128, 128);
            ymir_bridge_set_virtua_gun_input(inst, port, 100, 100, 1, 0);
            ymir_bridge_set_virtua_gun_input(inst, port, 0xFFFF, 0xFFFF, 0, 0);
            ymir_bridge_set_virtua_gun_state(inst, port, 100, 100, 1, 0, 0);
            ymir_bridge_set_virtua_gun_state(inst, port, 0xFFFF, 0xFFFF, 0, 0, 1);
            ymir_bridge_set_mouse_motion(inst, port, 4, -4);
            ymir_bridge_set_mouse_motion(inst, port, -4, 4);
            for (int mb = YMIR_MOUSE_LEFT; mb <= YMIR_MOUSE_START; ++mb) {
                ymir_bridge_set_mouse_button(inst, port, (YmirMouseButton)mb, 1);
                ymir_bridge_set_mouse_button(inst, port, (YmirMouseButton)mb, 0);
            }
        }
    }

    ymir_bridge_destroy(inst);
    std::printf("OK: all peripheral types × both ports round-tripped\n");
    return 0;
}