package com.crownpark.retro_saturn

import android.content.Intent
import android.hardware.input.InputManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.provider.Settings
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import org.flame_engine.gamepads_android.GamepadsCompatibleActivity

/**
 * Flutter host activity for ymir_multiplatform.
 *
 * Implements the [GamepadsCompatibleActivity] interface that the
 * `gamepads_android` plugin requires so it can wire raw Android
 * KeyEvent/MotionEvent streams into the Dart-side `Gamepads.normalizedEvents`
 * stream consumed by [GamepadService].
 */
class MainActivity : FlutterActivity(), GamepadsCompatibleActivity {
    private var keyHandler: ((KeyEvent) -> Boolean)? = null
    private var motionHandler: ((MotionEvent) -> Boolean)? = null

    /* All-files access, the Retro-Amiga way. The library lives wherever the
     * user keeps it -- usually an SD card -- and scoped storage will not let
     * the app read a raw path there without this. The request opens the
     * system's All-files-access page; the parked result is completed from
     * onResume when the user comes back. */
    private var pendingStorageAccess: MethodChannel.Result? = null
    private var waitingForStorageSettings = false

    private fun hasSharedStorageAccess(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.R ||
            Environment.isExternalStorageManager()

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            STORAGE_CHANNEL,
        ).setMethodCallHandler { call, result ->
            when (call.method) {
                "hasSharedStorageAccess" ->
                    result.success(hasSharedStorageAccess())
                "requestSharedStorageAccess" -> {
                    if (hasSharedStorageAccess()) {
                        result.success(true)
                    } else if (pendingStorageAccess != null) {
                        result.error(
                            "busy",
                            "storage access settings are already open",
                            null,
                        )
                    } else {
                        pendingStorageAccess = result
                        waitingForStorageSettings = true
                        val intent = Intent(
                            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:$packageName"),
                        )
                        try {
                            startActivity(intent)
                        } catch (error: Exception) {
                            startActivity(
                                Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
                            )
                        }
                    }
                }
                else -> result.notImplemented()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (waitingForStorageSettings) {
            waitingForStorageSettings = false
            val pending = pendingStorageAccess
            pendingStorageAccess = null
            pending?.success(hasSharedStorageAccess())
        }
    }

    companion object {
        private const val STORAGE_CHANNEL = "retro_saturn/storage"
    }

    override fun registerInputDeviceListener(
        listener: InputManager.InputDeviceListener,
        handler: Handler?
    ) {
        getSystemService(InputManager::class.java)
            .registerInputDeviceListener(listener, handler)
    }

    override fun registerKeyEventHandler(handler: (KeyEvent) -> Boolean) {
        keyHandler = handler
    }

    override fun registerMotionEventHandler(handler: (MotionEvent) -> Boolean) {
        motionHandler = handler
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val handled = keyHandler?.invoke(event) ?: false
        return handled || super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val handled = motionHandler?.invoke(event) ?: false
        return handled || super.dispatchGenericMotionEvent(event)
    }
}