/*
 * audio_backend.cpp — ALSA PCM backend for Linux.
 *
 * Spawns a single writer thread that pulls int16 stereo frames from
 * the bridge's AudioRing (via ymir_bridge_pull_audio) and writes them
 * to the default ALSA PCM device. 44.1 kHz, 16-bit signed little-endian,
 * stereo. Recovers from underruns via snd_pcm_recover.
 *
 * The YmirInstance* is passed in via `bridge` so the writer thread
 * can pull frames. The bridge is not owned by the backend; the bridge
 * outlives us and is destroyed separately.
 */
#include "audio_backend.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

struct AlsaState {
    snd_pcm_t         *pcm        = nullptr;
    std::thread        writer;
    std::atomic<bool>  stop{false};
    std::atomic<int>   muted{0};
    std::atomic<int>   peak{0};
    void              *bridge     = nullptr;   /* YmirInstance* */
    int32_t            sampleRate = 44100;
    int32_t            channels   = 2;
};

/* Pull frames from the bridge's audio ring. Implemented in ymir_bridge.cpp
 * as `ymir_bridge_pull_audio`. */
extern int32_t ymir_bridge_pull_audio(void *inst_v, int16_t *out, int32_t max_frames);

static void alsa_writer_loop(AlsaState *st) {
    constexpr int32_t kFramesPerChunk = 1024;
    std::vector<int16_t> chunk(kFramesPerChunk * st->channels);

    while (!st->stop.load(std::memory_order_acquire)) {
        int32_t n = 0;
        if (st->bridge && !st->muted.load(std::memory_order_relaxed)) {
            n = ymir_bridge_pull_audio(st->bridge, chunk.data(), kFramesPerChunk);
        }

        if (n <= 0) {
            /* No data ready — write one period of silence then loop.
             * ALSA's start_threshold will keep the device quiet. */
            std::memset(chunk.data(), 0, kFramesPerChunk * st->channels * sizeof(int16_t));
            n = kFramesPerChunk;
        }

        snd_pcm_sframes_t wrote = snd_pcm_writei(st->pcm, chunk.data(), n);
        if (wrote < 0) {
            /* underrun or device error — try to recover */
            int rc = snd_pcm_recover(st->pcm, (int)wrote, 0);
            if (rc < 0) {
                std::fprintf(stderr, "ALSA recover failed: %s\n", snd_strerror(rc));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            snd_pcm_prepare(st->pcm);
        } else if (wrote < n) {
            snd_pcm_writei(st->pcm, chunk.data() + wrote * st->channels, n - (int)wrote);
        }

        /* Peak detector — track the max sample magnitude for the UI meter */
        int32_t p = st->peak.load(std::memory_order_relaxed);
        for (int32_t i = 0; i < n * st->channels; ++i) {
            int32_t v = chunk[i] < 0 ? -chunk[i] : chunk[i];
            if (v > p) p = v;
        }
        st->peak.store(p > 32767 ? 32767 : p, std::memory_order_relaxed);
    }
}

static int32_t alsa_start(void *user) {
    auto *st = (AlsaState *)user;
    int err = snd_pcm_open(&st->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::fprintf(stderr, "ALSA open failed: %s\n", snd_strerror(err));
        return -1;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(st->pcm, params);

    snd_pcm_hw_params_set_access(st->pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(st->pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(st->pcm, params, st->channels);

    unsigned int rate = (unsigned int)st->sampleRate;
    snd_pcm_hw_params_set_rate_near(st->pcm, params, &rate, 0);

    snd_pcm_uframes_t periodSize = 1024;
    snd_pcm_hw_params_set_period_size_near(st->pcm, params, &periodSize, 0);

    err = snd_pcm_hw_params(st->pcm, params);
    if (err < 0) {
        std::fprintf(stderr, "ALSA hw_params failed: %s\n", snd_strerror(err));
        snd_pcm_close(st->pcm);
        st->pcm = nullptr;
        return -1;
    }

    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(st->pcm, sw);
    snd_pcm_sw_params_set_start_threshold(st->pcm, sw, periodSize);
    snd_pcm_sw_params_set_avail_min(st->pcm, sw, periodSize);

    err = snd_pcm_prepare(st->pcm);
    if (err < 0) {
        std::fprintf(stderr, "ALSA prepare failed: %s\n", snd_strerror(err));
        snd_pcm_close(st->pcm);
        st->pcm = nullptr;
        return -1;
    }

    st->stop.store(false);
    st->writer = std::thread(alsa_writer_loop, st);
    return 0;
}

static void alsa_stop(void *user) {
    auto *st = (AlsaState *)user;
    st->stop.store(true, std::memory_order_release);
    if (st->writer.joinable()) st->writer.join();
    if (st->pcm) {
        snd_pcm_drain(st->pcm);
        snd_pcm_close(st->pcm);
        st->pcm = nullptr;
    }
}

static int32_t alsa_push_frames(void *user, const int16_t *frames, int32_t count) {
    /* The ALSA writer thread does the draining; this is a no-op for
     * compatibility with the bridge SCSP callback path. */
    (void)user; (void)frames; (void)count;
    return 0;
}

static void alsa_set_muted(void *user, int32_t muted) {
    ((AlsaState *)user)->muted.store(muted ? 1 : 0);
}

static int32_t alsa_get_level(void *user) {
    int32_t peak = ((AlsaState *)user)->peak.load(std::memory_order_relaxed);
    /* Decay toward 0 each call so the UI meter falls when audio drops. */
    int32_t cur = peak * 100 / 32767;
    int32_t prev = ((AlsaState *)user)->peak.load();
    if (cur < prev / 2) {
        ((AlsaState *)user)->peak.store(0, std::memory_order_relaxed);
    }
    return cur;
}

static void alsa_destroy(void *user) {
    alsa_stop(user);
    delete (AlsaState *)user;
}

/* Factory with bridge handle wired in. ymir_bridge.cpp calls this. */
extern "C" YmirAudioBackend *ymir_audio_backend_alsa_create_with_bridge(void *bridge) {
    auto *be = new YmirAudioBackend();
    auto *st = new AlsaState();
    st->bridge = bridge;
    be->start       = alsa_start;
    be->stop        = alsa_stop;
    be->push_frames = alsa_push_frames;
    be->set_muted   = alsa_set_muted;
    be->get_level   = alsa_get_level;
    be->destroy     = alsa_destroy;
    be->user        = st;
    return be;
}

/* Backwards-compat factory used when no bridge is available (host tests). */
extern "C" YmirAudioBackend *ymir_audio_backend_alsa_create(void) {
    return ymir_audio_backend_alsa_create_with_bridge(nullptr);
}