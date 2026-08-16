#!/usr/bin/env bash
# Cross-builds the iOS arm64 device slice of libymircore.dylib via the
# mobaiapp/iosbox Docker image, then copies the result into
# flutter_app/ios/Frameworks/YmirCore.framework/ so flutter build ios
# picks it up.
#
# Outputs:
#   native/ymir_core/ios/build/libymircore.dylib
#   flutter_app/ios/Frameworks/YmirCore.framework/YmirCore  (symlink → dylib)
#   flutter_app/ios/Frameworks/YmirCore.framework/Info.plist
#   flutter_app/ios/Frameworks/libymircore.dylib            (bare copy)
#
# Prereqs (host):
#   - Docker
#   - iosbox-sdk named volume populated from an Xcode extraction
#     (see docs/IOS_BUILD.md)
#   - The ymir-core subtree (vendor/... or AndroidStudioProjects/Ymir)
#
# See docs/IOS_BUILD.md for the full setup recipe.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"

# 1. Cross-build via Docker
bash "$REPO_ROOT/native/ymir_core/ios/build.sh"

DYLIB="$REPO_ROOT/native/ymir_core/ios/build/libymircore.dylib"
FW_DIR="$REPO_ROOT/flutter_app/ios/Frameworks/YmirCore.framework"
INFO_PLIST="$REPO_ROOT/native/ymir_core/ios/Info.plist"

if [ ! -f "$DYLIB" ]; then
    echo "FATAL: $DYLIB not produced" >&2
    exit 1
fi

# 2. Wrap as a .framework so flutter build ios treats it as a real
# framework (App Store rejection 90426 fires on bare .dylib copies).
mkdir -p "$FW_DIR"
cp -f "$DYLIB" "$FW_DIR/YmirCore"
cp -f "$INFO_PLIST" "$FW_DIR/Info.plist" 2>/dev/null || true
# Bare dylib copy next to the framework (Xcode "Embed Frameworks" phase
# uses the framework; tools/device-push.sh uses the bare dylib for the
# simulator slice).
cp -f "$DYLIB" "$REPO_ROOT/flutter_app/ios/Frameworks/libymircore.dylib"

echo
echo "Installed:"
echo "  $FW_DIR/YmirCore"
echo "  $REPO_ROOT/flutter_app/ios/Frameworks/libymircore.dylib"
echo
echo "Now: cd flutter_app && flutter build ios --release --no-codesign"