#include "saturn_library.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace saturn {

namespace {

bool ends_with_ci(const std::string &s, const char *suffix)
{
    const size_t n = SDL_strlen(suffix);
    if (s.size() < n) return false;
    return SDL_strncasecmp(s.c_str() + s.size() - n, suffix, n) == 0;
}

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (isspace((unsigned char)s[b]) || s[b] == '-' || s[b] == '_')) ++b;
    while (e > b && (isspace((unsigned char)s[e - 1]) || s[e - 1] == '-' ||
                     s[e - 1] == '_')) --e;
    return s.substr(b, e - b);
}

std::string strip_extension(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

/* Case-insensitive search, returning the position or npos. */
size_t find_ci(const std::string &hay, const char *needle, size_t from = 0)
{
    if (from >= hay.size()) return std::string::npos;
    const char *p = SDL_strcasestr(hay.c_str() + from, needle);
    return p ? (size_t)(p - hay.c_str()) : std::string::npos;
}

} /* namespace */

bool is_disc_image(const std::string &filename)
{
    /*
     * .cue and .ccd name a sheet whose tracks sit beside them, and those
     * tracks (.bin, .img) must NOT be listed themselves -- a two-track game
     * would otherwise appear three times, twice unplayable. So .bin is
     * deliberately absent: a bare .bin with no sheet is not something the CD
     * block can sensibly mount anyway.
     */
    static const char *const kExt[] = {
        ".cue", ".chd", ".iso", ".ccd", ".mds", ".mdf", ".img", nullptr
    };
    for (const char *const *e = kExt; *e; ++e)
        if (ends_with_ci(filename, *e)) {
            /* .img and .mdf are only meaningful with their sheet beside them,
             * and the sheet is what we list. Accept them only when nothing
             * else in the name suggests they are a track of something. */
            if (ends_with_ci(filename, ".img") || ends_with_ci(filename, ".mdf"))
                return find_ci(filename, "track") == std::string::npos;
            return true;
        }
    return false;
}

int disc_number_of(const std::string &filename, std::string &title)
{
    const std::string base = strip_extension(filename);

    /* Every spelling collections actually use. Ordered longest-first so
     * "Disc 2 of 4" is not matched as "Disc 2" with " of 4" left behind. */
    static const char *const kWords[] = { "disc", "disk", "cd", nullptr };

    for (const char *const *w = kWords; *w; ++w) {
        size_t at = find_ci(base, *w);
        while (at != std::string::npos) {
            size_t p = at + SDL_strlen(*w);
            while (p < base.size() && (base[p] == ' ' || base[p] == '.' ||
                                       base[p] == '_' || base[p] == '-')) ++p;
            if (p < base.size() && isdigit((unsigned char)base[p])) {
                int n = 0;
                const size_t numStart = p;
                while (p < base.size() && isdigit((unsigned char)base[p]))
                    n = n * 10 + (base[p++] - '0');
                (void)numStart;

                /* Swallow a trailing "of 4" and the bracket the whole thing
                 * sat in, so the title is the title and not "Title (". */
                size_t end = p;
                const size_t of = find_ci(base, "of", end);
                if (of != std::string::npos && of <= end + 1) {
                    size_t q = of + 2;
                    while (q < base.size() && base[q] == ' ') ++q;
                    while (q < base.size() && isdigit((unsigned char)base[q])) ++q;
                    end = q;
                }

                size_t open = at;
                while (open > 0 && (base[open - 1] == ' ' || base[open - 1] == '(' ||
                                    base[open - 1] == '[' || base[open - 1] == '-' ||
                                    base[open - 1] == '_')) --open;
                while (end < base.size() && (base[end] == ')' || base[end] == ']' ||
                                             base[end] == ' ')) ++end;

                std::string head = base.substr(0, open);
                std::string tail = end < base.size() ? base.substr(end) : std::string();
                title = trim(head + (tail.empty() ? "" : " " + tail));
                if (title.empty()) title = trim(base);
                return n > 0 ? n : 0;
            }
            at = find_ci(base, *w, at + 1);
        }
    }

    title = trim(base);
    return 0;
}

std::vector<Game> scan_discs(const std::string &root)
{
    std::vector<Game> games;
    if (root.empty()) return games;

    int n = 0;
    char **found = SDL_GlobDirectory(root.c_str(), nullptr,
                                     SDL_GLOB_CASEINSENSITIVE, &n);
    if (!found) return games;

    /* Grouped by title, so a four-disc game is one entry with four discs
     * rather than four entries that each start the same story again. */
    std::map<std::string, Game> byTitle;
    for (int i = 0; i < n && found[i]; ++i) {
        const std::string name = found[i];
        if (SDL_strchr(name.c_str(), '/')) continue;   /* one level only */
        if (!is_disc_image(name)) continue;

        std::string title;
        const int number = disc_number_of(name, title);
        if (title.empty()) title = name;

        Disc d;
        d.path   = root + "/" + name;
        d.file   = name;
        d.number = number;

        Game &g = byTitle[title];
        if (g.title.empty()) {
            g.title = title;
            const char c = (char)SDL_toupper((unsigned char)title[0]);
            g.initial = (c >= 'A' && c <= 'Z') ? c : '#';
        }
        g.discs.push_back(std::move(d));
    }
    SDL_free(found);

    games.reserve(byTitle.size());
    for (auto &kv : byTitle) {
        Game &g = kv.second;
        /* In disc order, so "next disc" means the next one. A game whose
         * files declare no numbers keeps the order the filesystem gave. */
        std::sort(g.discs.begin(), g.discs.end(),
                  [](const Disc &a, const Disc &b) {
                      if (a.number != b.number) return a.number < b.number;
                      return SDL_strcasecmp(a.file.c_str(), b.file.c_str()) < 0;
                  });
        games.push_back(std::move(g));
    }
    std::sort(games.begin(), games.end(), [](const Game &a, const Game &b) {
        return SDL_strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    return games;
}

} /* namespace saturn */
