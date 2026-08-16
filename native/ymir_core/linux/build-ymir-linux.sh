#!/usr/bin/env bash
# Builds the Linux version of libymircore.so plus all native test
# executables. Outputs land in build/ next to this script.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HERE/build}"
JOBS="${JOBS:-$(nproc)}"

cmake -S "$HERE" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DYmir_LIBRARY_ONLY=ON \
    -DYmir_DEV_BUILD=OFF

cmake --build "$BUILD_DIR" -j "$JOBS"

echo
echo "Built:"
echo "  $BUILD_DIR/libymircore.so"
echo "  $BUILD_DIR/ymir_smoke_test"
echo "  $BUILD_DIR/ymir_peripheral_test"
echo "  $BUILD_DIR/ymir_swap_snapshot_test"
echo "  $BUILD_DIR/ymir_audio_tone_test"
echo
echo "Flutter FE loads: $BUILD_DIR/libymircore.so"