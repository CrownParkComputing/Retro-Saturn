#!/usr/bin/env bash
# Runs inside the iosbox container. Links libymircore.dylib from the
# prebuilt ymir-core static archive + the bridge sources. No --wrap=
# (ymir-core has no ELF-style overrides, so Mach-O --redefine-sym is
# not needed either).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

IOSBOX_ROOT="/root/.iosbox"
IOS_SDK="$IOSBOX_ROOT/sdk/darwin.artifactbundle/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
IOS_LD="$IOSBOX_ROOT/sdk/darwin.artifactbundle/toolset/bin/ld64.lld"

IOS_MIN="${IOS_MIN:-13.0}"
TARGET="arm64-apple-ios${IOS_MIN}"
CFLAGS_COMMON=(-target "$TARGET" -isysroot "$IOS_SDK" -fPIC -O2 -fno-common)
LDFLAGS_COMMON=(
    -target "$TARGET" -isysroot "$IOS_SDK"
    -fuse-ld="$IOS_LD"
    -Wl,-arch,arm64
    -Wl,-platform_version,ios,"${IOS_MIN}.0",16.0.0
    -Wl,-adhoc_codesign
)

# Compile the bridge sources for arm64-apple-ios.
STAGE="$REPO_ROOT/native/ymir_core/ios/build/ios-arm64-stage"
mkdir -p "$STAGE"

clang++ "${CFLAGS_COMMON[@]}" -c \
    "$REPO_ROOT/native/ymir_core/bridge/ymir_bridge.cpp" \
    "$REPO_ROOT/native/ymir_core/bridge/audio_backend_ios.mm" \
    -I "$REPO_ROOT/native/ymir_core/bridge" \
    -I "$REPO_ROOT/libs/ymir-core/include" \
    -std=c++17 -o "$STAGE"

# Locate the prebuilt ymir-core static library.
CORE_LIB=$(find "$REPO_ROOT/native/ymir_core/ios/build/ios-arm64-core" -name 'libymir-core.a' | head -1)
if [ -z "$CORE_LIB" ]; then
    echo "FATAL: libymir-core.a not found — run build-ymir-ios-headless.sh first" >&2
    exit 1
fi

OUT_DIR="$REPO_ROOT/native/ymir_core/ios/build"
mkdir -p "$OUT_DIR"

clang++ "${LDFLAGS_COMMON[@]}" -dynamiclib \
    -install_name "@rpath/YmirCore.framework/YmirCore" \
    -o "$OUT_DIR/libymircore.dylib" \
    "$STAGE"/*.o \
    "$CORE_LIB" \
    -lz -lc++ \
    -framework AudioToolbox -framework AVFoundation \
    -framework Foundation -framework CoreFoundation