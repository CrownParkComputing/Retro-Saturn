#include "audio_backend.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <algorithm>

std::atomic<int> consumed{0};
int32_t ymir_bridge_pull_audio(void *, int16_t *out, int32_t frames)
{
    std::fill(out, out + frames * 2, 12000);
    consumed.fetch_add(frames);
    return frames;
}

template<class Predicate> bool wait_for(Predicate ready)
{
    const auto until = SDL_GetTicks() + 1000;
    while (!ready() && SDL_GetTicks() < until) SDL_Delay(5);
    return ready();
}

int main()
{
    if (!SDL_Init(SDL_INIT_AUDIO)) return 1;
    auto *backend = ymir_audio_backend_ios_create_with_bridge(&consumed);
    bool okay = true;
    for (int cycle = 0; cycle < 3 && okay; ++cycle) {
        backend->set_muted(backend->user, 0);
        const int before = consumed.load();
        okay = backend->start(backend->user) == 0 &&
            wait_for([&] { return consumed.load() > before && backend->get_level(backend->user) > 0; });
        backend->set_muted(backend->user, 1);
        const int mute_at = consumed.load();
        okay = okay && wait_for([&] { return consumed.load() > mute_at && backend->get_level(backend->user) == 0; });
        backend->stop(backend->user);
        const int stopped_at = consumed.load();
        SDL_Delay(40);
        okay = okay && consumed.load() == stopped_at && backend->get_level(backend->user) == 0;
    }
    backend->destroy(backend->user);
    delete backend;
    SDL_Quit();
    std::puts(okay ? "OK: audio consumes PCM, mutes, stops and restarts" : "FAIL: audio lifecycle");
    return okay ? 0 : 1;
}
