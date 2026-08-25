/*
 * ymir_bridge.c — Plain-C ABI implementation wrapping ymir::Saturn.
 *
 * Design:
 *   - YmirInstance owns a unique_ptr<ymir::Saturn>, a worker std::thread
 *     that loops RunFrame(), a framebuffer double-buffer (2 slots + atomic
 *     seq), an audio ring buffer (~500ms @ 44.1kHz stereo int16), a mailbox
 *     for synchronous state changes, and a per-port peripheral state.
 *   - All mutating FFI calls take inst->mut and either:
 *       a) operate immediately on state that is safe outside the emulator
 *          thread (peripheral state, audio mute, presentation pause), OR
 *       b) enqueue a request onto the mailbox for the worker thread to
 *          apply at the end of its current frame, then wait on a future.
 *   - The software frame callback writes XRGB8888 pixels into the next
 *     slot and increments seq. Dart reads the most recently completed
 *     slot via ymir_bridge_get_framebuffer().
 *   - The audio sample callback pushes int16 L/R into the ring buffer.
 *     The platform audio backend drains on its own thread/callback.
 *   - Button bits are inverted at the FFI boundary (Ymir reports
 *     1=released, 0=pressed; FFI takes 1=pressed).
 */
#include "ymir_bridge.h"
#include "audio_backend.h"

#include <ymir/ymir.hpp>
#include <ymir/sys/saturn.hpp>
#include <ymir/hw/smpc/smpc.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_port.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_defs.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_report.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_state_common.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_control_pad.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_virtua_gun.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_impl_shuttle_mouse.hpp>
#include <ymir/hw/vdp/vdp.hpp>
#include <ymir/hw/scsp/scsp.hpp>
#include <ymir/media/disc.hpp>
#include <ymir/media/loader/loader.hpp>
#include <ymir/sys/backup_ram.hpp>
#include <ymir/savestate/savestate.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

/* ---- audio ring buffer constants ---- */
constexpr int32_t kAudioSampleRate = 44100;
constexpr int32_t kAudioBufferMs   = 500;
constexpr int32_t kAudioPrebufferMs = 80;

/* ---- framebuffer double-buffer ---- */
struct FrameSlot {
    std::vector<uint32_t> pixels;
    int32_t               width  = 0;
    int32_t               height = 0;
    std::atomic<bool>     writing{false};
};

/* ---- audio ring buffer ---- */
struct AudioRing {
    std::vector<int16_t> buf;          /* interleaved L,R,L,R,... */
    std::atomic<int64_t> readFrame{0}; /* in frames (1 frame = 1 L + 1 R) */
    std::atomic<int64_t> writeFrame{0};
    int32_t              capacity = 0;
    std::mutex           mut;

    /* peak detector */
    std::atomic<int32_t> peakLevel{0};   /* 0..100 */
};

/* ---- peripheral state ---- */
struct PeripheralState {
    YmirPeripheralType type    = YMIR_PERIPHERAL_NONE;
    uint16_t           buttons = 0xFFFF;  /* 1=released (ymir convention) */
    /* virtua gun */
    uint16_t gun_x = 0xFFFF;
    uint16_t gun_y = 0xFFFF;
    bool     gun_trigger = false;
    bool     gun_start   = false;
    bool     gun_reload  = false;
    /* analog pad axes */
    uint8_t analog_lx = 128;
    uint8_t analog_ly = 128;
    uint8_t analog_rx = 128;
    uint8_t analog_ry = 128;
};

/* ---- pending state-change request ---- */
struct Request {
    enum Op { kLoadDisc, kLoadBios, kReset, kLoadInternalBackup, kSaveInternalBackup,
              kLoadSmpc, kSaveSmpc, kSaveState, kLoadState, kSwapState,
              kSetPeripheralType };
    Op                  op;
    char                path[1024] = {};
    int32_t             port       = 1;
    int32_t             hard       = 0;
    int32_t             copyOnWrite = 0;
    YmirPeripheralType  ptype      = YMIR_PERIPHERAL_NONE;
    std::promise<int32_t> done;
};

/* ---- the instance ---- */
struct YmirInstance {
    /* core */
    std::unique_ptr<ymir::Saturn> saturn;

    /* threading */
    std::thread        worker;
    std::atomic<bool>  workerRunning{false};
    std::atomic<bool>  workerShouldStop{false};
    std::mutex         mut;          /* protects all mutating ops */
    std::mutex         mailboxMut;
    std::vector<std::unique_ptr<Request>> mailbox;

    /* framebuffer double-buffer */
    FrameSlot slots[2];
    std::atomic<uint64_t> seq{0};     /* producer increments; consumer reads (seq-1) */

    /* audio */
    std::unique_ptr<YmirAudioBackend> audio;
    AudioRing              ring;
    std::atomic<int32_t>   muted{0};

    /* peripheral state, port 1 + port 2 */
    PeripheralState        ports[2];

    /* presentation */
    std::atomic<int32_t>   presentationPaused{0};

    /* status */
    std::string            statusBuf;
    std::mutex             statusMut;

    /* fps counter */
    std::atomic<int32_t>   fps{0};
};

/* ===================================================================
 *  internal helpers
 * =================================================================== */

static void set_status(YmirInstance *inst, const std::string &s) {
    std::lock_guard<std::mutex> lk(inst->statusMut);
    inst->statusBuf = s;
}

static int32_t translate_fs_err(const std::error_code &ec) {
    if (!ec) return YMIR_OK;
    return YMIR_ERR_FILE_OPEN;
}

/* read whole file into vector<uint8>; empty vector on failure */
static std::vector<uint8_t> read_file(const fs::path &path, int32_t *errOut) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec) { if (errOut) *errOut = YMIR_ERR_FILE_OPEN; return {}; }
    std::vector<uint8_t> out(sz);
    FILE *fp = std::fopen(path.string().c_str(), "rb");
    if (!fp) { if (errOut) *errOut = YMIR_ERR_FILE_OPEN; return {}; }
    size_t got = std::fread(out.data(), 1, sz, fp);
    std::fclose(fp);
    if (got != (size_t)sz) { if (errOut) *errOut = YMIR_ERR_FILE_READ; return {}; }
    if (errOut) *errOut = YMIR_OK;
    return out;
}

static int32_t write_file(const fs::path &path, const void *data, size_t sz) {
    FILE *fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) return YMIR_ERR_FILE_OPEN;
    size_t put = std::fwrite(data, 1, sz, fp);
    std::fclose(fp);
    return (put == sz) ? YMIR_OK : YMIR_ERR_FILE_WRITE;
}

/* ===================================================================
 *  peripheral handling — Ymir's peripheral ports invoke the registered
 *  callback every time the SH-2 reads INTBACK. We use one callback per
 *  port; it consults the per-port state and fills the report.
 * =================================================================== */

static void fill_peripheral_report(YmirInstance *inst, int32_t portIdx,
                                   ymir::peripheral::PeripheralReport &report) {
    using ymir::peripheral::Button;
    auto &ps = inst->ports[portIdx];
    report.type = (ymir::peripheral::PeripheralType)ps.type;
    /* Our buttons mask: 1=released (matches Button semantics).
     * Convert each index to the corresponding Button bit and OR it in
     * if the bit is set in ps.buttons. */
    auto btn = [&](YmirButton idx) -> Button {
        switch (idx) {
        case YMIR_BUTTON_UP:    return Button::Up;
        case YMIR_BUTTON_DOWN:  return Button::Down;
        case YMIR_BUTTON_LEFT:  return Button::Left;
        case YMIR_BUTTON_RIGHT: return Button::Right;
        case YMIR_BUTTON_START: return Button::Start;
        case YMIR_BUTTON_A:     return Button::A;
        case YMIR_BUTTON_B:     return Button::B;
        case YMIR_BUTTON_C:     return Button::C;
        case YMIR_BUTTON_X:     return Button::X;
        case YMIR_BUTTON_Y:     return Button::Y;
        case YMIR_BUTTON_Z:     return Button::Z;
        case YMIR_BUTTON_L:     return Button::L;
        case YMIR_BUTTON_R:     return Button::R;
        default:                return Button::None;
        }
    };
    Button b = Button::None;
    for (int i = 0; i < YMIR_BUTTON_COUNT; ++i) {
        if ((ps.buttons >> i) & 1u) b = b | btn((YmirButton)i);
    }

    switch (ps.type) {
    case YMIR_PERIPHERAL_CONTROL_PAD:
        report.report.controlPad.buttons = b;
        break;
    case YMIR_PERIPHERAL_ANALOG_PAD:
        report.report.analogPad.buttons = b;
        report.report.analogPad.analog   = true;
        report.report.analogPad.x = ps.analog_lx;
        report.report.analogPad.y = ps.analog_ly;
        report.report.analogPad.l = ps.analog_rx;
        report.report.analogPad.r = ps.analog_ry;
        break;
    case YMIR_PERIPHERAL_ARCADE_RACER:
        /* arcade racer uses buttons but with wheel */
        report.report.arcadeRacer.buttons = b;
        report.report.arcadeRacer.wheel   = ps.analog_lx;
        break;
    case YMIR_PERIPHERAL_MISSION_STICK:
        report.report.missionStick.buttons = b;
        report.report.missionStick.sixAxis = true;
        report.report.missionStick.x1 = ps.analog_lx;
        report.report.missionStick.y1 = ps.analog_ly;
        report.report.missionStick.z1 = ps.analog_rx;
        report.report.missionStick.x2 = ps.analog_rx;
        report.report.missionStick.y2 = ps.analog_ry;
        report.report.missionStick.z2 = ps.analog_lx;
        break;
    case YMIR_PERIPHERAL_VIRTUA_GUN:
        /* bool fields here are 1=pressed (NOT inverted like the button mask) */
        report.report.virtuaGun.start   = ps.gun_start;
        report.report.virtuaGun.trigger = ps.gun_trigger;
        report.report.virtuaGun.reload  = ps.gun_reload;
        report.report.virtuaGun.x       = ps.gun_x;
        report.report.virtuaGun.y       = ps.gun_y;
        break;
    case YMIR_PERIPHERAL_SHUTTLE_MOUSE:
        report.report.shuttleMouse.start  = (ps.buttons >> 4) & 1 ? false : true;
        report.report.shuttleMouse.middle = (ps.buttons >> 5) & 1 ? false : true;
        report.report.shuttleMouse.left   = (ps.buttons >> 6) & 1 ? false : true;
        report.report.shuttleMouse.right  = (ps.buttons >> 7) & 1 ? false : true;
        report.report.shuttleMouse.x      = 0; /* mouse deltas — driven by stick X */
        report.report.shuttleMouse.y      = 0;
        break;
    default:
        break;
    }
}

/* ---- Free-standing callback shims. ymir-core's Callback is C-style:
 * void(*)(void* ctx, ...). Each shim casts the context back to YmirInstance*.
 * Stored in a static dispatch table keyed by port index. ---- */

static void peripheral_cb_shim(void *ctx, ymir::peripheral::PeripheralReport &report) {
    auto *inst = (YmirInstance *)ctx;
    /* ctx points at a (portIdx << 32 | inst) packed uint64_t — see below */
    uintptr_t packed = (uintptr_t)ctx;
    int32_t portIdx = (int32_t)(packed >> 32);
    inst = (YmirInstance *)(packed & 0xFFFFFFFFu);
    (void)inst;
    /* ctx is actually inst; we need a way to disambiguate. Use a
     * separate dispatch via a global table indexed by port. */
    static YmirInstance *table[2] = {nullptr, nullptr};
    (void)table;
    /* simpler: caller passes inst as ctx; port comes from a side channel */
}

/* The above approach with packed pointers is awkward. Instead, store the
 * port index inside a small per-port dispatch struct held by YmirInstance.
 * We'll just keep two named functions — one for each port. */

static void peripheral_cb_port0(ymir::peripheral::PeripheralReport &report, void *ctx) {
    fill_peripheral_report((YmirInstance *)ctx, 0, report);
}
static void peripheral_cb_port1(ymir::peripheral::PeripheralReport &report, void *ctx) {
    fill_peripheral_report((YmirInstance *)ctx, 1, report);
}

static void make_peripheral_callback(YmirInstance *inst, int32_t portIdx) {
    auto &pp = (portIdx == 0 ? inst->saturn->SMPC.GetPeripheralPort1()
                             : inst->saturn->SMPC.GetPeripheralPort2());
    auto fn = (portIdx == 0) ? peripheral_cb_port0 : peripheral_cb_port1;
    ymir::peripheral::CBPeripheralReport cbp(inst, fn);
    pp.SetPeripheralReportCallback(cbp);
}

static void install_peripheral(YmirInstance *inst, int32_t portIdx, YmirPeripheralType type) {
    auto &pp = (portIdx == 0 ? inst->saturn->SMPC.GetPeripheralPort1()
                             : inst->saturn->SMPC.GetPeripheralPort2());
    switch (type) {
    case YMIR_PERIPHERAL_NONE:          pp.DisconnectPeripherals(); break;
    case YMIR_PERIPHERAL_CONTROL_PAD:   pp.ConnectControlPad();   break;
    case YMIR_PERIPHERAL_ANALOG_PAD:    pp.ConnectAnalogPad();    break;
    case YMIR_PERIPHERAL_ARCADE_RACER:  pp.ConnectArcadeRacer();  break;
    case YMIR_PERIPHERAL_MISSION_STICK: pp.ConnectMissionStick(); break;
    case YMIR_PERIPHERAL_VIRTUA_GUN:    pp.ConnectVirtuaGun();    break;
    case YMIR_PERIPHERAL_SHUTTLE_MOUSE: pp.ConnectShuttleMouse(); break;
    }
    inst->ports[portIdx].type = type;
    make_peripheral_callback(inst, portIdx);
}

/* ===================================================================
 *  worker thread + mailbox
 * =================================================================== */

static void apply_request(YmirInstance *inst, Request *req) {
    int32_t rc = YMIR_OK;
    try {
        switch (req->op) {
        case Request::kLoadBios: {
            auto data = read_file(req->path, &rc);
            if (rc == YMIR_OK) {
                if (data.size() != ymir::sys::kIPLSize) {
                    rc = YMIR_ERR_BAD_FORMAT;
                } else {
                    /* LoadIPL takes a mutable span. Copy into a writable buffer. */
                    std::array<uint8_t, ymir::sys::kIPLSize> buf;
                    std::memcpy(buf.data(), data.data(), ymir::sys::kIPLSize);
                    inst->saturn->LoadIPL(buf);
                    inst->saturn->Reset(true);
                    set_status(inst, "BIOS loaded");
                }
            }
            break;
        }
        case Request::kLoadDisc: {
            ymir::media::Disc disc;
            auto cb = [](ymir::media::MessageType, const std::string &) {};
            bool ok = ymir::media::LoadDisc(fs::path(req->path), disc, false, cb);
            if (!ok) { rc = YMIR_ERR_BAD_FORMAT; break; }
            inst->saturn->LoadDisc(std::move(disc));
            inst->saturn->Reset(true);
            set_status(inst, std::string("Disc loaded: ") + req->path);
            break;
        }
        case Request::kReset:
            inst->saturn->Reset(req->hard != 0);
            set_status(inst, req->hard ? "Hard reset" : "Soft reset");
            break;
        case Request::kLoadInternalBackup: {
            std::error_code ec;
            inst->saturn->LoadInternalBackupMemoryImage(fs::path(req->path),
                                                       req->copyOnWrite != 0, ec);
            rc = translate_fs_err(ec);
            break;
        }
        case Request::kSaveInternalBackup: {
            auto data = inst->saturn->mem.GetInternalBackupRAM().ReadAll();
            rc = write_file(req->path, data.data(), data.size());
            break;
        }
        case Request::kLoadSmpc: {
            /* ymir-core's SMPC persistent state writes/reads its 25-byte
             * file from a *path* that it manages. Copy the user's bytes
             * into a temp file, ask ymir to load it, then delete the temp. */
            auto data = read_file(req->path, &rc);
            if (rc != YMIR_OK) break;
            if (data.size() != YMIR_SMPC_STATE_SIZE) { rc = YMIR_ERR_BAD_FORMAT; break; }
            fs::path tmp = fs::temp_directory_path() / "ymir_smpc_state.bin";
            if (write_file(tmp, data.data(), data.size()) != YMIR_OK) {
                rc = YMIR_ERR_FILE_WRITE; break;
            }
            std::error_code ec;
            inst->saturn->SMPC.LoadPersistentDataFrom(tmp, ec);
            fs::remove(tmp, ec);
            rc = translate_fs_err(ec);
            break;
        }
        case Request::kSaveSmpc: {
            std::error_code ec;
            auto tmpPath = inst->saturn->SMPC.GetPersistentDataPath();
            (void)tmpPath; /* not used — SMPC writes on SavePersistentDataTo */
            /* SMPC writes via a managed path; instead we round-trip through
             * a temp file: ask SMPC to save to a temp, copy those bytes
             * out, then delete the temp. */
            fs::path tmp = fs::temp_directory_path() / "ymir_smpc_state.bin";
            inst->saturn->SMPC.SavePersistentDataTo(tmp, ec);
            if (ec) { rc = translate_fs_err(ec); break; }
            auto data = read_file(tmp.string().c_str(), &rc);
            fs::remove(tmp, ec);
            if (rc != YMIR_OK) break;
            rc = write_file(req->path, data.data(), data.size());
            break;
        }
        case Request::kSaveState: {
            /* ymir-core's savestate::SaveState is a structured POD with
             * no built-in Serialize/Deserialize (the upstream API
             * deliberately keeps the wire format private). v1 keeps
             * the SaveState in memory only; on-disk save state is a
             * v2 task. */
            set_status(inst, "Save state: not implemented in v1 (use rewind buffer)");
            rc = YMIR_ERR_GENERIC;
            break;
        }
        case Request::kLoadState: {
            rc = YMIR_ERR_GENERIC;
            break;
        }
        case Request::kSwapState: {
            rc = YMIR_ERR_GENERIC;
            break;
        }
        case Request::kSetPeripheralType:
            install_peripheral(inst, req->port - 1, req->ptype);
            break;
        }
    } catch (const std::exception &e) {
        rc = YMIR_ERR_GENERIC;
        set_status(inst, std::string("Error: ") + e.what());
    }
    req->done.set_value(rc);
}

static void drain_mailbox(YmirInstance *inst) {
    std::vector<std::unique_ptr<Request>> pending;
    {
        std::lock_guard<std::mutex> lk(inst->mailboxMut);
        pending.swap(inst->mailbox);
    }
    for (auto &req : pending) apply_request(inst, req.get());
}

static void worker_loop(YmirInstance *inst) {
    using clock = std::chrono::steady_clock;
    auto nextFrame = clock::now() + std::chrono::milliseconds(16);
    int32_t frameCount = 0;
    auto lastFpsTime = clock::now();

    while (!inst->workerShouldStop.load(std::memory_order_acquire)) {
        /* process pending state changes first */
        drain_mailbox(inst);

        /* one emulation frame */
        try {
            inst->saturn->RunFrame();
        } catch (...) {
            set_status(inst, "Emulation exception");
        }
        frameCount++;

        /* update fps once per second */
        auto now = clock::now();
        if (now - lastFpsTime >= std::chrono::seconds(1)) {
            inst->fps.store(frameCount, std::memory_order_relaxed);
            frameCount = 0;
            lastFpsTime = now;
        }

        /* frame pacing — sleep until target time, then advance */
        if (now < nextFrame) std::this_thread::sleep_until(nextFrame);
        nextFrame += std::chrono::milliseconds(16);
        if (nextFrame < clock::now()) nextFrame = clock::now(); /* catch up */
    }
    drain_mailbox(inst);
    inst->workerRunning.store(false, std::memory_order_release);
}

/* enqueue a request and block on its future, with a timeout */
static int32_t enqueue_request(YmirInstance *inst, std::unique_ptr<Request> req,
                               int32_t timeoutMs = 10000) {
    auto fut = req->done.get_future();
    {
        std::lock_guard<std::mutex> lk(inst->mailboxMut);
        inst->mailbox.push_back(std::move(req));
    }
    if (inst->workerRunning.load(std::memory_order_acquire)) {
        auto status = fut.wait_for(std::chrono::milliseconds(timeoutMs));
        if (status != std::future_status::ready) return YMIR_ERR_SNAPSHOT_TIMEOUT;
        return fut.get();
    }
    /* not running — apply inline */
    apply_request(inst, inst->mailbox.back().get());
    return fut.get();
}

/* ===================================================================
 *  FFI exports
 * =================================================================== */

extern "C" {

YmirInstance *ymir_bridge_create(void) {
    auto inst = std::make_unique<YmirInstance>();
    inst->saturn = std::make_unique<ymir::Saturn>();

    /* Fit the internal backup RAM up front.
     *
     * A real Saturn always has its 32 KiB of internal backup memory. ymir-core
     * leaves the container null until an image is loaded, and
     * BackupMemory::Size() dereferences that null (backup_ram.cpp:228) -- so
     * with no NVRAM file on disk, the first auto-save took the process down
     * with a SIGSEGV on the emulation thread, 60 seconds after a disc loaded,
     * in every game. The Dart side had already logged "NVRAM load: false" and
     * carried on, because a missing save file is a normal first run.
     *
     * A later LoadInternalBackupMemoryImage swaps its own mapped container in
     * on success and returns early WITHOUT clearing this one on failure, so
     * fitting it here holds for the life of the instance. SetInternalBackupRAM
     * swaps contents rather than objects, leaving the SH2 bus mapping (bound to
     * the object's address) intact. */
    {
        ymir::bup::BackupMemory bup;
        bup.CreateInMemory(ymir::sys::kInternalBackupRAMSize);
        if (!inst->saturn->mem.SetInternalBackupRAM(std::move(bup))) {
            set_status(inst.get(), "internal backup RAM rejected");
        }
    }

    /* default peripherals — control pad on port 1, nothing on port 2 */
    install_peripheral(inst.get(), 0, YMIR_PERIPHERAL_CONTROL_PAD);
    install_peripheral(inst.get(), 1, YMIR_PERIPHERAL_NONE);

    /* software render callback (C-style: ctx is LAST arg) */
    inst->saturn->VDP.SetSoftwareRenderCallback(
        ymir::vdp::CBSoftwareFrameComplete(
            inst.get(),
            [](uint32 *fb, uint32 w, uint32 h, void *ctx) {
                auto *inst = (YmirInstance *)ctx;
                uint64_t s = inst->seq.load(std::memory_order_acquire);
                int slot = (int)(s % 2);
                auto &fs = inst->slots[slot];
                size_t need = (size_t)w * (size_t)h;
                if ((int)fs.width != w || (int)fs.height != h ||
                    fs.pixels.size() != need) {
                    fs.width = w;
                    fs.height = h;
                    fs.pixels.assign(need, 0);
                }
                fs.writing.store(true, std::memory_order_release);
                std::memcpy(fs.pixels.data(), fb, need * sizeof(uint32_t));
                fs.writing.store(false, std::memory_order_release);
                inst->seq.store(s + 1, std::memory_order_release);
            }));

    /* audio sample callback (C-style: ctx is LAST arg) */
    inst->saturn->SCSP.SetSampleCallback(
        ymir::scsp::CBOutputSample(
            inst.get(),
            [](sint16 left, sint16 right, void *ctx) {
                auto *inst = (YmirInstance *)ctx;
                if (inst->muted.load(std::memory_order_relaxed)) return;
                int32_t cap = inst->ring.capacity;
                if (cap == 0) return;
                int64_t r = inst->ring.readFrame.load(std::memory_order_relaxed);
                int64_t w = inst->ring.writeFrame.load(std::memory_order_relaxed);
                int64_t queued = w - r;
                if (queued >= cap) return;
                int64_t idx = w % cap;
                std::lock_guard<std::mutex> lk(inst->ring.mut);
                inst->ring.buf[idx * 2 + 0] = left;
                inst->ring.buf[idx * 2 + 1] = right;
                inst->ring.writeFrame.store(w + 1, std::memory_order_release);
                int32_t peak = std::max(std::abs((int)left), std::abs((int)right));
                int32_t cur  = inst->ring.peakLevel.load(std::memory_order_relaxed);
                int32_t nw   = std::max(cur, peak * 100 / 32767);
                inst->ring.peakLevel.store(nw, std::memory_order_relaxed);
            }));

    /* audio ring buffer */
    inst->ring.capacity = kAudioSampleRate * kAudioBufferMs / 1000;
    inst->ring.buf.assign(inst->ring.capacity * 2, 0);

    /* audio backend — selected at compile time via #ifdef below */
#if defined(__ANDROID__)
    inst->audio.reset(ymir_audio_backend_android_create_with_bridge(inst.get()));
#elif defined(__APPLE__)
    inst->audio.reset(ymir_audio_backend_ios_create());
#else
    /* Linux: pass the bridge handle so the writer thread can pull
     * frames from the audio ring buffer. */
    inst->audio.reset(ymir_audio_backend_alsa_create_with_bridge(inst.get()));
#endif
    if (inst->audio && inst->audio->start) inst->audio->start(inst->audio->user);

    /* start worker thread */
    inst->workerShouldStop.store(false, std::memory_order_release);
    inst->workerRunning.store(true, std::memory_order_release);
    inst->worker = std::thread(worker_loop, inst.get());

    set_status(inst.get(), "Ready");
    return inst.release();
}

void ymir_bridge_destroy(YmirInstance *inst) {
    if (!inst) return;
    inst->workerShouldStop.store(true, std::memory_order_release);
    if (inst->worker.joinable()) inst->worker.join();
    if (inst->audio) {
        if (inst->audio->stop) inst->audio->stop(inst->audio->user);
        if (inst->audio->destroy) inst->audio->destroy(inst->audio->user);
        inst->audio.reset();
    }
    inst->saturn.reset();
    delete inst;
}

int32_t ymir_bridge_load_bios(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kLoadBios;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_load_disc(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kLoadDisc;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_load_internal_backup_memory(YmirInstance *inst, const char *path,
                                                int32_t copy_on_write) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kLoadInternalBackup;
    req->copyOnWrite = copy_on_write;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_save_internal_backup_memory(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kSaveInternalBackup;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_load_smpc_state(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kLoadSmpc;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_save_smpc_state(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kSaveSmpc;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_run_frame(YmirInstance *inst) {
    if (!inst) return YMIR_ERR_INVALID_HANDLE;
    /* frames are driven by the worker thread; this returns the running fps
     * so callers who want to step manually can know how many frames have
     * completed since last call. */
    return inst->fps.load(std::memory_order_relaxed);
}

void ymir_bridge_reset(YmirInstance *inst, int32_t hard) {
    if (!inst) return;
    auto req = std::make_unique<Request>();
    req->op = Request::kReset;
    req->hard = hard;
    enqueue_request(inst, std::move(req));
}

void ymir_bridge_set_presentation_paused(YmirInstance *inst, int32_t paused) {
    if (!inst) return;
    inst->presentationPaused.store(paused ? 1 : 0, std::memory_order_relaxed);
}

int32_t ymir_bridge_get_presentation_paused(YmirInstance *inst) {
    if (!inst) return 0;
    return inst->presentationPaused.load(std::memory_order_relaxed);
}

void ymir_bridge_set_audio_muted(YmirInstance *inst, int32_t muted) {
    if (!inst) return;
    inst->muted.store(muted ? 1 : 0, std::memory_order_relaxed);
    if (inst->audio && inst->audio->set_muted)
        inst->audio->set_muted(inst->audio->user, muted);
}

int32_t ymir_bridge_get_audio_muted(YmirInstance *inst) {
    if (!inst) return 0;
    return inst->muted.load(std::memory_order_relaxed);
}

int32_t ymir_bridge_get_audio_level(YmirInstance *inst) {
    if (!inst) return 0;
    int32_t v = inst->ring.peakLevel.load(std::memory_order_relaxed);
    /* decay */
    int32_t cur = inst->audio && inst->audio->get_level
                      ? inst->audio->get_level(inst->audio->user) : v;
    /* simple lerp toward v for stability */
    int32_t lvl = (cur * 7 + v * 1) / 8;
    return std::clamp(lvl, 0, 100);
}

int32_t ymir_bridge_get_fps(YmirInstance *inst) {
    if (!inst) return 0;
    return inst->fps.load(std::memory_order_relaxed);
}

const char *ymir_bridge_get_status(YmirInstance *inst) {
    if (!inst) return "";
    inst->statusMut.lock();
    static thread_local std::string out;
    out = inst->statusBuf;
    inst->statusMut.unlock();
    return out.c_str();
}

const uint32_t *ymir_bridge_get_framebuffer(YmirInstance *inst,
                                            int32_t *out_width,
                                            int32_t *out_height) {
    if (!inst || !out_width || !out_height) return nullptr;
    uint64_t s = inst->seq.load(std::memory_order_acquire);
    if (s == 0) { *out_width = 0; *out_height = 0; return nullptr; }
    int slot = (int)((s - 1) % 2);
    auto &fs = inst->slots[slot];
    /* wait until producer is done writing this slot */
    while (fs.writing.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    *out_width  = fs.width;
    *out_height = fs.height;
    return fs.pixels.data();
}

void ymir_bridge_set_virtua_gun_fb_size(YmirInstance *inst,
                                        int32_t width, int32_t height) {
    /* kept for future use — the bridge currently consumes whatever size
     * the VDP callback reports. */
    (void)inst; (void)width; (void)height;
}

void ymir_bridge_set_peripheral_type(YmirInstance *inst, int32_t port,
                                     YmirPeripheralType type) {
    if (!inst || (port != 1 && port != 2)) return;
    auto req = std::make_unique<Request>();
    req->op = Request::kSetPeripheralType;
    req->port = port;
    req->ptype = type;
    enqueue_request(inst, std::move(req));
}

YmirPeripheralType ymir_bridge_get_peripheral_type(YmirInstance *inst, int32_t port) {
    if (!inst || (port != 1 && port != 2)) return YMIR_PERIPHERAL_NONE;
    return inst->ports[port - 1].type;
}

void ymir_bridge_set_pad_button(YmirInstance *inst, int32_t port,
                                int32_t button_index, int32_t pressed) {
    if (!inst || (port != 1 && port != 2)) return;
    if (button_index < 0 || button_index >= YMIR_BUTTON_COUNT) return;
    auto &ps = inst->ports[port - 1];
    uint16_t mask = (uint16_t)(1u << button_index);
    /* FFI: pressed=1, ymir: released=1. Invert here. */
    if (pressed) ps.buttons &= ~mask;
    else         ps.buttons |=  mask;
}

void ymir_bridge_set_virtua_gun_input(YmirInstance *inst, int32_t port,
                                      int32_t x, int32_t y,
                                      int32_t trigger_pressed,
                                      int32_t start_pressed) {
    if (!inst || (port != 1 && port != 2)) return;
    auto &ps = inst->ports[port - 1];
    ps.gun_x = (uint16_t)std::clamp(x, 0, 0xFFFF);
    ps.gun_y = (uint16_t)std::clamp(y, 0, 0xFFFF);
    ps.gun_trigger = trigger_pressed != 0;
    ps.gun_start   = start_pressed   != 0;
}

void ymir_bridge_set_analog_axis(YmirInstance *inst, int32_t port,
                                 int32_t left_x, int32_t left_y,
                                 int32_t right_x, int32_t right_y) {
    if (!inst || (port != 1 && port != 2)) return;
    auto &ps = inst->ports[port - 1];
    ps.analog_lx = (uint8_t)std::clamp(left_x,  0, 255);
    ps.analog_ly = (uint8_t)std::clamp(left_y,  0, 255);
    ps.analog_rx = (uint8_t)std::clamp(right_x, 0, 255);
    ps.analog_ry = (uint8_t)std::clamp(right_y, 0, 255);
}

int32_t ymir_bridge_save_state(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kSaveState;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_load_state(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kLoadState;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

int32_t ymir_bridge_swap_state(YmirInstance *inst, const char *path) {
    if (!inst || !path) return YMIR_ERR_INVALID_ARG;
    auto req = std::make_unique<Request>();
    req->op = Request::kSwapState;
    std::strncpy(req->path, path, sizeof(req->path) - 1);
    return enqueue_request(inst, std::move(req));
}

void ymir_bridge_set_rtc_to_host(YmirInstance *inst, int64_t offsetSeconds) {
    if (!inst) return;
    /* Set the SMPC's RTC to the host's current date+time, optionally
     * shifted (e.g. +9h for JST). Called before LoadIPL so the BIOS
     * Set Clock wizard shows the device's clock. */
    auto dt = util::datetime::host(offsetSeconds);
    inst->saturn->SMPC.GetRTC().SetDateTime(dt);
}

void ymir_bridge_set_persistent_smpc_path(YmirInstance *inst, const char *path) {
    if (!inst || !path) return;
    /* Tell ymir-core where to find the SMPC persistent data file.
     * Must be set before LoadIPL so ymir-core reads it during boot
     * and the BIOS wizard auto-skips. */
    std::error_code ec;
    inst->saturn->SMPC.LoadPersistentDataFrom(path, ec);
    /* ec is intentionally ignored: a missing file is fine (first run). */
}

} /* extern "C" */

/* ---- internal helpers exposed to audio backends (same TU) ---- */
int32_t ymir_bridge_pull_audio(void *inst_v, int16_t *out, int32_t max_frames) {
    auto *inst = (YmirInstance *)inst_v;
    if (!inst || !out || max_frames <= 0) return 0;
    /* Loop the ring buffer transfer until we've filled `max_frames` or
     * the ring runs dry. The earlier "memset remainder with silence"
     * approach in the consumer produced audible pops because half of
     * every AAudio callback period was repeated silence. */
    int32_t total = 0;
    while (total < max_frames) {
        int32_t cap = inst->ring.capacity;
        if (cap == 0) break;
        int64_t r = inst->ring.readFrame.load(std::memory_order_relaxed);
        int64_t w = inst->ring.writeFrame.load(std::memory_order_acquire);
        int64_t queued = w - r;
        if (queued <= 0) break;
        int32_t want = max_frames - total;
        int32_t n = (int32_t)std::min<int64_t>(queued, want);
        std::lock_guard<std::mutex> lk(inst->ring.mut);
        int64_t start = r % cap;
        int64_t end = start + n;
        if (end <= cap) {
            std::memcpy(out + total * 2,
                        inst->ring.buf.data() + start * 2,
                        n * 2 * sizeof(int16_t));
        } else {
            int32_t first = (int32_t)(cap - start);
            std::memcpy(out + total * 2,
                        inst->ring.buf.data() + start * 2,
                        first * 2 * sizeof(int16_t));
            std::memcpy(out + (total + first) * 2,
                        inst->ring.buf.data(),
                        (n - first) * 2 * sizeof(int16_t));
        }
        inst->ring.readFrame.store(r + n, std::memory_order_release);
        total += n;
    }
    /* Decay peak slowly so the UI meter doesn't stick */
    int32_t cur = inst->ring.peakLevel.load(std::memory_order_relaxed);
    if (cur > 0) inst->ring.peakLevel.store(cur - 1, std::memory_order_relaxed);
    return total;
}

int32_t ymir_bridge_get_audio_muted_flag(YmirInstance *inst) {
    return inst ? inst->muted.load(std::memory_order_relaxed) : 0;
}

YmirInstance *ymir_bridge_get_instance_user(YmirAudioBackend *be) {
    /* Helper for backends that store the YmirInstance* as their user. */
    return be ? (YmirInstance *)be->user : nullptr;
}