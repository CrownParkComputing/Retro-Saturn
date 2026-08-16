/*
 * audio_backend.c — ALSA PCM backend for Linux.
 *
 * Spawns a single writer thread that pulls frames from the bridge's
 * AudioRing and writes them to the default ALSA PCM device. 44.1 kHz,
 * 16-bit signed little-endian, stereo. Recovers from underruns via
 * snd_pcm_prepare.
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
    std::atomic<int32_t> muted{0};
    int32_t            sampleRate = 44100;
    int32_t            channels   = 2;
    /* optional level peak (smoothed) */
    std::atomic<int32_t> peak{0};
    /* opaque pointer back to YmirInstance for ymir_bridge_pull_audio */
    void              *bridge     = nullptr;
};

static void alsa_writer_loop(AlsaState *st) {
    constexpr int32_t kFramesPerChunk = 1024;
    std::vector<int16_t> chunk(kFramesPerChunk * st->channels);

    /* Forward decl — defined in ymir_bridge.cpp (same .so when linked together). */
    extern int32_t ymir_bridge_pull_audio(void *inst, int16_t *out, int32_t max_frames);

    while (!st->stop.load(std::memory_order_acquire)) {
        int32_t n = 0;
        if (st->bridge && !st->muted.load(std::memory_order_relaxed)) {
            n = ymir_bridge_pull_audio(st->bridge, chunk.data(), kFramesPerChunk);
        }

        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        snd_pcm_sframes_t wrote = snd_pcm_writei(st->pcm, chunk.data(), n);
        if (wrote < 0) {
            /* underrun or device error — try to recover */
            snd_pcm_recover(st->pcm, (int)wrote, 0);
        } else if (wrote < n) {
            /* short write — advance pointer and try again */
            snd_pcm_writei(st->pcm, chunk.data() + wrote * st->channels, n - (int)wrote);
        }
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
    int32_t peak = ((AlsaState *)user)->peak.load();
    return peak * 100 / 32767;
}

static void alsa_destroy(void *user) {
    alsa_stop(user);
    delete (AlsaState *)user;
}

/* The ALSA writer thread calls ymir_bridge_pull_audio (defined in
 * ymir_bridge.cpp). Since both .cpp files are linked into the same
 * libymircore.so, the linker resolves the symbol at link time. */

extern "C" YmirAudioBackend *ymir_audio_backend_alsa_create(void) {
    auto *be = new YmirAudioBackend();
    auto *st = new AlsaState();
    be->start       = alsa_start;
    be->stop        = alsa_stop;
    be->push_frames = alsa_push_frames;
    be->set_muted   = alsa_set_muted;
    be->get_level   = alsa_get_level;
    be->destroy     = alsa_destroy;
    be->user        = st;
    return be;
}