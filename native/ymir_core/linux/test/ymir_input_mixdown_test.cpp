// ymir_input_mixdown_test — proves the two pieces of input plumbing that
// cannot be observed from the app: mouse delta carry, and trigger latching.
//
// Both exist because the host and the SMPC run on unrelated clocks. Testing
// them through a running Saturn would need a BIOS, a disc and a game that
// draws a pointer; testing the arithmetic directly needs none of that.

#include "input_mixdown.hpp"

#include <cstdio>
#include <thread>
#include <vector>

using ymir_bridge::LatchedButton;
using ymir_bridge::MouseAccum;

static int failures = 0;

static void check(bool cond, const char *what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

int main() {
    /* ---- a small movement arrives whole, in one read ---- */
    {
        MouseAccum m;
        m.add(12, -7);
        int16_t x = 0, y = 0;
        m.consume(x, y);
        check(x == 12 && y == -7, "small delta delivered intact");
        check(!m.pending(), "nothing left pending after a small delta");

        m.consume(x, y);
        check(x == 0 && y == 0, "a second read with no motion reports zero");
    }

    /* ---- a fast swipe is handed over across reads, not clamped away ---- */
    {
        MouseAccum m;
        m.add(700, 0);           /* far beyond one report's -256..255 */
        int16_t x = 0, y = 0;
        int32_t total = 0;
        for (int i = 0; i < 4; ++i) {
            m.consume(x, y);
            total += x;
        }
        check(total == 700, "fast swipe delivered in full across reads");
        check(!m.pending(), "swipe fully drained");
    }

    /* ---- negative motion carries the same way ---- */
    {
        MouseAccum m;
        m.add(0, -600);
        int16_t x = 0, y = 0;
        int32_t total = 0;
        for (int i = 0; i < 4; ++i) {
            m.consume(x, y);
            total += y;
        }
        check(total == -600, "negative swipe delivered in full");
    }

    /* ---- one report never exceeds what the wire format carries ---- */
    {
        MouseAccum m;
        m.add(5000, -5000);
        int16_t x = 0, y = 0;
        m.consume(x, y);
        check(x <= MouseAccum::kMax && y >= MouseAccum::kMin,
              "a single report stays inside the reportable range");
    }

    /* ---- a click shorter than one read still reaches the game ---- */
    {
        LatchedButton b;
        b.press();
        b.release();             /* pressed and released between two reads */
        check(b.sample(), "sub-frame click is seen by the next read");
        check(b.sample(), "and by the one after, so a slow poll cannot miss it");
        check(!b.sample(), "then it clears");
    }

    /* ---- a held button stays held for as long as it is held ---- */
    {
        LatchedButton b;
        b.press();
        for (int i = 0; i < 10; ++i) {
            if (!b.sample()) { check(false, "held button stayed held"); break; }
        }
        check(b.peek(), "held button still reads pressed");
        b.release();
        b.sample();
        b.sample();
        check(!b.sample(), "released button clears after its latch expires");
    }

    /* ---- an untouched button never fires ---- */
    {
        LatchedButton b;
        check(!b.sample(), "idle button reports released");
        check(!b.peek(), "idle button peeks released");
    }

    /* ---- nothing is lost when both threads are busy at once ----
     *
     * The producer is the UI thread and the consumer is the emulation thread
     * reading the SMPC. A lost delta here is a pointer that walks away from
     * where the user is aiming, which on a device looks like drift with no
     * cause. Assert the arithmetic instead: everything added comes out. */
    {
        MouseAccum m;
        constexpr int kSteps = 20000;
        std::atomic<bool> done{false};
        std::atomic<long> drained{0};

        std::thread consumer([&] {
            int16_t x = 0, y = 0;
            while (!done.load(std::memory_order_relaxed)) {
                m.consume(x, y);
                drained += x;
            }
            /* final drain, until it stops producing anything */
            do {
                m.consume(x, y);
                drained += x;
            } while (x != 0);
        });

        for (int i = 0; i < kSteps; ++i) m.add(1, 0);
        done.store(true, std::memory_order_relaxed);
        consumer.join();

        check(drained.load() == kSteps,
              "concurrent add/consume loses no movement");
    }

    if (failures) {
        std::printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: mouse delta carry + trigger latching\n");
    return 0;
}
