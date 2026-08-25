// savestate_io.hpp — on-disk format for ymir-core save states.
//
// ymir-core fills a `savestate::SaveState` and offers no serializer of its own:
// the struct is the API, the wire format is left to the front end. That is why
// save/load were stubbed out to YMIR_ERR_GENERIC and Pause/Resume never worked.
//
// The struct is trivially copyable EXCEPT for two heap-backed members --
// `smpc.intback.report` and `scu.cartData`, both std::vector<uint8>. So the
// format is:
//
//   magic "YMS1" | version | sizeof(SaveState) | core layout tag
//   length-prefixed smpc.intback.report
//   length-prefixed scu.cartData
//   raw byte image of the SaveState
//
// The image includes the two vector objects' own bytes (pointer, size,
// capacity). Those are meaningless once written and MUST NOT be copied back
// over live vectors on load -- doing so hands the destructor a stale pointer
// to free. Load therefore re-initialises them before restoring the payloads.
//
// A state written by one build cannot be read by another: the layout is
// whatever the compiler chose that day. The header carries sizeof() and a
// layout tag so a mismatch is refused cleanly instead of being loaded as
// garbage, and kSaveStateStructSize below fails the BUILD if upstream changes
// the struct -- which is the moment to re-check the vector inventory, since a
// newly added heap member would otherwise serialize as a dangling pointer.

#pragma once

#include <ymir/savestate/savestate.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

namespace ymir_bridge {

/// SaveState is about 6 MB -- most of it the CD block's 512 KiB DRAM and the
/// VDP/SCSP state. NEVER declare one as a local.
///
/// The bridge's worker thread gets a 1 MB stack (that is Android's default,
/// and kWorkerStackBytes now pins the same figure everywhere so the host
/// builds hit the same wall). A 6 MB local overflows it -- and it does so in
/// the enclosing function's PROLOGUE, where the whole frame is reserved, not
/// at the line that declares it. One in a `case` branch therefore crashed
/// apply_request on every request, including loading a disc: the app died the
/// moment a game was tapped, with a stack overflow pointing at a function that
/// looked unrelated to save states.
///
/// Allocate through this, and the object lives on the heap where it fits.
[[nodiscard]] inline std::unique_ptr<ymir::savestate::SaveState> MakeSaveState() {
    return std::make_unique<ymir::savestate::SaveState>();
}

/// Bump when the format below changes shape.
inline constexpr uint32_t kSaveStateFormatVersion = 1;

/// Guard against upstream growing a new heap-backed member.
///
/// If this fires, ymir-core's SaveState changed. Re-read it: if the new field
/// is trivially copyable, update the number. If it is a vector, string or
/// pointer, it needs adding to the two payloads below FIRST -- otherwise it
/// would be written as a raw pointer and restored as a dangling one.
inline constexpr size_t kSaveStateStructSize = sizeof(ymir::savestate::SaveState);

static_assert(std::is_trivially_copyable_v<ymir::savestate::SchedulerSaveState> &&
                  std::is_trivially_copyable_v<ymir::savestate::SystemSaveState> &&
                  std::is_trivially_copyable_v<ymir::savestate::SH2SaveState> &&
                  std::is_trivially_copyable_v<ymir::savestate::VDPSaveState> &&
                  std::is_trivially_copyable_v<ymir::savestate::SCSPSaveState> &&
                  std::is_trivially_copyable_v<ymir::savestate::CDBlockSaveState>,
              "a save state component grew a heap-backed member; see the note above");

/// Everything the header records, so a foreign or stale file is refused rather
/// than reinterpreted.
struct SaveStateHeader {
    char     magic[4];      // "YMS1"
    uint32_t version;
    uint64_t structSize;    // sizeof(SaveState) in the writing build
    uint64_t reportBytes;   // smpc.intback.report
    uint64_t cartBytes;     // scu.cartData
};

/// Serialize `st` into a byte buffer.
std::vector<uint8_t> SerializeSaveState(const ymir::savestate::SaveState &st);

/// Rebuild `out` from `bytes`. Returns false if the buffer is truncated, not a
/// save state, or was written by a build with a different layout.
bool DeserializeSaveState(const uint8_t *bytes, size_t size,
                          ymir::savestate::SaveState &out);

} // namespace ymir_bridge
