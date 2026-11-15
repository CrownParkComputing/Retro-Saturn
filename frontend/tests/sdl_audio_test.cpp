#include "audio_backend.h"
#include "saturn_lifecycle.h"
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
    {
        saturn::AppLifecycle lifecycle(7, [&](bool paused) {
            if (paused) backend->stop(backend->user);
            else backend->start(backend->user);
        });
        backend->set_muted(backend->user, 0);
        auto send = [](Uint32 type, SDL_WindowID window = 7) {
            SDL_Event event{};
            event.type = type;
            event.window.windowID = window;
            return SDL_PushEvent(&event);
        };
        auto silent = [&] {
            const int stopped = consumed.load();
            SDL_Delay(40);
            return consumed.load() == stopped && backend->get_level(backend->user) == 0;
        };
        for (const Uint32 event : {SDL_EVENT_WILL_ENTER_BACKGROUND, SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MINIMIZED}) {
            lifecycle.set_playing(true);
            const int before = consumed.load();
            okay = okay && wait_for([&] { return consumed.load() > before; });
            okay = send(event) && okay;
            // No SDL_PollEvent: audio must already be stopped before the loop resumes.
            okay = silent() && lifecycle.suspended() && okay;
            lifecycle.set_playing(true); // An in-flight UI frame must not restart background audio.
            send(SDL_EVENT_DID_ENTER_FOREGROUND);
            send(SDL_EVENT_WINDOW_FOCUS_GAINED);
            send(SDL_EVENT_WINDOW_RESTORED);
            okay = silent() && !lifecycle.suspended() && lifecycle.take_pause_request() && okay;
        }
        lifecycle.set_playing(true);
        const int before = consumed.load();
        send(SDL_EVENT_WINDOW_MOUSE_LEAVE);
        send(SDL_EVENT_WINDOW_FOCUS_LOST, 99);
        okay = wait_for([&] { return consumed.load() > before; }) && !lifecycle.suspended() && okay;
        lifecycle.set_playing(false);
    }
    backend->destroy(backend->user);
    delete backend;
    SDL_Quit();
    std::puts(okay ? "OK: audio consumes PCM, mutes, stops and restarts" : "FAIL: audio lifecycle");
    return okay ? 0 : 1;
}
