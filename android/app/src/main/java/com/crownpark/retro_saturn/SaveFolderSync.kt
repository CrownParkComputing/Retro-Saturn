package com.crownpark.retro_saturn

import android.content.Context
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest

/** The selected saves folder is durable storage; the core needs a local mapped RAM file. */
internal object SaveFolderSync {
    private const val INDEX = ".folder-sync.json"

    private fun allowed(relative: String): Boolean = relative == "backup-ram.bin" ||
        (relative.startsWith("states/") && !relative.substringAfterLast('/').startsWith('.') &&
            (relative.endsWith(".state") || relative.endsWith(".ymstate") ||
                Regex(".*\\.s[1-4]$").matches(relative)))

    private fun digest(file: File): String {
        val hash = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(65536)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                hash.update(buffer, 0, n)
            }
        }
        return hash.digest().joinToString("") { "%02x".format(it.toInt() and 255) }
    }

    private fun index(local: File): JSONObject = try {
        JSONObject(File(local, INDEX).readText())
    } catch (_: Exception) { JSONObject() }

    private fun remember(index: JSONObject, relative: String, file: File, hash: String = digest(file)) {
        index.put(relative, JSONObject().put("hash", hash).put("size", file.length()).put("modified", file.lastModified()))
    }

    private fun saveIndex(local: File, index: JSONObject) {
        val pending = File(local, "$INDEX.tmp")
        pending.writeText(index.toString())
        check(pending.renameTo(File(local, INDEX))) { "Could not save the sync journal" }
    }

    private fun localHash(file: File, previous: JSONObject?): String {
        // RAM can change through its memory map without its size changing.
        if (file.name != "backup-ram.bin" && previous != null &&
            previous.optLong("size", -1) == file.length() &&
            previous.optLong("modified", -1) == file.lastModified())
            return previous.getString("hash")
        return digest(file)
    }

    private fun remoteFiles(root: DocumentFile): Map<String, DocumentFile> {
        val files = mutableMapOf<String, DocumentFile>()
        root.findFile("backup-ram.bin")?.takeIf { it.isFile }?.let { files["backup-ram.bin"] = it }
        root.findFile("states")?.takeIf { it.isDirectory }?.listFiles()?.forEach {
            val relative = "states/${it.name}"
            if (it.isFile && allowed(relative)) files[relative] = it
        }
        return files
    }

    @Synchronized
    fun restore(context: Context, remote: DocumentFile, local: File): Boolean = try {
        local.mkdirs()
        val journal = index(local)
        val source = remoteFiles(remote)
        for ((relative, document) in source) {
            val destination = File(local, relative)
            val previous = journal.optJSONObject(relative)
            // A crash or failed export must never replace newer, unsynced local progress.
            if (destination.isFile && (previous == null || localHash(destination, previous) != previous.optString("hash")))
                continue
            destination.parentFile?.mkdirs()
            val pending = File(destination.path + ".restore-part")
            context.contentResolver.openInputStream(document.uri).use { input ->
                checkNotNull(input) { "Cannot read the selected saves folder" }
                pending.outputStream().use { input.copyTo(it) }
            }
            check(pending.length() == document.length()) { "Incomplete save import" }
            if (relative == "backup-ram.bin") check(pending.length() == 32768L) { "Invalid Saturn battery RAM size" }
            check(pending.renameTo(destination)) { "Cannot restore a save" }
            remember(journal, relative, destination)
        }
        for (relative in journal.keys().asSequence().toList()) {
            if (relative in source || !allowed(relative)) continue
            val file = File(local, relative)
            val previous = journal.getJSONObject(relative)
            if (file.isFile && localHash(file, previous) == previous.optString("hash")) {
                check(file.delete())
                journal.remove(relative)
            }
        }
        saveIndex(local, journal)
        true
    } catch (error: Exception) {
        Log.w("SaturnSaveSync", "Save import failed; retaining the local cache", error)
        false
    }

    @Synchronized
    fun sync(context: Context, remote: DocumentFile, local: File): Boolean = try {
        val journal = index(local)
        val files = local.walkTopDown().filter { it.isFile && allowed(it.relativeTo(local).invariantSeparatorsPath) }.toList()
        val present = files.map { it.relativeTo(local).invariantSeparatorsPath }.toSet()
        var complete = true
        for (file in files) {
            val relative = file.relativeTo(local).invariantSeparatorsPath
            val hash = localHash(file, journal.optJSONObject(relative))
            if (hash == journal.optJSONObject(relative)?.optString("hash")) continue
            val directory = if (relative.startsWith("states/"))
                remote.findFile("states") ?: remote.createDirectory("states") else remote
            checkNotNull(directory) { "Cannot create the save states folder" }
            val name = file.name
            val pendingName = ".$name.retro-part"
            val pending = directory.findFile(pendingName) ?: directory.createFile("application/octet-stream", pendingName)
            checkNotNull(pending) { "Cannot create a save file" }
            context.contentResolver.openOutputStream(pending.uri, "wt").use { output ->
                checkNotNull(output)
                file.inputStream().use { it.copyTo(output) }
            }
            check(pending.length() == file.length()) { "Incomplete save export" }
            val old = directory.findFile(name)
            val previousName = ".$name.retro-previous"
            directory.findFile(previousName)?.delete()
            if (old != null && !old.renameTo(previousName)) {
                pending.delete()
                complete = false
                continue
            }
            if (!pending.renameTo(name)) {
                old?.renameTo(name)
                complete = false
                continue
            }
            old?.delete()
            remember(journal, relative, file, hash)
        }
        val external = remoteFiles(remote)
        for (relative in journal.keys().asSequence().toList()) {
            if (relative in present || !allowed(relative)) continue
            val old = external[relative]
            if (old == null || old.delete()) journal.remove(relative) else complete = false
        }
        saveIndex(local, journal)
        complete
    } catch (error: Exception) {
        Log.w("SaturnSaveSync", "Save export failed; retaining pending progress locally", error)
        false
    }
}
