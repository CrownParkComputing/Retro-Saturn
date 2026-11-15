#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <mutex>
#include <utility>

namespace saturn {

// Stop audio immediately, even when SDL suspends the normal event loop.
class AppLifecycle {
public:
    AppLifecycle(SDL_WindowID window, std::function<void(bool)> pause)
        : window_(window), pause_(std::move(pause))
    {
        pause_(true);
        SDL_AddEventWatch(watch, this);
    }
    ~AppLifecycle() { detach(); }
    void detach()
    {
        if (attached_) SDL_RemoveEventWatch(watch, this);
        attached_ = false;
    }

    void set_playing(bool playing)
    {
        std::lock_guard lock(mutex_);
        playing_ = playing && reasons_ == 0 && !pause_requested_;
        apply();
    }
    bool take_pause_request()
    {
        std::lock_guard lock(mutex_);
        return std::exchange(pause_requested_, false);
    }
    bool suspended()
    {
        std::lock_guard lock(mutex_);
        return reasons_ != 0;
    }

private:
    static bool SDLCALL watch(void *data, SDL_Event *event)
    {
        auto &self = *static_cast<AppLifecycle *>(data);
        unsigned reason = 0;
        bool suspend = false;
        switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND: reason = 1; suspend = true; break;
        case SDL_EVENT_DID_ENTER_FOREGROUND: reason = 1; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: reason = 2; suspend = true; break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: reason = 2; break;
        case SDL_EVENT_WINDOW_MINIMIZED: reason = 4; suspend = true; break;
        case SDL_EVENT_WINDOW_RESTORED: reason = 4; break;
        default: return true;
        }
        if (reason != 1 && event->window.windowID != self.window_) return true;
        std::lock_guard lock(self.mutex_);
        if (suspend) {
            self.reasons_ |= reason;
            if (self.playing_) self.pause_requested_ = true;
            self.playing_ = false;
        } else {
            self.reasons_ &= ~reason;
        }
        self.apply();
        return true;
    }
    void apply()
    {
        const bool paused = !playing_ || reasons_ != 0;
        if (paused != paused_) {
            paused_ = paused;
            pause_(paused);
        }
    }
    SDL_WindowID window_;
    std::function<void(bool)> pause_;
    std::mutex mutex_;
    unsigned reasons_ = 0;
    bool playing_ = false;
    bool paused_ = true;
    bool pause_requested_ = false;
    bool attached_ = true;
};

}
