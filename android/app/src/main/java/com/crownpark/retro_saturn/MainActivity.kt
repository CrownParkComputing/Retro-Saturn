package com.crownpark.retro_saturn

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import org.libsdl.app.SDLActivity
import java.io.File

/**
 * Retro-Saturn.
 *
 * Deliberately thin. The shelf, the settings, the save manager, the light gun
 * and the whole layout live in the native SDL3 + Dear ImGui frontend, so they
 * are shared with the desktop build rather than written twice.
 *
 * What cannot live there is the APK: assets inside it have no filesystem path,
 * and the frontend reads its font, its artwork and the demo disc as ordinary
 * files. So they are unpacked into internal storage on first run, which is
 * also the directory the frontend treats as its base on Android.
 */
class MainActivity : SDLActivity() {

    override fun getLibraries(): Array<String> =
        arrayOf("SDL3", "ymircore", "retrosaturn")

    /** Not libmain.so -- the frontend keeps its own name. */
    override fun getMainSharedObject(): String =
        "${applicationInfo.nativeLibraryDir}/libretrosaturn.so"

    override fun getMainFunction(): String = "SDL_main"

    /** No arguments: the frontend finds its own discs and settings. Passing a
     *  game in from here would put half the launch logic on the Android side,
     *  where the desktop build could not use it. */
    override fun getArguments(): Array<String> = arrayOf()

    companion object {
        private const val REQ_PICK_FOLDER = 0x5A70
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        SafBridge.activity = this
        unpackAssets()
        writeCandidateRoots()
        super.onCreate(savedInstanceState)
    }

    override fun onDestroy() {
        SafBridge.activity = null
        super.onDestroy()
    }

    /**
     * The system folder picker.
     *
     * Called from SafBridge on the UI thread. The old
     * startActivityForResult/onActivityResult pair rather than a modern
     * ActivityResultLauncher, because SDLActivity extends plain
     * android.app.Activity and registerForActivityResult does not exist here.
     */
    fun launchFolderPicker() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or
                     Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                     Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(i, REQ_PICK_FOLDER)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == REQ_PICK_FOLDER) {
            if (resultCode == Activity.RESULT_OK) data?.data?.let { SafBridge.onPicked(it) }
            return
        }
        super.onActivityResult(requestCode, resultCode, data)
    }

    /**
     * Where discs can live, written out for the native side to read.
     *
     * Android has not handed out general storage for years: a folder chosen in
     * the system picker comes back as a content:// URI, which is not a path
     * and cannot be opened by an emulator that mounts a file. Copying is the
     * usual answer and it is the wrong one here -- a Saturn disc is a third of
     * a gigabyte and a shelf of them is tens.
     *
     * The app's own external directories need no permission, ARE real paths,
     * and a file manager or a USB cable can reach them. getExternalFilesDirs
     * returns one per volume, so this finds the SD card as well as internal
     * storage, which is where a library of this size actually wants to be.
     *
     * A file rather than a JNI call because it is read once at startup and
     * never changes: a text file needs no bridge, no thread rules and no
     * lifetime to get wrong.
     */
    private fun writeCandidateRoots() {
        try {
            val roots = getExternalFilesDirs(null)
                .filterNotNull()
                .map { File(it, "Saturn") }
            for (r in roots) r.mkdirs()
            File(filesDir, "roots.txt")
                .writeText(roots.joinToString("\n") { it.absolutePath })
        } catch (_: Exception) {
            /* Without it the frontend falls back to its own guess, which is
             * the primary volume -- a worse list, not a broken app. */
        }
    }

    /**
     * Copy the packaged assets out of the APK.
     *
     * Only when the size differs, so an app that has already been run starts
     * without rewriting twenty megabytes -- but an app that has just been
     * UPDATED does refresh them, which a "copy once" flag would not. Size is a
     * coarse test and a sufficient one: these files change as a set, with the
     * build that produced them.
     */
    private fun unpackAssets() {
        val base = filesDir
        for (dir in arrayOf("assets", "demo")) {
            val names = try { assets.list(dir) ?: emptyArray() } catch (_: Exception) { emptyArray() }
            if (names.isEmpty()) {
                /*
                 * Nothing packaged under this name, so nothing should be left
                 * unpacked under it either. The demo disc was bundled once and
                 * is not any more; without this, an install that had the old
                 * build keeps the 83MB file for ever and the app goes on
                 * offering a demo that new installs do not have.
                 */
                File(base, dir).deleteRecursively()
                continue
            }
            val out = File(base, dir).apply { mkdirs() }
            for (name in names) {
                val dst = File(out, name)
                try {
                    assets.open("$dir/$name").use { input ->
                        val want = input.available().toLong()
                        if (dst.exists() && dst.length() == want) return@use
                        dst.outputStream().use { input.copyTo(it) }
                    }
                } catch (_: Exception) {
                    /* One asset failing is not worth refusing to start over:
                     * the frontend copes with a missing font or demo, and a
                     * silent fallback beats a launcher that will not open. */
                }
            }
        }
    }
}
