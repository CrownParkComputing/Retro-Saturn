/*
 * audio_backend_android.cpp — AAudio MMAP backend for Android.
 *
 * Uses AAudioStreamBuilder to open a low-latency MMAP output stream
 * (API 28+ fallback to legacy AAudioStream on API 26+). The stream's
 * render callback runs on a high-priority audio thread; it pulls int16
 * stereo frames from the bridge's AudioRing via ymir_bridge_pull_audio
 * and writes them into the AAudio buffer. The bridge's SCSP sample
 * callback fires on the emulator thread and pushes into the ring.
 *
 * v1: legacy AAudio output (not MMAP); full MMAP optimization lands
 * after device perf benchmarking.
 */
#include "audio_backend.h"

#include <aaudio/AAudio.h>

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define LOG_TAG "ymir-aaudio"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

struct AaudioState {
    AAudioStream *stream = nullptr;
    void          *bridge = nullptr;  /* YmirInstance* */
    std::atomic<int> muted{0};
    std::atomic<int> peak{0};
    /* Set by the error callback when the device goes away; a detached
     * reopen thread clears it once a new stream is up. */
    std::atomic<bool> reopening{false};
};

static int32_t aaudio_start(void *user);

/* Disconnects happen in normal use -- headphones unplugged, a Bluetooth
 * speaker going away, a phone call taking the device. Left unhandled the
 * stream simply stops and the game plays silently for the rest of the
 * session, which reads as a bug in the emulator.
 *
 * AAudio forbids rebuilding the stream from this callback, so a detached
 * thread closes the dead stream and opens a fresh one against whatever
 * device the system now routes to. */
static void aaudio_error_cb(AAudioStream *stream, void *user,
                            aaudio_result_t error) {
    (void)stream;
    auto *st = (AaudioState *)user;
    if (error != AAUDIO_ERROR_DISCONNECTED) return;
    bool expected = false;
    if (!st->reopening.compare_exchange_strong(expected, true)) return;
    LOGW("audio device disconnected; reopening on the new route");
    std::thread([st]() {
        if (st->stream) {
            AAudioStream_close(st->stream);
            st->stream = nullptr;
        }
        if (aaudio_start(st) != 0) {
            LOGE("could not reopen audio after disconnect; session is silent");
        }
        st->reopening.store(false);
    }).detach();
}

/* Forward decl — implemented in ymir_bridge.cpp. */
extern int32_t ymir_bridge_pull_audio(void *inst_v, int16_t *out, int32_t max_frames);

/* AAudio render callback — runs on the audio thread. Must be real-time
 * safe (no allocations, no locks, no syscalls). ymir_bridge_pull_audio
 * uses a lock-free ring buffer (atomic counters + per-slot atomic_flag)
 * so this callback is RT-safe. */
static aaudio_data_callback_result_t aaudio_render_cb(
        AAudioStream *stream, void *user, void *audio_data,
        int32_t num_frames) {
    auto *st = (AaudioState *)user;
    int16_t *out = (int16_t *)audio_data;
    int32_t channels = AAudioStream_getChannelCount(stream);
    int32_t want = num_frames;

    int32_t got = 0;
    if (st->bridge && !st->muted.load(std::memory_order_relaxed)) {
        got = ymir_bridge_pull_audio(st->bridge, out, want);
    }

    if (got < want) {
        /* Underrun — fill the rest with silence. */
        std::memset(out + got * channels, 0,
                    (size_t)(want - got) * channels * sizeof(int16_t));
    }

    /* Peak detector — for UI meter (no RT impact since it's a single atomic) */
    int32_t p = st->peak.load(std::memory_order_relaxed);
    for (int32_t i = 0; i < got * channels; ++i) {
        int32_t v = out[i] < 0 ? -out[i] : out[i];
        if (v > p) p = v;
    }
    st->peak.store(p > 32767 ? 32767 : p, std::memory_order_relaxed);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int32_t aaudio_start(void *user) {
    auto *st = (AaudioState *)user;
    if (st->stream) {
        LOGW("stream already open");
        return 0;
    }

    AAudioStreamBuilder *builder = nullptr;
    aaudio_result_t rc = AAudio_createStreamBuilder(&builder);
    if (rc != AAUDIO_OK || !builder) {
        LOGE("AAudio_createStreamBuilder failed: %d", rc);
        return -1;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, 44100);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    /* int16_t frames interleaved */
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, aaudio_render_cb, st);
    AAudioStreamBuilder_setErrorCallback(builder, aaudio_error_cb, st);

    rc = AAudioStreamBuilder_openStream(builder, &st->stream);
    AAudioStreamBuilder_delete(builder);
    if (rc != AAUDIO_OK) {
        LOGE("AAudioStreamBuilder_openStream failed: %d (msg=%s)",
             rc, AAudio_convertResultToText(rc));
        st->stream = nullptr;
        return -1;
    }

    /* Force a fixed 1024-frame buffer so each callback asks for a
     * predictable, modest chunk. AAudio default is auto-sized around
     * 1700+ frames (~40 ms) which is way larger than the emulator's
     * 60 Hz cadence (~735 frames / 16.6 ms), so the producer always
     * underruns. AAudioStream_setBufferSizeInFrames is best-effort —
     * the system may clamp to a min/max. */
    AAudioStream_setBufferSizeInFrames(st->stream, 1024);

    rc = AAudioStream_requestStart(st->stream);
    if (rc != AAUDIO_OK) {
        LOGE("AAudioStream_requestStart failed: %d", rc);
        AAudioStream_close(st->stream);
        st->stream = nullptr;
        return -1;
    }

    LOGI("AAudio stream started: %d Hz, %d ch, %d frames/buf",
         AAudioStream_getSampleRate(st->stream),
         AAudioStream_getChannelCount(st->stream),
         AAudioStream_getBufferSizeInFrames(st->stream));
    return 0;
}

static void aaudio_stop(void *user) {
    auto *st = (AaudioState *)user;
    if (st->stream) {
        AAudioStream_requestStop(st->stream);
        AAudioStream_close(st->stream);
        st->stream = nullptr;
    }
}

static int32_t aaudio_push_frames(void *user, const int16_t *frames, int32_t count) {
    /* The AAudio render callback does the pulling; this is a no-op for
     * compatibility with the bridge SCSP callback path. */
    (void)user; (void)frames; (void)count;
    return 0;
}

static void aaudio_set_muted(void *user, int32_t muted) {
    ((AaudioState *)user)->muted.store(muted ? 1 : 0);
}

static int32_t aaudio_get_level(void *user) {
    int32_t peak = ((AaudioState *)user)->peak.load(std::memory_order_relaxed);
    int32_t cur = peak * 100 / 32767;
    return cur;
}

static void aaudio_destroy(void *user) {
    aaudio_stop(user);
    delete (AaudioState *)user;
}

extern "C" YmirAudioBackend *ymir_audio_backend_android_create_with_bridge(void *bridge) {
    auto *be = new YmirAudioBackend();
    auto *st = new AaudioState();
    st->bridge = bridge;
    be->start       = aaudio_start;
    be->stop        = aaudio_stop;
    be->push_frames = aaudio_push_frames;
    be->set_muted   = aaudio_set_muted;
    be->get_level   = aaudio_get_level;
    be->destroy     = aaudio_destroy;
    be->user        = st;
    return be;
}

extern "C" YmirAudioBackend *ymir_audio_backend_android_create(void) {
    return ymir_audio_backend_android_create_with_bridge(nullptr);
}