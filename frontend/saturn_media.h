/*
 * Retro-Saturn — RetroMedia client (https://media.crownparkcomputing.com).
 *
 * Cover art for the shelf, and -- for an administrator account -- the ability
 * to download Saturn discs straight onto the device.
 *
 * Every call here is ASYNCHRONOUS. The frontend draws at 60fps and must never
 * block on the network, so a media_begin_*() call returns immediately and the
 * outcome arrives later through media_poll(), which the frame loop drains.
 *
 * Where there is libcurl the work happens here; everywhere else the calls are
 * stubs and media_available() is false, so the frontend can call them
 * unconditionally and simply not offer the pages.
 */
#ifndef SATURN_MEDIA_H
#define SATURN_MEDIA_H

#include <string>
#include <vector>

namespace saturn {

/** Who is signed in, and what they are allowed to do. */
struct MediaAccount {
    bool        signed_in = false;
    std::string email;
    bool        is_admin  = false;   /* gates game downloads, server-side too */
    int         credits   = 0;
    int         free_remaining = 0;
};

/** One catalogue entry. `preview` is a media-root-relative path, empty when the
 *  title has no artwork; `rom_files` is 0 for non-admins because the server
 *  strips rom media from their responses. */
struct MediaGame {
    std::string slug;
    std::string title;
    std::string preview;
    /** Which kinds of picture this title has, comma separated -- "titles",
     *  "box2d", "boxback", "marquee", "cartridges" and so on. The server
     *  stores them as media/<kind>/<same name>.png, so `preview` plus one of
     *  these names is enough to ask for any of them without a second
     *  request per game. */
    std::string media_types;
    int         rom_files = 0;
    long long   bytes     = 0;
};

/** A decoded, cached image: raw RGBA8 in a file the frontend uploads to a
 *  texture. Decoding happens off the native side, which has no image codec. */
struct MediaArt {
    std::string slug;
    std::string path;
    int         w = 0;
    int         h = 0;
};

enum class MediaOp { None, Status, Login, Logout, Catalogue, Artwork, Download };

struct MediaResult {
    MediaOp     op = MediaOp::None;
    bool        ok = false;
    std::string message;          /* already user-facing; show it verbatim */
    MediaAccount           account;
    std::vector<MediaGame> games;
    MediaArt               art;
    std::string            path;  /* Download: where the game landed */
};

/** False on platforms with no client, so the UI can hide the media pages. */
bool media_available(void);

/**
 * Whether GAME downloads may be offered at all on this platform.
 *
 * Separate from media_available() and deliberately stricter. Artwork is
 * metadata about software the user already owns; shipping a storefront for the
 * games themselves is a different proposition, and on iOS it is one the App
 * Store does not allow. The account's isAdmin flag is a permission check, this
 * is a platform one -- and a permission check is no substitute, because an
 * administrator signing in on iOS must still not see it.
 */
bool media_downloads_available(void);

/* Each begins one operation; exactly one MediaResult follows per call. */
void media_begin_status(void);
void media_begin_login(const std::string &email, const std::string &password);
void media_begin_login_key(const std::string &api_key);
void media_begin_logout(void);
void media_begin_catalogue(const std::string &search, const std::string &letter,
                           bool roms_only);
void media_begin_artwork(const std::string &slug, const std::string &preview);
void media_begin_download(const std::string &slug, const std::string &dest_dir);

/** Take the next completed result. Returns false when none is ready; never
 *  blocks, so it is safe to call every frame. */
bool media_poll(MediaResult &out);

/** Last email used, to prefill the sign-in form. */
std::string media_last_email(void);

/** Live progress of a running download, or empty when nothing is in flight.
 *  Sampled rather than queued -- a 1 GB title is minutes of otherwise silent
 *  work, and the UI wants the latest figure each frame, not every figure. */
std::string media_progress(void);

/** Load a cached RGBA image written by the bridge. Returns false if the file is
 *  missing or malformed. */
bool media_read_art(const std::string &path, int &w, int &h,
                    std::vector<unsigned char> &rgba);

} /* namespace saturn */

#endif /* SATURN_MEDIA_H */
