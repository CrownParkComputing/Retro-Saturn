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
// applicationId stays `com.crownpark.ymir_multiplatform` so existing
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
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        // Retro-Saturn is the display name of this app; this applicationId
        // remains `com.crownpark.ymir_multiplatform` because that is the
        // Play Store identity under which the legacy ymir-android listing
        // already lives. Changing it would mean publishing as a brand-new
        // app; existing installs would not auto-update. Pick once and keep.
        applicationId = "com.crownpark.ymir_multiplatform"
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
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