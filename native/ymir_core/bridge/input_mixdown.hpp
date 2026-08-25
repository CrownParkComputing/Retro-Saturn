// input_mixdown.hpp — pure input plumbing shared by the bridge and its
// host-side tests. No ymir-core, no threads, no I/O: everything here is a
// value type with arithmetic on it, so the awkward parts (delta carry,
// trigger latching) can be proven on the host instead of on a device.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace ymir_bridge {

/// Relative mouse movement, host-rate in, SMPC-rate out.
///
/// The host produces deltas whenever a pointer moves, which is neither
/// synchronous with nor as frequent as the SMPC's peripheral read. Ymir's
/// ShuttleMouseReport carries a signed 9-bit-ish range (-256..255) and treats
/// anything outside it as an overflow flag, so a fast flick has to be handed
/// over across several reads rather than clamped away -- clamping loses the
/// distance and the pointer lands short of where the user swiped.
///
/// So: accumulate everything, emit at most what the report can carry, and keep
/// the remainder for the next read.
struct MouseAccum {
    static constexpr int32_t kMin = -256;
    static constexpr int32_t kMax = 255;

    /// Atomic because the two ends run on different threads: the UI thread
    /// adds movement as the pointer moves, and the emulation thread drains it
    /// from inside the SMPC's peripheral read. A plain int here loses whole
    /// deltas when the two overlap, and a lost delta is a pointer that drifts
    /// away from where the user is aiming with no way to get it back.
    std::atomic<int32_t> x{0};
    std::atomic<int32_t> y{0};

    void add(int32_t dx, int32_t dy) {
        addAxis(x, dx);
        addAxis(y, dy);
    }

    /// Take up to one report's worth, leaving the rest pending.
    void consume(int16_t &out_x, int16_t &out_y) {
        out_x = consumeAxis(x);
        out_y = consumeAxis(y);
    }

    bool pending() const {
        return x.load(std::memory_order_relaxed) != 0 ||
               y.load(std::memory_order_relaxed) != 0;
    }

    void clear() {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
    }

private:
    static void addAxis(std::atomic<int32_t> &v, int32_t d) {
        // Bound the pending total. Without this an app left running with a
        // stuck pointer could accumulate unboundedly, and the overflow would
        // arrive as motion minutes later.
        int32_t cur = v.load(std::memory_order_relaxed);
        int32_t next;
        do {
            next = std::clamp(cur + d, kMin * 64, kMax * 64);
        } while (!v.compare_exchange_weak(cur, next, std::memory_order_relaxed));
    }

    static int16_t consumeAxis(std::atomic<int32_t> &v) {
        const int32_t emit =
            std::clamp(v.load(std::memory_order_relaxed), kMin, kMax);
        // fetch_sub rather than a store: movement added between the load and
        // here is kept, not overwritten.
        v.fetch_sub(emit, std::memory_order_relaxed);
        return (int16_t)emit;
    }
};

/// A button that must survive at least one peripheral read.
///
/// A trigger pull is a host event with no relation to the SMPC's read cadence.
/// A fast mouse click -- press and release inside one 16ms frame -- would
/// otherwise be set and cleared between two reads and the game would never see
/// it. Latching for a minimum number of reads costs nothing on a normal press
/// (which spans many reads anyway) and makes the fast one land.
struct LatchedButton {
    static constexpr int32_t kMinReports = 2;

    /// Atomic for the same reason MouseAccum is: pressed on the UI thread,
    /// sampled on the emulation thread.
    std::atomic<bool>    held{false};
    std::atomic<int32_t> remaining{0};

    void press() {
        held.store(true, std::memory_order_relaxed);
        remaining.store(kMinReports, std::memory_order_relaxed);
    }

    void release() { held.store(false, std::memory_order_relaxed); }

    void set(bool pressed) {
        if (pressed) press();
        else release();
    }

    /// Read for one peripheral report, decrementing the latch.
    bool sample() {
        if (held.load(std::memory_order_relaxed)) return true;
        int32_t cur = remaining.load(std::memory_order_relaxed);
        while (cur > 0) {
            if (remaining.compare_exchange_weak(cur, cur - 1,
                                                std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    /// Non-destructive read, for tests and assertions.
    bool peek() const {
        return held.load(std::memory_order_relaxed) ||
               remaining.load(std::memory_order_relaxed) > 0;
    }
};

} // namespace ymir_bridge
