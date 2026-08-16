#!/usr/bin/env bash
# Runs inside the iosbox container. Cross-compiles ymir-core and the
# bridge for arm64-apple-ios and links them into libymircore.dylib.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

# Pick the iOS SDK inside the iosbox volume
IOSBOX_ROOT="/root/.iosbox"
IOS_SDK="$IOSBOX_ROOT/sdk/darwin.artifactbundle/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -d "$IOS_SDK" ]; then
    echo "FATAL: iOS SDK not found at $IOS_SDK" >&2
    echo "Ensure the iosbox-sdk volume is created (see docs/IOS_BUILD.md)." >&2
    exit 1
fi

IOS_MIN="${IOS_MIN:-13.0}"
TARGET="arm64-apple-ios${IOS_MIN}"

export CC="clang -target $TARGET -isysroot $IOS_SDK -fPIC"
export CXX="clang++ -target $TARGET -isysroot $IOS_SDK -fPIC"
export AR="llvm-ar"
export RANLIB="llvm-ranlib"
export STRIP="llvm-strip"
export CFLAGS="-fPIC -O2 -fno-common"
export CXXFLAGS="-fPIC -O2 -fno-common"

BUILD_DIR="$REPO_ROOT/native/ymir_core/ios/build/ios-arm64-core"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_OSX_SYSROOT="$IOS_SDK" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_AR="$AR" \
    -DCMAKE_RANLIB="$RANLIB" \
    -DCMAKE_BUILD_TYPE=Release \
    -DYmir_LIBRARY_ONLY=ON \
    -DYmir_DEV_BUILD=OFF \
    -DYmir_FF_VIRTUA_GUN=ON

cmake --build "$BUILD_DIR" -j "$(nproc)"