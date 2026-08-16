/*
 * audio_backend.h — Platform-agnostic audio backend interface.
 *
 * The bridge owns the SCSP sample callback; it pushes int16 stereo
 * frames into the backend via `push_frames`. The backend drains them
 * to the platform audio device on its own thread/callback.
 */
#ifndef YMIR_AUDIO_BACKEND_H
#define YMIR_AUDIO_BACKEND_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YmirAudioBackend {
    /* Start the audio device. Returns 0 on success, negative on error. */
    int32_t (*start)(void *user);
    /* Stop the audio device. Safe to call even if not started. */
    void    (*stop)(void *user);
    /* Push N frames of int16 stereo interleaved (L,R,L,R,...).
     * Returns the number of frames actually accepted. */
    int32_t (*push_frames)(void *user, const int16_t *frames, int32_t count);
    /* Mute/unmute output without stopping the stream. */
    void    (*set_muted)(void *user, int32_t muted);
    /* Current peak level in 0..100 (smoothed). */
    int32_t (*get_level)(void *user);
    /* Free the backend. Called once on bridge destroy. */
    void    (*destroy)(void *user);

    void    *user;
} YmirAudioBackend;

/* Factory symbols resolved by the bridge at compile time:
 *   ymir_audio_backend_alsa_create       (Linux)
 *   ymir_audio_backend_android_create   (Android)
 *   ymir_audio_backend_ios_create        (iOS/macOS)
 *   ymir_audio_backend_stub_create       (headless tests)
 */
YmirAudioBackend *ymir_audio_backend_alsa_create(void);
YmirAudioBackend *ymir_audio_backend_alsa_create_with_bridge(void *bridge);
YmirAudioBackend *ymir_audio_backend_android_create(void);
YmirAudioBackend *ymir_audio_backend_android_create_with_bridge(void *bridge);
YmirAudioBackend *ymir_audio_backend_ios_create(void);
YmirAudioBackend *ymir_audio_backend_stub_create(void);

#ifdef __cplusplus
}
#endif

#endif /* YMIR_AUDIO_BACKEND_H */