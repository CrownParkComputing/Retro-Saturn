#!/usr/bin/env bash
# Build Retro-Saturn for Android: libretrosaturn.so, which is the SDL3 + Dear
# ImGui frontend, linked against libymircore.so and libSDL3.so.
#
#   ANDROID_ABI=arm64-v8a ./android/build-core.sh
#
# Shaped after Retro-DOS's android/build-core.sh, and for the same reasons:
# SDL3 is built from source rather than borrowed from a retired tree, and the
# Android build never shares a configured tree with the host build.
#
# The one structural difference is that ymir is CMake, so the core needs no
# private source tree -- core/retro/android/CMakeLists.txt already builds
# libymircore.so for the NDK and this simply drives it.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$(cd "$HERE/.." && pwd)"
CORE="$APP/core"

[ -f "$CORE/retro/bridge/ymir_bridge.h" ] || {
    echo "error: the core is missing at $CORE" >&2
    echo "       run: git submodule update --init --recursive" >&2
    exit 1; }

ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
# 28 is this app's real floor rather than a preference: ymir-core wants iconv,
# and bionic did not gain iconv_open until API 28 -- below that the core does
# not configure at all ("Could NOT find Iconv"). The frontend and the core must
# agree, because a frontend built for a newer API than the library it loads
# fails at run time rather than at build time.
ANDROID_API="${ANDROID_API:-28}"
ANDROID_NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/28.2.13676358}}"
SDL3_TAG="${SDL3_TAG:-release-3.2.20}"
JOBS="${JOBS:-$(nproc)}"

OUT="$HERE/build/$ANDROID_ABI"
SDL3_SRC="$HERE/build/SDL"
SDL3_PREFIX="$OUT/sdl3"
IMGUI="$CORE/vendor/imgui/imgui"

case "$ANDROID_ABI" in
    arm64-v8a)   TRIPLE=aarch64-linux-android ;;
    x86_64)      TRIPLE=x86_64-linux-android ;;
    armeabi-v7a) TRIPLE=armv7a-linux-androideabi ;;
    *) echo "error: unsupported ANDROID_ABI '$ANDROID_ABI'" >&2; exit 1 ;;
esac

[ -d "$ANDROID_NDK" ] || { echo "error: no NDK at $ANDROID_NDK" >&2; exit 1; }
[ -f "$IMGUI/imgui.cpp" ] || {
    echo "error: no vendored ImGui at $IMGUI" >&2
    echo "       git -C $CORE submodule update --init --recursive" >&2
    exit 1; }

TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
CXX="$TOOLCHAIN/bin/${TRIPLE}${ANDROID_API}-clang++"
[ -x "$CXX" ] || { echo "error: no compiler at $CXX" >&2; exit 1; }

mkdir -p "$OUT"

# ---------------------------------------------------------------------------
# 1. SDL3 for Android
# ---------------------------------------------------------------------------
if [ ! -f "$SDL3_PREFIX/lib/libSDL3.so" ]; then
    echo "==> SDL3 ($SDL3_TAG) for $ANDROID_ABI"
    [ -d "$SDL3_SRC" ] || git clone --depth 1 --branch "$SDL3_TAG" \
        https://github.com/libsdl-org/SDL.git "$SDL3_SRC"
    cmake -S "$SDL3_SRC" -B "$OUT/sdl3-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ANDROID_ABI" \
        -DANDROID_PLATFORM="android-$ANDROID_API" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$SDL3_PREFIX" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF >/dev/null
    cmake --build "$OUT/sdl3-build" -j"$JOBS" >/dev/null
    cmake --install "$OUT/sdl3-build" >/dev/null
fi
echo "==> SDL3: $SDL3_PREFIX/lib/libSDL3.so"

# ---------------------------------------------------------------------------
# 1b. libcurl, and a TLS stack for it
# ---------------------------------------------------------------------------
# RetroMedia is HTTPS, so Android needs both. mbedTLS rather than OpenSSL
# because it is a few hundred kilobytes and builds in under a minute with
# plain CMake, where OpenSSL wants Perl, its own Configure and ten times the
# time for a client that makes half a dozen kinds of request.
#
# zlib comes from the NDK sysroot -- Android has always shipped libz -- but
# minizip does not, so the two files that read a zip are compiled from zlib's
# own contrib directory alongside the frontend.
MBEDTLS_TAG="${MBEDTLS_TAG:-v3.6.2}"
CURL_TAG="${CURL_TAG:-curl-8_11_1}"
ZLIB_TAG="${ZLIB_TAG:-v1.3.1}"
MBEDTLS_SRC="$HERE/build/mbedtls"
CURL_SRC="$HERE/build/curl"
ZLIB_SRC="$HERE/build/zlib"
MBEDTLS_PREFIX="$OUT/mbedtls"
CURL_PREFIX="$OUT/curl"

if [ ! -f "$MBEDTLS_PREFIX/lib/libmbedtls.a" ]; then
    echo "==> mbedTLS ($MBEDTLS_TAG)"
    [ -d "$MBEDTLS_SRC" ] || git clone --depth 1 --branch "$MBEDTLS_TAG" \
        --recurse-submodules https://github.com/Mbed-TLS/mbedtls.git "$MBEDTLS_SRC"
    cmake -S "$MBEDTLS_SRC" -B "$OUT/mbedtls-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ANDROID_ABI" -DANDROID_PLATFORM="android-$ANDROID_API" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$MBEDTLS_PREFIX" \
        -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
        >/dev/null
    cmake --build "$OUT/mbedtls-build" -j"$JOBS" >/dev/null
    cmake --install "$OUT/mbedtls-build" >/dev/null
fi

# Built, not installed: with the command-line tool and its manual switched off,
# curl's install step still tries to copy man pages that were never generated
# and fails. The archive and the headers are where they are.
CURL_LIB="$OUT/curl-build/lib/libcurl.a"
CURL_INC="$CURL_SRC/include"
if [ ! -f "$CURL_LIB" ]; then
    echo "==> libcurl ($CURL_TAG)"
    [ -d "$CURL_SRC" ] || git clone --depth 1 --branch "$CURL_TAG" \
        https://github.com/curl/curl.git "$CURL_SRC"
    # Only what the client uses. Every protocol left in is code that ships and
    # attack surface that does not need to be there.
    cmake -S "$CURL_SRC" -B "$OUT/curl-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ANDROID_ABI" -DANDROID_PLATFORM="android-$ANDROID_API" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
        -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
        -DMBEDTLS_INCLUDE_DIR="$MBEDTLS_PREFIX/include" \
        -DMBEDTLS_LIBRARY="$MBEDTLS_PREFIX/lib/libmbedtls.a" \
        -DMBEDX509_LIBRARY="$MBEDTLS_PREFIX/lib/libmbedx509.a" \
        -DMBEDCRYPTO_LIBRARY="$MBEDTLS_PREFIX/lib/libmbedcrypto.a" \
        -DCURL_ZLIB=ON -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
        -DHTTP_ONLY=ON -DCURL_DISABLE_LDAP=ON \
        -DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_USE_LIBSSH2=OFF \
        -DCURL_CA_BUNDLE=none -DCURL_CA_PATH="/system/etc/security/cacerts" \
        >/dev/null
    cmake --build "$OUT/curl-build" -j"$JOBS" >/dev/null
fi
[ -f "$CURL_LIB" ] || { echo "error: libcurl.a was not built" >&2; exit 1; }

# minizip, from zlib's contrib. Two files, and the only part of zlib that is
# not already in the sysroot.
if [ ! -d "$ZLIB_SRC" ]; then
    echo "==> zlib ($ZLIB_TAG), for contrib/minizip"
    git clone --depth 1 --branch "$ZLIB_TAG" https://github.com/madler/zlib.git "$ZLIB_SRC"
fi

# ---------------------------------------------------------------------------
# 2. The Saturn core and its bridge
# ---------------------------------------------------------------------------
# CMAKE_DISABLE_FIND_PACKAGE_Vulkan for the same reason the desktop build sets
# it: upstream's own SDL3 GUI wants a SPIR-V compiler that is not needed here,
# and its absence is a FATAL_ERROR rather than a skipped target.
echo "==> the Saturn core"
cmake -S "$CORE/retro/android" -B "$OUT/core" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM="android-$ANDROID_API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON \
    >/dev/null
cmake --build "$OUT/core" -j"$JOBS" --target ymircore >/dev/null
CORE_SO="$OUT/core/libymircore.so"
[ -f "$CORE_SO" ] || { echo "error: $CORE_SO was not built" >&2; exit 1; }
echo "    $(ls -lh "$CORE_SO" | awk '{print $5}')  libymircore.so"

# ---------------------------------------------------------------------------
# 3. The frontend
# ---------------------------------------------------------------------------
# Which Ymir this is, stamped in so the About page can answer "is the core up
# to date?" without anyone reading a submodule pointer.
CORE_DESC="$(git -C "$CORE" describe --tags --always 2>/dev/null || echo unknown)"
CORE_DATE="$(git -C "$CORE" log -1 --format=%cd --date=format:'%Y-%m-%d' 2>/dev/null || echo unknown)"
CORE_VER="$(grep -m1 'project(Ymir VERSION' "$CORE/CMakeLists.txt" 2>/dev/null \
            | sed 's/.*VERSION //; s/).*//' || echo unknown)"
echo "==> frontend (Ymir $CORE_VER, $CORE_DESC, $CORE_DATE)"

FE_OUT="$OUT/frontend"; mkdir -p "$FE_OUT"
FE_OBJS=""
FE_FLAGS="-fPIC -O2 -g0 -DANDROID -std=gnu++17 -Wall -Wextra -Wno-unused-parameter
          -DSATURN_CORE_VERSION=\"$CORE_VER\"
          -DSATURN_CORE_DESC=\"$CORE_DESC\"
          -DSATURN_CORE_DATE=\"$CORE_DATE\"
          -DSATURN_MEDIA_HTTP=1
          -I$APP/frontend -I$CORE/vendor/stb/stb -I$CORE/retro/bridge
          -I$IMGUI -I$IMGUI/backends -I$SDL3_PREFIX/include
          -I$CURL_INC -I$OUT/curl-build/include -I$ZLIB_SRC/contrib -I$ZLIB_SRC"

for src in "$APP"/frontend/*.cpp \
           "$IMGUI"/imgui.cpp "$IMGUI"/imgui_draw.cpp "$IMGUI"/imgui_tables.cpp \
           "$IMGUI"/imgui_widgets.cpp \
           "$IMGUI"/backends/imgui_impl_sdl3.cpp \
           "$IMGUI"/backends/imgui_impl_sdlrenderer3.cpp; do
    obj="$FE_OUT/$(basename "${src%.cpp}").o"
    # shellcheck disable=SC2086
    "$CXX" $FE_FLAGS -c -o "$obj" "$src" || {
        echo "error: compile failed on $src" >&2; exit 1; }
    FE_OBJS="$FE_OBJS $obj"
done

# minizip, compiled as C beside the frontend.
echo "==> minizip"
MZ_OBJS=""
for src in "$ZLIB_SRC"/contrib/minizip/unzip.c "$ZLIB_SRC"/contrib/minizip/ioapi.c; do
    obj="$FE_OUT/$(basename "${src%.c}").o"
    "$TOOLCHAIN/bin/${TRIPLE}${ANDROID_API}-clang" -fPIC -O2 -g0 \
        -I"$ZLIB_SRC" -I"$ZLIB_SRC/contrib/minizip" -c -o "$obj" "$src" || {
        echo "error: minizip compile failed on $src" >&2; exit 1; }
    MZ_OBJS="$MZ_OBJS $obj"
done

echo "==> linking libretrosaturn.so"
# shellcheck disable=SC2086
"$CXX" -shared -fPIC -o "$OUT/libretrosaturn.so" \
    $FE_OBJS $MZ_OBJS "$CORE_SO" \
    -L"$SDL3_PREFIX/lib" -lSDL3 \
    "$CURL_LIB" \
    "$MBEDTLS_PREFIX/lib/libmbedtls.a" \
    "$MBEDTLS_PREFIX/lib/libmbedx509.a" \
    "$MBEDTLS_PREFIX/lib/libmbedcrypto.a" \
    -lz -lm -ldl -llog -landroid \
    -Wl,--no-undefined

# ---------------------------------------------------------------------------
# 4. Prove SDL_main is actually there
# ---------------------------------------------------------------------------
# SDLActivity looks up SDL_main by name. Without <SDL3/SDL_main.h> the
# frontend's main() keeps its own name, the library loads, and the app dies at
# startup with a message about a missing entry point -- a device-only mystery
# that this turns into a build error.
"$TOOLCHAIN/bin/llvm-nm" -D --defined-only "$OUT/libretrosaturn.so" > "$OUT/exported.syms"
grep -q " SDL_main\$" "$OUT/exported.syms" || {
    echo "error: libretrosaturn.so does not export SDL_main" >&2
    echo "       saturn_app.cpp must include <SDL3/SDL_main.h>" >&2
    exit 1; }

# libc++_shared is a real runtime dependency, not an optional extra: leaving it
# out fails at System.loadLibrary time rather than at build time.
cp -f "$SDL3_PREFIX/lib/libSDL3.so" "$CORE_SO" "$OUT/"
CXX_SHARED="$TOOLCHAIN/sysroot/usr/lib/$TRIPLE/libc++_shared.so"
[ -f "$CXX_SHARED" ] || CXX_SHARED="$(find "$TOOLCHAIN" -name libc++_shared.so -path "*$TRIPLE*" | head -1)"
[ -f "$CXX_SHARED" ] && cp -f "$CXX_SHARED" "$OUT/" || echo "warning: no libc++_shared.so" >&2

find "$SDL3_PREFIX" -name 'SDL3-*.jar' ! -name '*sources*' -exec cp -f {} "$OUT/SDL3.jar" \; 2>/dev/null || true

# ---------------------------------------------------------------------------
# 5. Install where Gradle actually looks
# ---------------------------------------------------------------------------
# Not a convenience. Gradle packages app/src/main/jniLibs, and its
# mergeReleaseNativeLibs task reports UP-TO-DATE when nothing there changed --
# so leaving this as a manual step produces a BUILD SUCCESSFUL that ships the
# previous library, with none of the new code in it.
JNI_LIBS="$HERE/app/src/main/jniLibs/$ANDROID_ABI"
mkdir -p "$JNI_LIBS"
cp -f "$OUT/libretrosaturn.so" "$OUT/libSDL3.so" "$OUT/libymircore.so" "$JNI_LIBS/"
[ -f "$OUT/libc++_shared.so" ] && cp -f "$OUT/libc++_shared.so" "$JNI_LIBS/"

# Stripped, and only the copies being packaged -- the ones in build/ keep
# their symbols so a native crash from a test build can still be read. The
# core is 48MB with them and a third of that without, and that difference is
# the download every user pays for.
for so in "$JNI_LIBS"/*.so; do
    "$TOOLCHAIN/bin/llvm-strip" --strip-unneeded "$so" 2>/dev/null || true
done

APP_LIBS="$HERE/app/libs"; mkdir -p "$APP_LIBS"
[ -f "$OUT/SDL3.jar" ] && cp -f "$OUT/SDL3.jar" "$APP_LIBS/"

# The interface assets, which the frontend reads from its own asset folder.
ASSETS="$HERE/app/src/main/assets/assets"
mkdir -p "$ASSETS"
cp -f "$APP"/frontend/assets/* "$ASSETS/" 2>/dev/null || true
# The demo disc is deliberately NOT packaged for Android.
#
# It is 83MB of mostly CD audio, which is incompressible -- gzip takes it to
# 74MB -- and it would be the entire download for every user who installs the
# app. A homebrew Pong is not worth that on a phone connection. The desktop
# build still ships it, where it costs nothing, and demo_disc_path() returning
# empty is already handled: the wizard's demo button simply is not offered.

echo "==> installed for packaging:"
ls -lh "$JNI_LIBS"/*.so
