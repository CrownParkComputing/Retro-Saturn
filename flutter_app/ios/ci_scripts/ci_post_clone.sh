#!/bin/sh
# Xcode Cloud post-clone step.
#
# Xcode Cloud's images have Xcode and CocoaPods but no Flutter, and it does not
# run `flutter build` -- it invokes xcodebuild on the Runner scheme directly.
# That only works if Flutter has already generated ios/Flutter/Generated.xcconfig,
# because the Runner target's "Thin Binary" build phase calls
# "$FLUTTER_ROOT/packages/flutter_tools/bin/xcode_backend.sh" and FLUTTER_ROOT is
# defined in that file. So: install Flutter, resolve packages, and let
# `--config-only` write the config the Xcode build needs.
#
# Apple runs this from the ci_scripts directory, which must sit next to the
# Xcode project -- hence ios/ci_scripts/ rather than the repo root.
set -e

# Pinned rather than tracking stable. A newer Flutter resolves newer transitive
# packages and rewrites pubspec.lock mid-build, so an untracked toolchain makes
# cloud builds differ from CI for reasons that have nothing to do with the
# commit being built. This is the version Retro-C64 pins, and it satisfies this
# app's Dart constraint (sdk: ^3.11.5).
FLUTTER_VERSION="${FLUTTER_VERSION:-3.47.1}"
FLUTTER_HOME="$HOME/flutter"

echo "--- installing Flutter $FLUTTER_VERSION"
git clone --depth 1 -b "$FLUTTER_VERSION" https://github.com/flutter/flutter.git "$FLUTTER_HOME"
export PATH="$FLUTTER_HOME/bin:$PATH"
flutter --version

APP_DIR="$CI_PRIMARY_REPOSITORY_PATH/flutter_app"
cd "$APP_DIR"

# The emulator core cannot be rebuilt here. It needs the vendored submodules
# (mio, libchdr, lz4, xxHash, fmt, concurrentqueue) and a full CMake pass over
# ymir-core -- minutes of work against sources Xcode Cloud does not check out.
# It is committed for exactly this reason, so fail clearly if it is absent
# rather than producing an app that installs and then dies at dlopen.
#
# BOTH slices are checked. An xcframework carrying only the device one builds a
# green cloud archive and fails on a simulator; only the simulator one fails on
# every phone. Neither is visible until someone runs it.
CORE_XC="ios/Frameworks/YmirCore.xcframework"
for slice in ios-arm64 ios-arm64-simulator; do
  if [ ! -f "$CORE_XC/$slice/YmirCore.framework/YmirCore" ]; then
    echo "error: missing $CORE_XC/$slice -- build it with" >&2
    echo "       native/ymir_core/ios/build-ios-macos.sh and commit the result" >&2
    exit 1
  fi
done

echo "--- resolving packages"
flutter precache --ios
flutter pub get

echo "--- generating the Xcode config Flutter's build phases rely on"
flutter build ios --release --no-codesign --config-only

# This project resolves its plugins through CocoaPods, so a Podfile is expected
# -- but --config-only is what writes it, and a future move to Swift Package
# Manager would remove it. Run pod install only if there is something to run it
# on, so the script survives either mechanism.
cd ios
if [ -f Podfile ]; then
  echo "--- pod install"
  pod install --repo-update
else
  echo "note: no Podfile -- plugins resolve via Swift Package Manager"
fi

echo "--- ready for xcodebuild"
