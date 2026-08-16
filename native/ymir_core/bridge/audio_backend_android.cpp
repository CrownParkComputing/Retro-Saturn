/*
 * audio_backend_android.c — AAudio MMAP backend for Android.
 *
 * Uses AAudio's modern low-latency path (API 26+). Spawns a writer
 * thread that pulls frames from the bridge ring and pushes them to
 * the AAudio stream.
 *
 * v1 scaffold: full implementation lands when Android device testing
 * begins. For now compiles and links but exits the writer thread
 * immediately so the bridge doesn't block.
 */
#include "audio_backend.h"

#include <cstdio>

struct AaudioState {
    int placeholder = 0;
};

static int32_t aaudio_start(void *user) {
    (void)user;
    std::fprintf(stderr, "audio_backend_android: AAudio start not yet implemented\n");
    return 0;
}

static void aaudio_stop(void *user) {
    (void)user;
}

static int32_t aaudio_push_frames(void *user, const int16_t *frames, int32_t count) {
    (void)user; (void)frames; (void)count;
    return 0;
}

static void aaudio_set_muted(void *user, int32_t muted) {
    (void)user; (void)muted;
}

static int32_t aaudio_get_level(void *user) {
    (void)user;
    return 0;
}

static void aaudio_destroy(void *user) {
    delete (AaudioState *)user;
}

extern "C" YmirAudioBackend *ymir_audio_backend_android_create(void) {
    auto *be = new YmirAudioBackend();
    auto *st = new AaudioState();
    be->start       = aaudio_start;
    be->stop        = aaudio_stop;
    be->push_frames = aaudio_push_frames;
    be->set_muted   = aaudio_set_muted;
    be->get_level   = aaudio_get_level;
    be->destroy     = aaudio_destroy;
    be->user        = st;
    return be;
}