#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="${1:-iphonesimulator}"
case "$sdk" in iphoneos|iphonesimulator) ;; *) echo 'Use iphoneos or iphonesimulator' >&2; exit 1;; esac
cmake_bin="${CMAKE_BIN:-cmake}"
ninja_bin="${NINJA_BIN:-ninja}"
build_number="${IOS_BUILD_NUMBER:-6}"
jobs="${JOBS:-4}"
out="$PWD/build/apple"
sdl_src="$out/SDL"
sdl_tag="release-3.4.18"
sdl_build="$out/sdl-$sdk"
sdl_prefix="$out/sdl-install-$sdk"
if [ ! -d "$sdl_src/.git" ]; then
  git clone --depth 1 --branch "$sdl_tag" https://github.com/libsdl-org/SDL.git "$sdl_src"
elif [ "$(git -C "$sdl_src" describe --tags --exact-match 2>/dev/null || true)" != "$sdl_tag" ]; then
  git -C "$sdl_src" fetch --depth 1 origin "refs/tags/$sdl_tag:refs/tags/$sdl_tag"
  git -C "$sdl_src" checkout --detach "$sdl_tag"
fi
platform_flags=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk"
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)
"$cmake_bin" -S "$sdl_src" -B "$sdl_build" -G Ninja "${platform_flags[@]}" \
  -DCMAKE_MAKE_PROGRAM="$ninja_bin" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$sdl_prefix" -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
"$cmake_bin" --build "$sdl_build" --parallel "$jobs"
"$cmake_bin" --install "$sdl_build"
"$cmake_bin" -S . -B "$out/$sdk" -G Xcode "${platform_flags[@]}" \
  -DSDL3_DIR="$sdl_prefix/lib/cmake/SDL3" -DBUILD_TESTING=OFF \
  -DSATURN_APP_BUILD="$build_number" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DCMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL=YES
# Xcode archive can leave the normal product path linked to its archive staging area.
product="$out/$sdk/Release-$sdk/RetroSaturn.app"
if [ -L "$product" ]; then rm "$product"; fi
"$cmake_bin" --build "$out/$sdk" --config Release --target RetroSaturn --parallel "$jobs"
