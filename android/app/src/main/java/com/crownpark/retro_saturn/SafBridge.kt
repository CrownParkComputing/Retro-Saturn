package com.crownpark.retro_saturn

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import java.io.File

/**
 * Storage Access Framework bridge.
 *
 * Android will not hand an app broad file access any more -- MANAGE_EXTERNAL_
 * STORAGE is reserved for file managers -- so a library kept anywhere but the
 * app's own directories is reachable only as a *document tree* the user
 * explicitly grants. More than one may be granted: a card full of discs and a
 * folder of BIOS dumps are two different places and there is no reason to make
 * somebody merge them.
 *
 * That leaves a problem the emulator cannot solve on its own. SAF yields
 * `content://` URIs and ymir opens a disc BY PATH; there is no
 * LoadDisc("content://..."). So this does three separate jobs:
 *
 *   1. GRANT and remember trees, and lay out `bios`, `cd` and `saves` inside
 *      each one so there is an obvious place to put things.
 *   2. ENUMERATE over SAF, which is all the shelf needs -- no real path is
 *      required to show a list of names.
 *   3. STAGE the one disc being started into the app's own directory, which
 *      IS a real path. Cached by name and size, so a game is copied once and
 *      started instantly ever after.
 *
 * With one exception that matters: a tree that already lives inside the app's
 * own external storage is used by its real path directly. Nothing is copied,
 * because nothing needs to be.
 *
 * Everything here is called from native code, so the methods are static and
 * take and return only strings and primitives.
 */
object SafBridge {

    private const val PREFS = "retro_saturn_saf"
    private const val KEY   = "trees"

    /** Set by MainActivity so the bridge can reach a Context and the picker. */
    @Volatile @JvmStatic var activity: MainActivity? = null

    private fun ctx(): Context? = activity?.applicationContext

    /* ------------------------------------------------------------------ */
    /* Grants                                                              */
    /* ------------------------------------------------------------------ */

    /** Launch the system folder picker. Returns at once; the grant arrives
     *  asynchronously, so native code polls trees(). */
    @JvmStatic
    fun pick() {
        val a = activity ?: return
        a.runOnUiThread { a.launchFolderPicker() }
    }

    /** Called by MainActivity when the user has chosen one. */
    fun onPicked(uri: Uri) {
        val c = ctx() ?: return
        try {
            c.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (_: Exception) {
            /* Some providers refuse to persist. The grant still works for this
             * run, so remember it anyway rather than discarding a folder the
             * user just chose. */
        }
        val s = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val have = s.getStringSet(KEY, emptySet())!!.toMutableSet()
        have.add(uri.toString())
        s.edit().putStringSet(KEY, have).apply()
        ensureLayout(uri.toString())
    }

    /**
     * The granted trees, newline separated, as `uri<TAB>display name`.
     *
     * Grants that no longer hold are dropped rather than listed: a card that
     * has been reformatted, or a folder deleted, would otherwise sit in the
     * settings for ever looking like a choice.
     */
    @JvmStatic
    fun trees(): String {
        val c = ctx() ?: return ""
        val s = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val saved = s.getStringSet(KEY, emptySet())!!
        val live = c.contentResolver.persistedUriPermissions.map { it.uri.toString() }.toSet()
        val out = StringBuilder()
        val keep = mutableSetOf<String>()
        for (u in saved) {
            val uri = Uri.parse(u)
            val doc = try { DocumentFile.fromTreeUri(c, uri) } catch (_: Exception) { null }
            if (doc == null || !doc.canRead()) continue
            if (live.isNotEmpty() && u !in live && !doc.canRead()) continue
            keep.add(u)
            out.append(u).append('\t').append(doc.name ?: u).append('\n')
        }
        if (keep != saved) s.edit().putStringSet(KEY, keep).apply()
        return out.toString()
    }

    /** Forget one grant. The files are untouched; only our access is dropped. */
    @JvmStatic
    fun forget(treeUri: String) {
        val c = ctx() ?: return
        val s = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val have = s.getStringSet(KEY, emptySet())!!.toMutableSet()
        have.remove(treeUri)
        s.edit().putStringSet(KEY, have).apply()
        try {
            c.contentResolver.releasePersistableUriPermission(
                Uri.parse(treeUri),
                Intent.FLAG_GRANT_READ_URI_PERMISSION or
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        } catch (_: Exception) { }
    }

    /**
     * What each of our three folders may already be called.
     *
     * People have had these libraries for years and they are not called what
     * this app would have called them: a folder of Saturn discs is as likely
     * to be "Games" or "roms" as "cd", and "BIOS" is almost never "bios".
     * Creating our own names beside theirs would leave two empty folders and a
     * library the app cannot see, so an existing one under any of these names
     * is used as it stands.
     *
     * Case-insensitively, because "BIOS" and "bios" are the same folder to
     * every person who has ever looked at one.
     */
    private val ALIASES = mapOf(
        "bios"  to listOf("bios", "bios roms", "system"),
        "cd"    to listOf("cd", "cds", "games", "roms", "discs", "disks", "iso", "isos"),
        "saves" to listOf("saves", "save", "saveram", "savedata", "backup"),
    )

    /**
     * An existing child folder matching any alias for `kind`, or null.
     *
     * The one with something in it wins. Order alone is not enough: this app
     * creates "cd" when it finds nothing, and an empty "cd" from an earlier
     * run would otherwise beat the "Games" folder holding the whole library,
     * for ever, because it exists and comes first in the list.
     */
    private fun findFolder(root: DocumentFile, kind: String): DocumentFile? {
        val want = ALIASES[kind] ?: listOf(kind)
        val children = try { root.listFiles() } catch (_: Exception) { return null }
        var firstExisting: DocumentFile? = null
        for (alias in want) {
            for (f in children) {
                if (!f.isDirectory) continue
                if (f.name?.equals(alias, ignoreCase = true) != true) continue
                if (firstExisting == null) firstExisting = f
                val inside = try { f.listFiles() } catch (_: Exception) { emptyArray() }
                if (inside.isNotEmpty()) return f
            }
        }
        return firstExisting
    }

    /**
     * Make `bios`, `cd` and `saves` inside a granted tree, unless something
     * that plainly IS one of them is already there.
     *
     * Somewhere obvious to put things beats a folder that works but whose
     * shape nobody can guess. Existing folders are left exactly as they are --
     * this creates, it never renames, moves or tidies.
     */
    @JvmStatic
    fun ensureLayout(treeUri: String): Boolean {
        val c = ctx() ?: return false
        val root = try { DocumentFile.fromTreeUri(c, Uri.parse(treeUri)) } catch (_: Exception) { null }
            ?: return false
        if (!root.canWrite()) return false
        for (kind in ALIASES.keys) {
            if (findFolder(root, kind) == null) root.createDirectory(kind)
        }
        return true
    }

    /** What `kind` is actually called inside this tree, for showing a person. */
    @JvmStatic
    fun folderName(treeUri: String, kind: String): String {
        val c = ctx() ?: return kind
        val root = try { DocumentFile.fromTreeUri(c, Uri.parse(treeUri)) } catch (_: Exception) { null }
            ?: return kind
        return findFolder(root, kind)?.name ?: kind
    }

    /* ------------------------------------------------------------------ */
    /* Enumerating                                                         */
    /* ------------------------------------------------------------------ */

    /**
     * Files in one subfolder of a tree, as `name<TAB>size` per line.
     *
     * `sub` may be empty for the root. Directories are skipped: the shelf
     * scans one level, exactly as the desktop build does.
     */
    @JvmStatic
    fun list(treeUri: String, sub: String): String {
        val c = ctx() ?: return ""
        var dir = try { DocumentFile.fromTreeUri(c, Uri.parse(treeUri)) } catch (_: Exception) { null }
            ?: return ""
        if (sub.isNotEmpty()) dir = findFolder(dir, sub) ?: return ""
        val out = StringBuilder()
        for (f in dir.listFiles()) {
            if (f.isDirectory) continue
            out.append(f.name ?: continue).append('\t').append(f.length()).append('\n')
        }
        return out.toString()
    }

    /* ------------------------------------------------------------------ */
    /* Staging                                                             */
    /* ------------------------------------------------------------------ */

    /** How far a stage() has got, 0..100, or -1 when nothing is running. */
    @Volatile private var progress: Int = -1
    @JvmStatic fun stageProgress(): Int = progress

    /**
     * Copy one file out of a tree into a real directory, and answer with its
     * path. Already-copied files of the same size are answered instantly.
     *
     * Called on the emulator thread and deliberately synchronous: the frontend
     * shows "copying" and has nothing else to do until the disc is there.
     */
    @JvmStatic
    fun stage(treeUri: String, sub: String, name: String, destDir: String): String {
        val c = ctx() ?: return ""
        var dir = try { DocumentFile.fromTreeUri(c, Uri.parse(treeUri)) } catch (_: Exception) { null }
            ?: return ""
        if (sub.isNotEmpty()) dir = findFolder(dir, sub) ?: return ""
        val src = dir.findFile(name) ?: return ""
        val want = src.length()

        val dst = File(destDir, name)
        if (dst.exists() && dst.length() == want) return dst.absolutePath
        File(destDir).mkdirs()

        progress = 0
        try {
            c.contentResolver.openInputStream(src.uri).use { input ->
                if (input == null) return ""
                val tmp = File(destDir, "$name.part")
                tmp.outputStream().use { out ->
                    val buf = ByteArray(1 shl 20)
                    var done = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        out.write(buf, 0, n)
                        done += n
                        if (want > 0) progress = ((done * 100) / want).toInt()
                    }
                }
                /* Renamed only once it is complete, so an interrupted copy is
                 * never mistaken for a game. */
                if (!tmp.renameTo(dst)) { tmp.delete(); return "" }
            }
        } catch (_: Exception) {
            return ""
        } finally {
            progress = -1
        }
        return dst.absolutePath
    }

    /**
     * The real path of a granted tree, when it genuinely has a usable one.
     *
     * A tree INSIDE the app's own external storage can be read by path, so
     * there is no reason to copy anything out of it -- that would be
     * duplicating a file onto the device it is already on. Anywhere else, a
     * grant is a permission and not a path, and answering with a path that
     * cannot be opened is worse than answering with nothing.
     *
     * Two things the first version of this got wrong and which are the whole
     * of the check below: "primary" means internal storage and nothing else,
     * and a candidate only counts if it lies under the app's own directory.
     * Without either, a folder on internal storage resolved to a lookalike on
     * the memory card, and one outside the sandbox resolved to a path the app
     * is not allowed to read.
     */
    @JvmStatic
    fun realPath(treeUri: String): String {
        val c = ctx() ?: return ""
        val id = try {
            DocumentsContract.getTreeDocumentId(Uri.parse(treeUri))
        } catch (_: Exception) { return "" }
        val colon = id.indexOf(':')
        if (colon < 0) return ""
        val volume = id.substring(0, colon)          // "primary" or "FEDD-B1FF"
        val rel = id.substring(colon + 1)

        for (base in c.getExternalFilesDirs(null).filterNotNull()) {
            /* .../<volume>/Android/data/<pkg>/files -> four up is the volume */
            val root = base.parentFile?.parentFile?.parentFile?.parentFile ?: continue
            val isPrimary = root.absolutePath.startsWith("/storage/emulated")
            if ((volume == "primary") != isPrimary) continue

            val candidate = File(root, rel)
            /*
             * Tested, not assumed.
             *
             * The rule would be "only inside the app's own directory", and on
             * this hardware that rule is wrong: a folder on the memory card
             * outside the sandbox lists perfectly well. Whether a path can be
             * read is a question with an answer, so ask it -- listFiles()
             * returning null is exactly the case a path must not be claimed
             * for.
             */
            if (candidate.isDirectory && candidate.canRead() &&
                candidate.listFiles() != null) return candidate.absolutePath
        }
        return ""
    }
}
