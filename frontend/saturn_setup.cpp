#include "saturn_setup.h"

#include <SDL3/SDL.h>

#include <vector>

namespace saturn {

namespace {

bool is_file(const std::string &p)
{
    SDL_PathInfo info;
    return !p.empty() && SDL_GetPathInfo(p.c_str(), &info) &&
           info.type == SDL_PATHTYPE_FILE;
}

} /* namespace */

std::string demo_disc_path()
{
    /*
     * Beside the binary first, then the source tree.
     *
     * A packaged build ships the demo next to the executable; a developer
     * build has not been packaged and would otherwise have no demo at all,
     * which is exactly when you most want one to test with. The source-tree
     * path is a fallback, not the arrangement.
     */
    std::vector<std::string> tries;
#if defined(__ANDROID__)
    /* An APK asset has no filesystem path, so it is unpacked to the app's own
     * storage on first run and read from there. */
    if (const char *internal = SDL_GetAndroidInternalStoragePath())
        tries.push_back(std::string(internal) + "/demo/PPPong.cue");
#else
    if (const char *base = SDL_GetBasePath()) {
        tries.push_back(std::string(base) + "demo/PPPong.cue");
        tries.push_back(std::string(base) + "../core/tools/test-roms/homebrew/PPPong.cue");
    }
#endif
    for (const std::string &p : tries)
        if (is_file(p)) return p;
    return std::string();
}

const char *demo_title()
{
    return "Pixel Poppy Pong (homebrew demo)";
}

std::vector<std::string> candidate_disc_roots()
{
    std::vector<std::string> out;
#if defined(__ANDROID__)
    /*
     * The app's OWN external directory, which needs no permission and no
     * grant: Android gives every app one of these and lets a file manager or
     * a USB cable reach it. It is the only folder this app can rely on, so it
     * is offered first and by name.
     */
    if (const char *ext = SDL_GetAndroidExternalStoragePath())
        out.push_back(std::string(ext) + "/Saturn");
#elif defined(__APPLE__)
    /* Documents, because that is the one the Files app shows. */
    if (const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
        out.push_back(std::string(docs) + "Saturn");
#else
    if (const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME))
        out.push_back(std::string(home) + "Saturn");
    if (const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
        out.push_back(std::string(docs) + "Saturn");
#endif
    return out;
}

/*
 * The pickers.
 *
 * SDL3 has SDL_ShowOpenFileDialog and SDL_ShowOpenFolderDialog, and on
 * Android they are backed by the Storage Access Framework -- which is the
 * per-folder grant this app wants rather than all-files access. They are
 * asynchronous, so the caller gets a callback; that is wrapped here into the
 * blocking shape the wizard wants, because the wizard is a modal step and has
 * nothing else to do while the user chooses.
 */
namespace {

struct PickResult {
    bool done = false;
    bool ok = false;
    std::string path;
};

void pick_cb(void *userdata, const char *const *filelist, int)
{
    PickResult *r = (PickResult *)userdata;
    if (filelist && filelist[0]) {
        r->path = filelist[0];
        r->ok = true;
    }
    r->done = true;
}

bool pump_until(PickResult &r)
{
    /* The dialog is the system's, and it runs while this loop pumps events --
     * without which the callback never arrives and the app looks hung. */
    for (int i = 0; i < 60000 && !r.done; ++i) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    return r.ok;
}

} /* namespace */

bool pick_folder_supported() { return true; }

bool pick_folder(std::string &out)
{
    PickResult r;
    SDL_ShowOpenFolderDialog(pick_cb, &r, nullptr, nullptr, false);
    if (!pump_until(r)) return false;
    out = r.path;
    return true;
}

bool pick_file_supported() { return true; }

bool pick_file(std::string &out)
{
    PickResult r;
    /* A BIOS dump is a .bin most of the time and a .rom sometimes, but the
     * filter is deliberately loose: somebody's file is called what it is
     * called, and a picker that hides it is a picker they cannot use. */
    static const SDL_DialogFileFilter filters[] = {
        { "Saturn BIOS (*.bin, *.rom)", "bin;rom" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog(pick_cb, &r, nullptr, filters, 2, nullptr, false);
    if (!pump_until(r)) return false;
    out = r.path;
    return true;
}

} /* namespace saturn */
