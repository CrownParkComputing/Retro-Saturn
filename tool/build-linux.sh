#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S native/core/linux -B build/core-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-linux --parallel "${JOBS:-4}"
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel "${JOBS:-4}"
