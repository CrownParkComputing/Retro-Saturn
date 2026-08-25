#include "savestate_io.hpp"

#include <algorithm>

namespace ymir_bridge {

namespace {

void append(std::vector<uint8_t> &out, const void *data, size_t n) {
    const auto *p = static_cast<const uint8_t *>(data);
    out.insert(out.end(), p, p + n);
}

bool take(const uint8_t *&cur, const uint8_t *end, void *dst, size_t n) {
    if ((size_t)(end - cur) < n) return false;
    std::memcpy(dst, cur, n);
    cur += n;
    return true;
}

} // namespace

std::vector<uint8_t> SerializeSaveState(const ymir::savestate::SaveState &st) {
    const auto &report = st.smpc.intback.report;
    const auto &cart = st.scu.cartData;

    SaveStateHeader hdr{};
    std::memcpy(hdr.magic, "YMS1", 4);
    hdr.version = kSaveStateFormatVersion;
    hdr.structSize = kSaveStateStructSize;
    hdr.reportBytes = report.size();
    hdr.cartBytes = cart.size();

    std::vector<uint8_t> out;
    out.reserve(sizeof(hdr) + report.size() + cart.size() + sizeof(st));

    append(out, &hdr, sizeof(hdr));
    if (!report.empty()) append(out, report.data(), report.size());
    if (!cart.empty()) append(out, cart.data(), cart.size());

    // The struct image goes out last, vector headers and all. They are
    // restored, never read back -- see DeserializeSaveState.
    append(out, &st, sizeof(st));
    return out;
}

bool DeserializeSaveState(const uint8_t *bytes, size_t size,
                          ymir::savestate::SaveState &out) {
    if (!bytes) return false;

    const uint8_t *cur = bytes;
    const uint8_t *end = bytes + size;

    SaveStateHeader hdr{};
    if (!take(cur, end, &hdr, sizeof(hdr))) return false;
    if (std::memcmp(hdr.magic, "YMS1", 4) != 0) return false;
    if (hdr.version != kSaveStateFormatVersion) return false;
    // Written by a build whose SaveState was laid out differently. Loading it
    // would put each field's bytes into the wrong field.
    if (hdr.structSize != kSaveStateStructSize) return false;

    std::vector<uint8_t> report(hdr.reportBytes);
    std::vector<uint8_t> cart(hdr.cartBytes);
    if (hdr.reportBytes && !take(cur, end, report.data(), report.size())) return false;
    if (hdr.cartBytes && !take(cur, end, cart.data(), cart.size())) return false;
    if ((size_t)(end - cur) < kSaveStateStructSize) return false;

    // Copy the image in, THEN re-establish the two vectors. Their bytes in the
    // image are a pointer/size/capacity captured at save time; leaving them in
    // place means the first thing done with `out` -- assignment or destruction
    // -- frees an address that was never allocated in this process.
    std::memcpy(&out, cur, kSaveStateStructSize);

    new (&out.smpc.intback.report) std::vector<uint8>();
    new (&out.scu.cartData) std::vector<uint8>();

    out.smpc.intback.report.assign(report.begin(), report.end());
    out.scu.cartData.assign(cart.begin(), cart.end());
    return true;
}

} // namespace ymir_bridge
