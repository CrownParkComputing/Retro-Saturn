/*
 * audio_backend_stub.c — Silent backend used by headless native tests.
 *
 * Drops all samples; exposes get_level() based on a synthetic peak so
 * tests can assert the SCSP callback is firing.
 */
#include "audio_backend.h"

#include <atomic>
#include <cstdint>

struct StubState {
    std::atomic<int32_t> muted{0};
    std::atomic<int32_t> peak{0};
    std::atomic<int32_t> frames_pushed{0};
};

static int32_t stub_start(void *user) {
    (void)user;
    return 0;
}

static void stub_stop(void *user) {
    (void)user;
}

static int32_t stub_push_frames(void *user, const int16_t *frames, int32_t count) {
    auto *st = (StubState *)user;
    int32_t peak = st->peak.load();
    for (int32_t i = 0; i < count * 2; ++i) {
        int32_t v = frames[i] < 0 ? -frames[i] : frames[i];
        if (v > peak) peak = v;
    }
    st->peak.store(peak);
    st->frames_pushed.fetch_add(count);
    return count;
}

static void stub_set_muted(void *user, int32_t muted) {
    ((StubState *)user)->muted.store(muted);
}

static int32_t stub_get_level(void *user) {
    int32_t peak = ((StubState *)user)->peak.load();
    return peak * 100 / 32767;
}

static void stub_destroy(void *user) {
    delete (StubState *)user;
}

extern "C" YmirAudioBackend *ymir_audio_backend_stub_create(void) {
    auto *be = new YmirAudioBackend();
    auto *st = new StubState();
    be->start       = stub_start;
    be->stop        = stub_stop;
    be->push_frames = stub_push_frames;
    be->set_muted   = stub_set_muted;
    be->get_level   = stub_get_level;
    be->destroy     = stub_destroy;
    be->user        = st;
    return be;
}