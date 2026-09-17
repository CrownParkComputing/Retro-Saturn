/*
 * Retro-Saturn — the disc shelf.
 *
 * A Saturn game is a DISC, not a folder, and that one fact shapes everything
 * here. There is no scanning a directory for an executable, no per-title
 * configuration file shipped beside it, no "which of these five programs did
 * you mean" -- there is a disc image, and you put it in the drive.
 *
 * What it does have that a folder of DOS games does not is MULTIPLE discs per
 * game. Panzer Dragoon Saga is four of them, and a shelf that lists
 * "Panzer Dragoon Saga (Disc 1)" through "(Disc 4)" as four unrelated games is
 * a shelf that has misunderstood the machine.
 */
#ifndef SATURN_LIBRARY_H
#define SATURN_LIBRARY_H

#include <string>
#include <vector>

namespace saturn {

/* One disc image on disk. */
struct Disc {
    std::string path;      /* what goes to the bridge                        */
    std::string file;      /* the file's own name, for when the title is bare */
    int         number = 0; /* 1-based disc number, 0 when the name says none */
};

/* One game: its title, and every disc that belongs to it. */
struct Game {
    std::string title;     /* the shared part of the name, cleaned up        */
    std::vector<Disc> discs;
    char initial = '#';

    const Disc *disc(size_t i) const {
        return i < discs.size() ? &discs[i] : nullptr;
    }
    bool multi() const { return discs.size() > 1; }
};

/*
 * Every disc image directly inside [root], grouped into games.
 *
 * One level only. A Saturn collection is a flat folder of images, and
 * recursing turns a game's own extracted files -- or the tracks beside a .cue
 * -- into library entries of their own.
 */
std::vector<Game> scan_discs(const std::string &root);

/*
 * The disc number a filename declares, and the title with that declaration
 * removed. Handles the forms collections actually use:
 *
 *     Title (Disc 2)     Title (Disc 2 of 4)     Title (CD2)
 *     Title [Disc 2]     Title - Disc 2          Title (Disk 2)
 *
 * Returns 0 and leaves [title] alone when the name claims no disc number,
 * which is the common case and must not be mangled.
 */
int disc_number_of(const std::string &filename, std::string &title);

/* True when the extension is one the CD block can mount. */
bool is_disc_image(const std::string &filename);

} /* namespace saturn */

#endif /* SATURN_LIBRARY_H */
