#include "saturn_artwork.h"
#include "saturn_saf.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace saturn {
namespace {

std::string stem(const std::string &name)
{
    return name.substr(0, name.find_last_of('.'));
}

std::string key(std::string name)
{
    for (char &c : name) c = (char)SDL_tolower((unsigned char)c);
    return name;
}

} // namespace

bool is_artwork_image(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = key(name.substr(dot));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

void ArtworkLibrary::scan(const std::string &directory, const std::string &tree)
{
    directory_ = directory;
    tree_ = tree;
    files_.clear();
    std::vector<std::string> names;
    if (!tree.empty()) {
        for (const SafEntry &entry : saf_list(tree, "artwork"))
            if (is_artwork_image(entry.name)) names.push_back(entry.name);
    } else if (!directory.empty()) {
        int count = 0;
        char **found = SDL_GlobDirectory(directory.c_str(), nullptr, 0, &count);
        for (int i = 0; found && i < count; ++i) {
            const std::string name = found[i];
            if (name.find('/') != std::string::npos || !is_artwork_image(name)) continue;
            SDL_PathInfo info;
            if (SDL_GetPathInfo((directory + "/" + name).c_str(), &info) &&
                info.type == SDL_PATHTYPE_FILE) names.push_back(name);
        }
        SDL_free(found);
    }
    // Stable preference for PNG when both formats have the same name.
    std::sort(names.begin(), names.end(), [](const auto &a, const auto &b) {
        const bool apng = key(a.substr(a.find_last_of('.'))) == ".png";
        const bool bpng = key(b.substr(b.find_last_of('.'))) == ".png";
        return apng != bpng ? apng : a < b;
    });
    for (const std::string &name : names) files_.try_emplace(key(stem(name)), name);
}

std::string ArtworkLibrary::path_for(const Game &game, const std::string &cache_dir) const
{
    std::string name;
    for (const Disc &disc : game.discs) {
        auto it = files_.find(key(stem(disc.file)));
        if (it != files_.end()) { name = it->second; break; }
    }
    if (name.empty()) {
        auto it = files_.find(key(game.title));
        if (it != files_.end()) name = it->second;
    }
    if (name.empty()) return {};
    if (!tree_.empty()) return saf_stage(tree_, "artwork", name, cache_dir);
    return directory_ + "/" + name;
}

} // namespace saturn
