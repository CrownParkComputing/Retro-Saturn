/*
 * Retro-Saturn — first run, and the bundled demo.
 *
 * Two things a DOS emulator never has to deal with, and they are the whole
 * reason this file exists:
 *
 * A SATURN NEEDS A BIOS, and it is Sega's. We cannot ship one, cannot fetch
 * one, and cannot emulate one -- so the first thing a new user must do is
 * supply a file we are not allowed to give them. That is an awkward first
 * conversation and it deserves a screen of its own rather than an error
 * message.
 *
 * ANDROID DOES NOT GIVE OUT FOLDERS. It grants ONE directory at a time,
 * through the system picker, and only the one the user chose. There is no
 * "give this app storage" any more, and an app that asks for all-files access
 * is an app Play will ask hard questions about. So the wizard asks for two
 * specific folders and says why.
 */
#ifndef SATURN_SETUP_H
#define SATURN_SETUP_H

#include <string>
#include <vector>

namespace saturn {

/* ---- the bundled demo ----
 *
 * Pixel Poppy Pong, a free Saturn homebrew from the SegaXtreme competition.
 * It is here so the app can be seen to work without a single copyrighted
 * file -- which matters to a new user with nothing yet, and matters to an App
 * Store reviewer who has to verify the emulator does something.
 *
 * It still needs a BIOS to boot. Nothing can change that.
 */

/* The demo's .cue, or empty when it did not ship with this build. Looked for
 * beside the executable first, then in the source tree, so a developer build
 * finds it without a packaging step. */
std::string demo_disc_path();

/* What to call it on screen. */
const char *demo_title();

/* ---- where things can be kept ----
 *
 * The folders this platform will actually let the app read, most useful
 * first. On a desktop these are suggestions; on Android they are the app's
 * own directories, which need no grant at all.
 */
std::vector<std::string> candidate_disc_roots();

/*
 * Ask the system for a folder, if this platform has such a thing.
 *
 * Android: opens the document-tree picker, which grants exactly the folder
 * the user chooses and nothing else. Returns false when there is no picker --
 * on a desktop the path is typed, and offering a button that does nothing is
 * worse than offering none.
 */
bool pick_folder_supported();
bool pick_folder(std::string &out);

/* The same, for a single file: the BIOS. */
bool pick_file_supported();
bool pick_file(std::string &out);

} /* namespace saturn */

#endif /* SATURN_SETUP_H */
