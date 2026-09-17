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
    /* An APK asset has no filesystem path, so MainActivity unpacks it to the
     * app's own storage on first run and it is read from there. */
    if (const char *internal = SDL_GetAndroidInternalStoragePath()) {
        tries.push_back(std::string(internal) + "/demo/PPPong.cue");
        tries.push_back(std::string(internal) + "/demo/PPPong.bin");
    }
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
     * The app's OWN external directories, which need no permission and no
     * grant: Android gives every app one per storage volume and lets a file
     * manager or a USB cable reach them. They are the only folders this app
     * can rely on, and crucially they are real paths -- a folder chosen in the
     * system picker is a content:// URI, which an emulator that mounts a file
     * cannot open at all.
     *
     * MainActivity writes the list, because only Java can ask for it and the
     * SD card is the one most people want for a library this size. SDL knows
     * about the primary volume only, so it is the fallback rather than the
     * answer.
     */
    if (const char *internal = SDL_GetAndroidInternalStoragePath()) {
        const std::string list = std::string(internal) + "/roots.txt";
        if (SDL_IOStream *in = SDL_IOFromFile(list.c_str(), "rb")) {
            const Sint64 size = SDL_GetIOSize(in);
            if (size > 0 && size < 8192) {
                std::string text((size_t)size, '\0');
                if (SDL_ReadIO(in, text.data(), text.size()) == text.size()) {
                    size_t pos = 0;
                    while (pos < text.size()) {
                        size_t eol = text.find('\n', pos);
                        if (eol == std::string::npos) eol = text.size();
                        std::string line = text.substr(pos, eol - pos);
                        pos = eol + 1;
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                            line.pop_back();
                        if (!line.empty()) out.push_back(line);
                    }
                }
            }
            SDL_CloseIO(in);
        }
    }
    if (out.empty()) {
        if (const char *ext = SDL_GetAndroidExternalStoragePath())
            out.push_back(std::string(ext) + "/Saturn");
    }
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

bool pickers_usable()
{
#if defined(__ANDROID__)
    /*
     * No.
     *
     * SDL's Android dialog backend answers SDL_Unsupported() for a folder, and
     * for a file it returns the content:// URI the system picker produced.
     * Neither is something fopen() can take, so a "Browse..." button there is
     * a button that appears to do nothing -- which is worse than not offering
     * one and saying where files should go instead.
     */
    return false;
#else
    return true;
#endif
}

/*
 * A BIOS found rather than chosen.
 *
 * With no usable picker on Android, the way in is to put the file in the discs
 * folder like everything else -- so look for it there. 512 KiB exactly is the
 * strong signal: that is the size of every Saturn IPL and almost nothing else
 * in a folder of disc images is that size to the byte.
 */
std::string find_bios_in(const std::string &dir)
{
    if (dir.empty()) return std::string();
    int n = 0;
    char **found = SDL_GlobDirectory(dir.c_str(), nullptr,
                                     SDL_GLOB_CASEINSENSITIVE, &n);
    if (!found) return std::string();

    std::string best;
    for (int i = 0; i < n && found[i]; ++i) {
        const std::string name = found[i];
        if (name.find('/') != std::string::npos) continue;
        const std::string full = dir + "/" + name;
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(full.c_str(), &info)) continue;
        if (info.type != SDL_PATHTYPE_FILE) continue;
        if (info.size != 512 * 1024) continue;
        /* Prefer one that says what it is, but take any 512 KiB file: people
         * name their dumps all sorts of things. */
        if (SDL_strcasestr(name.c_str(), "bios") ||
            SDL_strcasestr(name.c_str(), "ipl")) { best = full; break; }
        if (best.empty()) best = full;
    }
    SDL_free(found);
    return best;
}

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
