package com.crownpark.retro_saturn

import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class SaveFolderSyncTest {
    @Test
    fun externalProgressSurvivesRestoreSyncAndDeletion() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val uri = InstrumentationRegistry.getArguments().getString("savesTree")
        require(!uri.isNullOrBlank()) { "Provide a granted savesTree for this device regression check" }
        val granted = requireNotNull(DocumentFile.fromTreeUri(context, Uri.parse(uri)))
        val parent = if (granted.name == "RetroSaturnReleaseCheck") granted else {
            require(InstrumentationRegistry.getArguments().getString("fixtureUnderGrantedTree") == "true") {
                "Use the isolated release-check folder"
            }
            granted.findFile("RetroSaturnReleaseCheck") ?: requireNotNull(granted.createDirectory("RetroSaturnReleaseCheck"))
        }
        val remote = requireNotNull(parent.createDirectory("sync-fixtures-${System.currentTimeMillis()}"))
        val local = File(context.cacheDir, "save-sync-check-${System.currentTimeMillis()}").apply { mkdirs() }
        val ram = File(local, "backup-ram.bin")
        fun put(name: String, bytes: ByteArray) {
            val target = remote.findFile(name) ?: remote.createFile("application/octet-stream", name)!!
            context.contentResolver.openOutputStream(target.uri, "wt")!!.use { it.write(bytes) }
        }
        fun read(name: String): ByteArray = context.contentResolver.openInputStream(remote.findFile(name)!!.uri)!!.use { it.readBytes() }
        try {
            val first = ByteArray(32768) { 17 }
            val pending = ByteArray(32768) { 34 }
            val replaced = ByteArray(32768) { 51 }
            put("backup-ram.bin", first)
            assertTrue(SaveFolderSync.restore(context, remote, local))
            assertArrayEquals(first, ram.readBytes())

            ram.writeBytes(pending)
            assertTrue(SaveFolderSync.restore(context, remote, local))
            assertArrayEquals("Unsynced progress must survive restart", pending, ram.readBytes())
            assertTrue(SaveFolderSync.sync(context, remote, local))
            assertArrayEquals(pending, read("backup-ram.bin"))

            put("backup-ram.bin", replaced)
            assertTrue(SaveFolderSync.restore(context, remote, local))
            assertArrayEquals("Same-size external replacements must be imported", replaced, ram.readBytes())
            assertTrue(remote.findFile("backup-ram.bin")!!.delete())
            assertTrue(SaveFolderSync.restore(context, remote, local))
            assertFalse("External removal must not resurrect an unchanged cached save", ram.exists())

            ram.writeBytes(pending)
            val state = File(local, "states/Game.20261115-120000.state")
            state.parentFile!!.mkdirs()
            state.writeBytes(ByteArray(8192) { 68 })
            assertTrue(SaveFolderSync.sync(context, remote, local))
            assertArrayEquals(pending, read("backup-ram.bin"))
            assertNotNull(remote.findFile("states")!!.findFile(state.name))
            assertTrue(state.delete())
            assertTrue(SaveFolderSync.sync(context, remote, local))
            assertNull("Deleting a snapshot must update the selected folder", remote.findFile("states")!!.findFile(state.name))
        } finally {
            remote.delete()
            local.deleteRecursively()
        }
    }
}
