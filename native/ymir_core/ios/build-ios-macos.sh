#!/usr/bin/env bash
# Builds YmirCore.framework for iOS arm64, natively on macOS, and installs it
# where `flutter build ios` will embed it from.
#
# This replaces tools/build-ios-linux.sh and the mobaiapp/iosbox container it
# drove. That path could never run in CI: the macOS runners have no Docker,
# and the Linux ones have no iOS SDK -- it needed a volume populated by hand
# from an Xcode extraction. A Mac already has the SDK and the toolchain, so
# nothing has to be cross-anything; CMake's own iOS support is enough.
#
# Outputs:
#   native/ymir_core/ios/build/YmirCore.framework
#   flutter_app/ios/Frameworks/YmirCore.framework   (what the Runner embeds)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HERE/build/ios-arm64}"
IOS_MIN="${IOS_MIN:-15.0}"

# sysctl, not nproc: this script is macOS-only and nproc is a coreutils thing.
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

command -v xcrun >/dev/null || {
    echo "FATAL: xcrun not found -- this script needs Xcode, and only runs on macOS." >&2
    exit 1
}

echo "==> configuring (iOS ${IOS_MIN}, arm64, $(xcrun --sdk iphoneos --show-sdk-version) SDK)"
# CMAKE_OSX_SYSROOT=iphoneos lets CMake resolve the SDK through xcrun rather
# than a hardcoded path, so an Xcode upgrade does not silently break this.
cmake -S "$HERE" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" \
    -DCMAKE_BUILD_TYPE=Release

echo "==> building"
cmake --build "$BUILD_DIR" --config Release -j "$JOBS"

FRAMEWORK="$(find "$BUILD_DIR" -name 'YmirCore.framework' -type d | head -1)"
if [ -z "$FRAMEWORK" ]; then
    echo "FATAL: YmirCore.framework was not produced under $BUILD_DIR" >&2
    exit 1
fi

DEST="$REPO_ROOT/flutter_app/ios/Frameworks"
mkdir -p "$DEST"
rm -rf "$DEST/YmirCore.framework"
cp -R "$FRAMEWORK" "$DEST/YmirCore.framework"

echo
echo "Installed: $DEST/YmirCore.framework"
file "$DEST/YmirCore.framework/YmirCore"
