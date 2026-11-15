#ifndef SATURN_ARTWORK_H
#define SATURN_ARTWORK_H

#include "saturn_library.h"

#include <map>
#include <string>

namespace saturn {

bool is_artwork_image(const std::string &name);

// One local image per game, named after the disc or its shared multi-disc title.
class ArtworkLibrary {
public:
    void scan(const std::string &directory, const std::string &tree = {});
    std::string path_for(const Game &game, const std::string &cache_dir) const;

private:
    std::string directory_, tree_;
    std::map<std::string, std::string> files_;
};

} // namespace saturn
#endif
