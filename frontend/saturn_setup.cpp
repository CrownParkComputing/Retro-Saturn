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
 * SDL_ShowOpenFileDialog and SDL_ShowOpenFolderDialog are asynchronous: they
 * return at once and call back later, on the main thread, while the ordinary
 * event loop runs. That is exactly the shape wanted here -- the app keeps
 * drawing, keeps answering its close button, and picks the answer up whenever
 * it arrives.
 *
 * On Android these are the Storage Access Framework, so a folder chosen here
 * is granted on its own; the app never asks for access to all files.
 */
namespace {

struct Pick {
    bool        open = false;   /* a dialog is up                        */
    bool        ready = false;  /* an answer is waiting to be taken      */
    std::string path;
};
Pick g_pick;

void pick_cb(void *, const char *const *filelist, int)
{
    g_pick.open = false;
    /* A null list is an error; an empty one is a cancel. Neither is an
     * answer, and neither should overwrite what the user already had. */
    if (filelist && filelist[0]) {
        g_pick.path = filelist[0];
        g_pick.ready = true;
    }
}

} /* namespace */

bool pick_in_progress() { return g_pick.open; }

bool take_pick(std::string &out)
{
    if (!g_pick.ready) return false;
    g_pick.ready = false;
    out = g_pick.path;
    return true;
}

void begin_pick_folder()
{
    if (g_pick.open) return;
    g_pick.open = true;
    SDL_ShowOpenFolderDialog(pick_cb, nullptr, nullptr, nullptr, false);
}

void begin_pick_file()
{
    if (g_pick.open) return;
    g_pick.open = true;
    /* A BIOS dump is a .bin most of the time and a .rom sometimes, but the
     * filter is deliberately loose: somebody's file is called what it is
     * called, and a picker that hides it is a picker they cannot use. */
    static const SDL_DialogFileFilter filters[] = {
        { "Saturn BIOS (*.bin, *.rom)", "bin;rom" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog(pick_cb, nullptr, nullptr, filters, 2, nullptr, false);
}

} /* namespace saturn */
