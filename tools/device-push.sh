#!/usr/bin/env bash
# Multi-target device push for ymir-android.
#
# Modes:
#   --target android --build --run      Build APK, install + launch on
#                                        the connected Retroid.
#   --target android --launch            Just launch the existing APK.
#   --target linux --run                 No-op reminder: flutter run -d linux.
#   --target ios --build --run            Build IPA via tools/build-ios-linux.sh,
#                                        install via MobAI + usbmuxd.
#   --target ios --run                   Just install + launch the existing IPA.
#
# iOS path mirrors the ViceMultiplatform pattern (debugger attach is
# required because TAP builds are JIT-only and iOS kills them in <1s
# unless a debugger is attached).

set -euo pipefail

target="android"
mode="launch"

for arg in "$@"; do
    case "$arg" in
        --target) shift; target="${1:-}" ;;
        --target=*) target="${arg#--target=}" ;;
        --build) mode="build" ;;
        --run) mode="run" ;;
        --launch) mode="launch" ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
    shift 2>/dev/null || true
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"

case "$target" in
    android)
        if [ "$mode" = "build" ]; then
            (cd "$REPO_ROOT" && bash native/ymir_core/android/build.sh)
            (cd "$REPO_ROOT/flutter_app" && \
                /home/jon/development/flutter/bin/flutter build apk --debug)
        fi
        APK="$REPO_ROOT/flutter_app/build/app/outputs/flutter-apk/app-debug.apk"
        adb install -r "$APK"
        adb shell am force-stop com.crownpark.ymir_multiplatform
        adb shell am start -n com.crownpark.ymir_multiplatform/.MainActivity
        echo
        echo "Launched. adb logcat -d | grep ymir to inspect."
        ;;

    linux)
        echo "Linux host: nothing to push. Run:"
        echo "  cd flutter_app && /home/jon/development/flutter/bin/flutter run -d linux"
        ;;

    ios)
        if [ "$mode" = "build" ]; then
            bash "$HERE/build-ios-linux.sh"
            # IPA packaging is handled by flutter build ipa — the user
            # usually does that step manually because of signing.
            echo "Build done. Now run:"
            echo "  cd flutter_app && flutter build ipa --export-method ad-hoc"
        fi
        # The actual MobAI + usbmuxd install/launch dance mirrors
        # ViceMultiplatform's tool — delegate via that pattern when
        # needed. For now we just print the IPA path.
        echo "Run the ViceMultiplatform device-push.sh pattern with the ymir IPA."
        ;;
esac