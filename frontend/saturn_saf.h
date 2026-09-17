/*
 * Retro-Saturn — the Android document-tree bridge, as plain C++.
 *
 * Android grants storage a folder at a time, as a `content://` document tree,
 * and ymir opens a disc by path. These calls are the join between the two:
 * they ask the Kotlin side (SafBridge) to grant, lay out, enumerate and -- for
 * the one disc actually being started -- copy into somewhere with a real path.
 *
 * Every function is a no-op returning nothing useful off Android, so the
 * frontend can call them unconditionally and simply not offer the screens.
 */
#ifndef SATURN_SAF_H
#define SATURN_SAF_H

#include <string>
#include <vector>

namespace saturn {

/** Is there a document-tree bridge on this platform at all? */
bool saf_available();

/** One granted folder. */
struct SafTree {
    std::string uri;      /* content://... -- the handle for everything else */
    std::string name;     /* what the system calls it, for showing a person  */
    std::string path;     /* a real path when it has one; empty otherwise    */
};

/** Ask for another folder. Returns at once; the answer appears in trees(). */
void saf_pick();

/** The folders granted so far. */
std::vector<SafTree> saf_trees();

/** Drop a grant. The files are untouched; only our access goes. */
void saf_forget(const std::string &uri);

/** Create bios/, cd/ and saves/ in a tree unless something that plainly is
 *  one of them is already there -- "BIOS" and "Games" count. */
bool saf_ensure_layout(const std::string &uri);

/** What `kind` ("bios", "cd", "saves") is actually called in this tree, which
 *  is not necessarily what we would have called it. */
std::string saf_folder_name(const std::string &uri, const std::string &kind);

/** One file in a granted folder. */
struct SafEntry { std::string name; long long bytes = 0; };

/** What is in one subfolder of a tree; `sub` empty for its root. */
std::vector<SafEntry> saf_list(const std::string &uri, const std::string &sub);

/** Copy one file somewhere with a real path, and answer with it. Already
 *  copied files of the same size return instantly. Empty on failure. */
std::string saf_stage(const std::string &uri, const std::string &sub,
                      const std::string &name, const std::string &dest_dir);

/** How far the running copy has got, 0..100, or -1 when none is running. */
int saf_stage_progress();

} /* namespace saturn */

#endif /* SATURN_SAF_H */
