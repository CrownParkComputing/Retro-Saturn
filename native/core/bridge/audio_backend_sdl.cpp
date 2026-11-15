#include "audio_backend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

// Internal ring-buffer consumer, shared with the Android and ALSA backends.
extern int32_t ymir_bridge_pull_audio(void *bridge, int16_t *out, int32_t frames);

namespace {

struct AudioState {
    void *bridge = nullptr;
    SDL_AudioStream *stream = nullptr;
    std::atomic<bool> muted{false};
    std::atomic<int> peak{0};
};

void render(void *user, SDL_AudioStream *stream, int additional_bytes, int)
{
    auto *state = static_cast<AudioState *>(user);
    std::array<int16_t, 2048> samples;
    while (additional_bytes > 0) {
        const int frames = std::min(additional_bytes / 4, 1024);
        if (frames <= 0) break;
        const int got = state->bridge ? ymir_bridge_pull_audio(state->bridge, samples.data(), frames) : 0;
        std::fill(samples.begin() + got * 2, samples.begin() + frames * 2, 0);
        if (state->muted.load(std::memory_order_relaxed))
            std::fill(samples.begin(), samples.begin() + frames * 2, 0);
        int peak = 0;
        for (int i = 0; i < frames * 2; ++i)
            peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
        state->peak.store(peak, std::memory_order_relaxed);
        if (!SDL_PutAudioStreamData(stream, samples.data(), frames * 4)) break;
        additional_bytes -= frames * 4;
    }
}

int32_t start(void *user)
{
    auto *state = static_cast<AudioState *>(user);
    if (state->stream) return 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
    state->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, state);
    if (!state->stream) { SDL_Log("Saturn audio: %s", SDL_GetError()); return -1; }
    if (!SDL_ResumeAudioStreamDevice(state->stream)) {
        SDL_DestroyAudioStream(state->stream);
        state->stream = nullptr;
        return -1;
    }
    return 0;
}

void stop(void *user)
{
    auto *state = static_cast<AudioState *>(user);
    if (state->stream) {
        SDL_DestroyAudioStream(state->stream);
        state->stream = nullptr;
    }
    state->peak.store(0, std::memory_order_relaxed);
}

int32_t push(void *, const int16_t *, int32_t) { return 0; }
void mute(void *user, int32_t value) { static_cast<AudioState *>(user)->muted.store(value != 0); }
int32_t level(void *user) { return static_cast<AudioState *>(user)->peak.load() * 100 / 32768; }
void destroy(void *user) { stop(user); delete static_cast<AudioState *>(user); }

} // namespace

extern "C" YmirAudioBackend *ymir_audio_backend_ios_create_with_bridge(void *bridge)
{
    auto *state = new AudioState;
    state->bridge = bridge;
    auto *backend = new YmirAudioBackend;
    *backend = {start, stop, push, mute, level, destroy, state};
    return backend;
}

extern "C" YmirAudioBackend *ymir_audio_backend_ios_create(void)
{
    return ymir_audio_backend_ios_create_with_bridge(nullptr);
}
