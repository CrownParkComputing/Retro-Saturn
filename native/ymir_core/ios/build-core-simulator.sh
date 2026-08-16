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
    -fuse-ld=lld
    -Wl,-arch,arm64
    -Wl,-platform_version,ios-simulator,"${IOS_MIN}.0","${IOS_MIN}.0"
    -Wl,-adhoc_codesign
)

STAGE="$REPO_ROOT/native/ymir_core/ios/build/sim-arm64-stage"
mkdir -p "$STAGE"

clang++ "${CFLAGS_COMMON[@]}" -c \
    "$REPO_ROOT/native/ymir_core/bridge/ymir_bridge.cpp" \
    "$REPO_ROOT/native/ymir_core/bridge/audio_backend_ios.mm" \
    -I "$REPO_ROOT/native/ymir_core/bridge" \
    -I "$REPO_ROOT/libs/ymir-core/include" \
    -std=c++17 -o "$STAGE"

CORE_LIB=$(find "$REPO_ROOT/native/ymir_core/ios/build/ios-arm64-core" -name 'libymir-core.a' | head -1)
if [ -z "$CORE_LIB" ]; then
    echo "FATAL: libymir-core.a not found — run build-ymir-ios-headless.sh first" >&2
    exit 1
fi

OUT_DIR="${OUT_DIR:-$REPO_ROOT/flutter_app/ios/vicecore/iphonesimulator}"
mkdir -p "$OUT_DIR"

clang++ "${LDFLAGS_COMMON[@]}" -dynamiclib \
    -install_name "@rpath/YmirCore.framework/YmirCore" \
    -o "$OUT_DIR/libymircore.dylib" \
    "$STAGE"/*.o \
    "$CORE_LIB" \
    -lz -lc++ \
    -framework AudioToolbox -framework AVFoundation \
    -framework Foundation -framework CoreFoundation