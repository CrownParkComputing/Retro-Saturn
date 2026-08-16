package com.crownpark.ymir_multiplatform

import android.hardware.input.InputManager
import android.os.Handler
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import io.flutter.embedding.android.FlutterActivity
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