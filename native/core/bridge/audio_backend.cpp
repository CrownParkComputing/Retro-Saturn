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
    int32_t            periodFrames = 512;
};

/* Pull frames from the bridge's audio ring. Implemented in ymir_bridge.cpp
 * as `ymir_bridge_pull_audio`. */
extern int32_t ymir_bridge_pull_audio(void *inst_v, int16_t *out, int32_t max_frames);

static void alsa_writer_loop(AlsaState *st) {
    const int32_t kFramesPerChunk = st->periodFrames;
    std::vector<int16_t> chunk(kFramesPerChunk * st->channels);

    while (!st->stop.load(std::memory_order_acquire)) {
        int32_t n = 0;
        if (st->bridge && !st->muted.load(std::memory_order_relaxed)) {
            n = ymir_bridge_pull_audio(st->bridge, chunk.data(), kFramesPerChunk);
        }

        if (n <= 0) {
            /*
             * Nothing ready yet. Wait for the emulator, do NOT pad the device
             * with silence.
             *
             * Padding was catastrophic here. snd_pcm_writei only blocks when
             * the device buffer is full, and this buffer is now sized in tens
             * of milliseconds -- but it used to be whatever ALSA felt like
             * handing out, which on the PipeWire plugin was 1,048,576 frames,
             * or twenty-four seconds. So the writer never blocked, spun as
             * fast as it could, and packed the device with silence. Every real
             * sample then queued up behind all of it. That is the delay that
             * was left after the ring was fixed: our ring said 60ms while the
             * device below it held seconds.
             *
             * Waiting instead lets the device pace this thread, which is what
             * a correctly sized buffer is for. If the producer really does
             * stall, the device underruns and snd_pcm_recover patches it --
             * a brief gap, rather than latency that never comes back.
             */
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        snd_pcm_sframes_t wrote = snd_pcm_writei(st->pcm, chunk.data(), n);
        if (st->stop.load(std::memory_order_acquire)) break;
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

        /*
         * Peak detector for the UI meter -- rise and fall both handled here.
         *
         * The fall used to happen in get_level, once per read, which made the
         * meter depend on how often somebody looked at it: this writer runs at
         * a fixed ~43 chunks a second and a vsynced UI reads at whatever the
         * display does, so at a high refresh rate the value decayed to nothing
         * between chunks and the meter read silence over loud music. Decaying
         * where the cadence is known fixes that for every caller.
         */
        int32_t p = st->peak.load(std::memory_order_relaxed);
        p -= p / 4;                     /* ~23 ms per chunk: a natural fall */
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

    /*
     * Size the buffer, do not accept whatever is offered.
     *
     * Only the period was ever requested here, and ALSA is free to pick the
     * buffer; the PipeWire ALSA plugin picks its maximum, which measured at
     * 1,048,576 frames -- 23.8 seconds of audio sitting between us and the
     * speakers. That is the delay, and no amount of care upstream of it
     * helps. Four short periods is 46ms, which is enough to survive ordinary
     * scheduling and small enough to hear as "at the same time".
     */
    snd_pcm_uframes_t periodSize = 512;
    snd_pcm_hw_params_set_period_size_near(st->pcm, params, &periodSize, 0);
    snd_pcm_uframes_t bufferSize = periodSize * 4;
    snd_pcm_hw_params_set_buffer_size_near(st->pcm, params, &bufferSize);

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
    /* What the device actually gave us, which is not always what was asked
     * for, and is the number that matters. */
    snd_pcm_hw_params_get_period_size(params, &periodSize, 0);
    snd_pcm_hw_params_get_buffer_size(params, &bufferSize);
    st->periodFrames = (int32_t)periodSize;
    /* Said once. Pausing closes the device and resuming opens it again, and a
     * line of this on every menu visit is noise. */
    static bool announced = false;
    if (!announced) {
        announced = true;
        std::fprintf(stderr, "ALSA: %u Hz, period %lu, buffer %lu frames (%.0f ms)\n",
                     rate, (unsigned long)periodSize, (unsigned long)bufferSize,
                     bufferSize * 1000.0 / (rate ? rate : 44100));
    }

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
    /* Nothing is playing, so the meter must not go on showing the last thing
     * that did: the writer thread is about to stop and it is the only thing
     * that decays this. */
    st->peak.store(0, std::memory_order_relaxed);
    st->stop.store(true, std::memory_order_release);
    // Pause must discard queued audio immediately. Draining an idle PipeWire
    // PCM can wait indefinitely; dropping also wakes a blocked writer.
    if (st->pcm) snd_pcm_drop(st->pcm);
    if (st->writer.joinable()) st->writer.join();
    if (st->pcm) {
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
    auto *st = (AlsaState *)user;
    /*
     * One value, one unit.
     *
     * This compared `cur`, a 0..100 percentage, against `prev`, the raw
     * 0..32767 magnitude it was derived from. That test is true for anything
     * louder than a whisper, so the peak was reset to zero on very nearly
     * every read and the meter showed silence while the speakers were
     * playing -- which makes it worse than no meter, because it answers "is
     * there any sound?" with a confident no.
     */
    /* A pure read. The writer thread does the decay -- see alsa_writer_loop --
     * because it is the one with a fixed cadence. */
    return st->peak.load(std::memory_order_relaxed) * 100 / 32767;
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
