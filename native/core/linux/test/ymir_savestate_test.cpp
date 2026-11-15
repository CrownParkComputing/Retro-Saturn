// ymir_savestate_test — the save state wire format.
//
// Save/load/swap returned YMIR_ERR_GENERIC from the day the bridge was written
// ("not implemented in v1"), so Pause/Resume in the app called a function that
// could only fail. This covers the format that replaced the stub.
//
// The interesting part is not the round trip, it is the two std::vector members
// buried in SaveState. The struct is written as a byte image for drift
// resistance, so those two carry a pointer, size and capacity that mean nothing
// once reloaded. If they are left in place, the first assignment or destruction
// frees an address this process never allocated. These tests fail hard -- with
// a crash, not a bad value -- if that fix-up regresses.

#include "savestate_io.hpp"
#include "ymir_bridge.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

using ymir_bridge::DeserializeSaveState;
using ymir_bridge::SaveStateHeader;
using ymir_bridge::SerializeSaveState;

static int failures = 0;

static void check(bool cond, const char *what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) ++failures;
}

int main() {
    namespace fs = std::filesystem;

    // SaveState is over half a megabyte; keep it off the stack.
    auto src = std::make_unique<ymir::savestate::SaveState>();
    auto dst = std::make_unique<ymir::savestate::SaveState>();

    // Recognisable values in a trivially copyable field, and in both of the
    // heap-backed ones.
    src->cdblockLLE = true;
    src->msh2SpilloverCycles = 0x0123456789ABCDEFull;
    src->smpc.intback.report.assign({1, 2, 3, 4, 5});
    src->smpc.intback.reportOffset = 3;
    src->scu.cartData.assign(4096, 0xA5);

    const auto bytes = SerializeSaveState(*src);
    check(bytes.size() > sizeof(ymir::savestate::SaveState),
          "serialized image carries the struct plus its payloads");

    check(DeserializeSaveState(bytes.data(), bytes.size(), *dst),
          "a freshly written state loads");
    check(dst->cdblockLLE == true, "plain fields survive");
    check(dst->msh2SpilloverCycles == 0x0123456789ABCDEFull,
          "64-bit fields survive intact");
    check(dst->smpc.intback.reportOffset == 3, "fields beside a vector survive");

    check(dst->smpc.intback.report == src->smpc.intback.report,
          "the INTBACK report vector is restored by value");
    check(dst->scu.cartData == src->scu.cartData,
          "the cart data vector is restored by value");

    // Prove the vectors are genuinely this process's own memory, not the
    // saved pointers: reassigning would free a bogus address otherwise.
    dst->smpc.intback.report.assign(64, 0x11);
    dst->scu.cartData.clear();
    dst->scu.cartData.shrink_to_fit();
    check(dst->smpc.intback.report.size() == 64,
          "restored vectors can be reassigned without freeing a stale pointer");

    // Loading twice into the same object must not leak or double-free either.
    check(DeserializeSaveState(bytes.data(), bytes.size(), *dst),
          "a second load into the same object is safe");
    check(dst->scu.cartData.size() == 4096, "and restores the payload again");

    // An empty payload is a normal state, not an edge case to trip over.
    {
        auto empty = std::make_unique<ymir::savestate::SaveState>();
        auto back = std::make_unique<ymir::savestate::SaveState>();
        empty->smpc.intback.report.clear();
        empty->scu.cartData.clear();
        const auto eb = SerializeSaveState(*empty);
        check(DeserializeSaveState(eb.data(), eb.size(), *back),
              "a state with no cart and no report round-trips");
        check(back->scu.cartData.empty(), "and stays empty");
    }

    // ---- refusals: a bad state must be rejected, never reinterpreted ----
    {
        auto bad = bytes;
        bad[0] = 'X';
        check(!DeserializeSaveState(bad.data(), bad.size(), *dst),
              "a file that is not a save state is refused");
    }
    {
        auto bad = bytes;
        SaveStateHeader hdr{};
        std::memcpy(&hdr, bad.data(), sizeof(hdr));
        hdr.version += 1;
        std::memcpy(bad.data(), &hdr, sizeof(hdr));
        check(!DeserializeSaveState(bad.data(), bad.size(), *dst),
              "a state from a future format version is refused");
    }
    {
        // The one that matters after a core update: same format, different
        // struct layout. Every field would land in the wrong place.
        auto bad = bytes;
        SaveStateHeader hdr{};
        std::memcpy(&hdr, bad.data(), sizeof(hdr));
        hdr.structSize += 8;
        std::memcpy(bad.data(), &hdr, sizeof(hdr));
        check(!DeserializeSaveState(bad.data(), bad.size(), *dst),
              "a state from a build with a different layout is refused");
    }
    {
        std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
        check(!DeserializeSaveState(truncated.data(), truncated.size(), *dst),
              "a half-written state is refused rather than read off the end");
    }
    check(!DeserializeSaveState(nullptr, 0, *dst), "a null buffer is refused");

    // ---- through the C ABI, the way the app calls it ----
    {
        YmirInstance *inst = ymir_bridge_create();
        if (!inst) {
            std::fprintf(stderr, "FAIL: create\n");
            return 1;
        }
        const fs::path out = fs::temp_directory_path() / "ymir_state_test.sav";
        fs::remove(out);

        const int32_t saveRc = ymir_bridge_save_state(inst, out.c_str());
        check(saveRc == YMIR_OK, "ymir_bridge_save_state writes a state");
        check(fs::exists(out) && fs::file_size(out) > 0, "and the file has content");

        const int32_t loadRc = ymir_bridge_load_state(inst, out.c_str());
        check(loadRc == YMIR_OK, "ymir_bridge_load_state restores it");

        // A state for another machine must come back as a distinct code, so
        // the UI can say which of the two things went wrong.
        {
            std::vector<uint8_t> junk(4096, 0x7E);
            const fs::path bogus = fs::temp_directory_path() / "ymir_state_junk.sav";
            FILE *f = std::fopen(bogus.c_str(), "wb");
            std::fwrite(junk.data(), 1, junk.size(), f);
            std::fclose(f);
            const int32_t rc = ymir_bridge_load_state(inst, bogus.c_str());
            check(rc == YMIR_ERR_STATE_INCOMPATIBLE,
                  "junk is reported as incompatible, not as a generic failure");
            fs::remove(bogus);
        }

        ymir_bridge_destroy(inst);
        fs::remove(out);
    }

    if (failures) {
        std::printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: save state round trip, vector fix-up and refusals\n");
    return 0;
}
