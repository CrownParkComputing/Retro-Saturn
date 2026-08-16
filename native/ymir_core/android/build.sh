#!/usr/bin/env bash
# Builds the Android arm64-v8a libymircore.so and copies it into
# flutter_app/android/app/src/main/jniLibs/arm64-v8a/. Flutter's
# DynamicLibrary.open('libymircore.so') resolves it at runtime.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HERE/build}"
JOBS="${JOBS:-4}"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-/home/jon/Android/Sdk/ndk/28.2.13676358}}"
API="${ANDROID_API:-26}"
JNI_LIBS_DIR="${JNI_LIBS_DIR:-$REPO_ROOT/flutter_app/android/app/src/main/jniLibs/arm64-v8a}"

if [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
    echo "FATAL: NDK toolchain not found at $NDK/build/cmake/android.toolchain.cmake" >&2
    echo "Set ANDROID_NDK_HOME or ANDROID_NDK_ROOT to your NDK install." >&2
    exit 1
fi

cmake -S "$HERE" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM="android-${API}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DYmir_LIBRARY_ONLY=ON \
    -DYmir_DEV_BUILD=OFF

cmake --build "$BUILD_DIR" -j "$JOBS"

mkdir -p "$JNI_LIBS_DIR"
cp -v "$BUILD_DIR/libymircore.so" "$JNI_LIBS_DIR/"

echo
echo "Installed: $JNI_LIBS_DIR/libymircore.so"