package com.crownpark.retro_saturn

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

    override fun onCreate(savedInstanceState: Bundle?) {
        unpackAssets()
        super.onCreate(savedInstanceState)
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
            val names = try { assets.list(dir) ?: continue } catch (_: Exception) { continue }
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
