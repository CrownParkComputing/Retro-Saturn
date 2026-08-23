plugins {
    id("com.android.application")
    id("kotlin-android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

// Release signing config.
//
// Resolution order, first match wins:
//   1. Env vars ANDROID_KEYSTORE_PATH / ANDROID_KEYSTORE_PASSWORD /
//      ANDROID_KEY_ALIAS / ANDROID_KEY_PASSWORD (matching the legacy
//      Dosbox-X-Android build.gradle, so the GitHub Actions secrets carry
//      over unchanged: ANDROID_KEYSTORE_BASE64 is decoded by the CI workflow
//      to a temp .jks and the path is exported).
//   2. `key.properties` next to this file -- a checked-out keystore + four
//      credentials, for local dev where the env vars aren't.
//
// applicationId is `com.saturn_emu.android` so existing
// installs upgrade in place -- the display name is "Retro-Saturn" and
// the Kotlin namespace is `com.crownpark.retro_saturn` for code
// consistency, but the Play Store identity is preserved.
import java.util.Properties

data class KeystoreConfig(
    val path: String,
    val storePassword: String,
    val keyAlias: String,
    val keyPassword: String,
)

fun resolveKeystore(): KeystoreConfig? {
    val envPath = System.getenv("ANDROID_KEYSTORE_PATH")
    val envStorePw = System.getenv("ANDROID_KEYSTORE_PASSWORD")
    val envAlias = System.getenv("ANDROID_KEY_ALIAS")
    val envKeyPw = System.getenv("ANDROID_KEY_PASSWORD")
    if (envPath != null && envStorePw != null && envAlias != null && envKeyPw != null) {
        logger.lifecycle("release: using keystore from ANDROID_KEYSTORE_PATH env var")
        return KeystoreConfig(envPath, envStorePw, envAlias, envKeyPw)
    }

    val propsFile = rootProject.file("key.properties")
    if (propsFile.exists()) {
        val p = Properties().apply { load(propsFile.inputStream()) }
        val path = p["storeFile"] as String?
        val storePw = p["storePassword"] as String?
        val alias = p["keyAlias"] as String?
        val keyPw = p["keyPassword"] as String?
        if (path != null && storePw != null && alias != null && keyPw != null) {
            logger.lifecycle("release: using keystore from ${propsFile.absolutePath}")
            return KeystoreConfig(path, storePw, alias, keyPw)
        }
    }

    logger.warn(
        "release: no ANDROID_KEYSTORE_* env vars and no key.properties; " +
            "falling back to the debug keystore. This build will not be " +
            "accepted by Play Console."
    )
    return null
}

val keystoreConfig = resolveKeystore()

android {
    namespace = "com.crownpark.retro_saturn"
    // Pinned, not flutter.compileSdkVersion. The whole Retro-* family states
    // its SDK levels outright: a floating value takes whatever the Flutter
    // on the build machine happens to default to, which is how Play
    // compliance ends up depending on which laptop or runner did the build.
    compileSdk = 36
    // The NDK the prebuilt cores were compiled with.
    ndkVersion = "28.2.13676358"

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        // Retro-Saturn is the display name of this app; the Play Store
        // identity is `com.saturn_emu.android`, the listing currently
        // titled "Sega Saturn - Android", which is where the existing
        // users are. This previously read `com.crownpark.ymir_multiplatform`,
        // which is not a package on the developer account at all -- the Play
        // API returns 404 for it -- so a bundle built from it had nowhere to
        // be uploaded. Changing this again would mean publishing as a
        // brand-new app and stranding those installs. Pick once and keep.
        applicationId = "com.saturn_emu.android"
        // 26, matching the API the native core is actually built against
        // (native/ymir_core/android/build.sh, ANDROID_API=26).
        //
        // This was flutter.minSdkVersion, which is 24. The app therefore
        // declared support for API 24 and 25 devices that could never run it:
        // the core is compiled for 26, so it installs and then fails to load.
        // minSdk is not a free choice here, it is whatever the core needs.
        minSdk = 26
        // Play requires the target to stay within a year of the latest
        // Android release - 36 or higher from 31 August 2026 - and refuses
        // updates outright below that. flutter.targetSdkVersion floats with
        // whichever Flutter version happens to run the build, so an older SDK
        // on a CI runner or another machine could drop it under the bar
        // without a line of this project changing. Compliance is a decision,
        // so it is written down.
        targetSdk = 36
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        create("release") {
            if (keystoreConfig != null) {
                storeFile = file(keystoreConfig.path)
                storePassword = keystoreConfig.storePassword
                keyAlias = keystoreConfig.keyAlias
                keyPassword = keystoreConfig.keyPassword
            }
        }
    }

    buildTypes {
        release {
            signingConfig = if (keystoreConfig != null) {
                signingConfigs.getByName("release")
            } else {
                // Fallback for dev/CI: sign with the debug key so
                // `flutter run --release` still works on a fresh tree.
                // A build that ships without the release keystore is wrong;
                // the warning above is the explicit signal.
                signingConfigs.getByName("debug")
            }
        }
    }
}

flutter {
    source = "../.."
}