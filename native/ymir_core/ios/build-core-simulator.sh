#!/usr/bin/env bash
# Mac-only. Builds the iOS Simulator slice of libymircore.dylib by
# linking the prebuilt arm64 ymir-core static lib + bridge sources
# against the iphonesimulator SDK.
#
# Why ld -r -alias: Mac Xcode ships no llvm-objcopy, so we can't do
# --redefine-sym the Linux way. For ymir-core (which has no --wrap=
# convention) we don't actually need any renaming, but we keep the
# pattern in place so a future GLES/Vulkan presenter slot is easy.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

IOS_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
IOS_MIN="${IOS_MIN:-15.0}"
TARGET="arm64-apple-ios${IOS_MIN}-simulator"
CFLAGS_COMMON=(-target "$TARGET" -isysroot "$IOS_SDK" -fPIC -O2 -fno-common)
LDFLAGS_COMMON=(
    -target "$TARGET" -isysroot "$IOS_SDK"
    # No -fuse-ld=lld: that belongs to the Linux cross-build this script was
    # copied from. Xcode's clang has no lld, and on a Mac ld64 is already the
    # right linker -- -target alone gives it the simulator platform.
    -Wl,-adhoc_codesign
)

# Taken from what CMake actually passes the device build, not guessed: the
# core's headers reach into the vendored third-party trees (mio for the
# memory-mapped backup RAM, fmt, xxHash, concurrentqueue, libchdr), and
# without them the first #include that leaves ymir-core fails.
INCLUDES=(
    -I "$REPO_ROOT/native/ymir_core/bridge"
    -I "$REPO_ROOT/libs/ymir-core/include"
    -I "$REPO_ROOT/libs/ymir-core/src"
    -I "$REPO_ROOT/vendor/mio/include"
    -I "$REPO_ROOT/vendor/fmt/include"
    -I "$REPO_ROOT/vendor/xxHash/xxHash"
    -I "$REPO_ROOT/vendor/concurrentqueue/concurrentqueue"
    -I "$REPO_ROOT/vendor/libchdr/libchdr/include"
)

# Also lifted from the device build. These are not tuning: the dev-log
# machinery evaluates TGroup::enabled at compile time, so without
# Ymir_ENABLE_DEVLOG the templates fail to be constant expressions and the
# core does not compile at all.
DEFINES=(
    -DNDEBUG
    -DYmirCore_EXPORTS
    -DYmir_DEV_ASSERTIONS=0
    -DYmir_DEV_BUILD=0
    -DYmir_ENABLE_DEVLOG=0
    -DYmir_EXTRA_INLINING=0
    -DYmir_FF_VIRTUA_GUN=1
    -DYmir_VERSION=\"0.3.2\"
    -DYmir_VERSION_MAJOR=0
    -DYmir_VERSION_MINOR=3
    -DYmir_VERSION_PATCH=2
    -DYmir_VERSION_BUILD=\"\"
    -DYmir_VERSION_PRERELEASE=\"\"
    -DTOML_EXCEPTIONS=0
)

STAGE="$REPO_ROOT/native/ymir_core/ios/build/sim-arm64-stage"
mkdir -p "$STAGE"

# gnu++20, matching what CMake gives the device build. The core headers use
# concepts (std::integral), so the c++17 this asked for could not parse them.
#
# One clang++ invocation per source. Passing -c with several inputs AND -o
# is an error ("cannot specify -o when generating multiple output files"),
# which is why this script had never produced anything.
for src in "$REPO_ROOT/native/ymir_core/bridge/ymir_bridge.cpp" \
           "$REPO_ROOT/native/ymir_core/bridge/audio_backend_ios.mm"; do
    obj="$STAGE/$(basename "${src%.*}").o"
    echo "==> compiling $(basename "$src")"
    clang++ "${CFLAGS_COMMON[@]}" -c "$src" \
        "${INCLUDES[@]}" "${DEFINES[@]}" \
        -std=gnu++20 -o "$obj"
done

# Searched across the whole build tree rather than one guessed directory:
# build-ios-macos.sh puts it under ios-arm64/ymir-core-build/, not the
# ios-arm64-core/ this once assumed, so the guess never matched.
CORE_LIB=$(find "$REPO_ROOT/native/ymir_core/ios/build" -name 'libymir-core.a' 2>/dev/null | head -1)
if [ -z "$CORE_LIB" ]; then
    echo "FATAL: libymir-core.a not found — run build-ymir-ios-headless.sh first" >&2
    exit 1
fi

# Not ios/vicecore/ -- that path is the C64 app's, left behind by the copy
# this script started as. Saturn's core is YmirCore.
OUT_DIR="${OUT_DIR:-$REPO_ROOT/native/ymir_core/ios/build/sim-arm64}"
mkdir -p "$OUT_DIR"

clang++ "${LDFLAGS_COMMON[@]}" -dynamiclib \
    -install_name "@rpath/YmirCore.framework/YmirCore" \
    -o "$OUT_DIR/libymircore.dylib" \
    "$STAGE"/*.o \
    "$CORE_LIB" \
    -lz -lc++ \
    -framework AudioToolbox -framework AVFoundation \
    -framework Foundation -framework CoreFoundation