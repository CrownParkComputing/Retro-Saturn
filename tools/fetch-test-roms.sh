#!/usr/bin/env bash
# Downloads a freeware Saturn BIOS + a small homebrew CHD/ISO for
# smoke-testing the Flutter emulator without copyrighted game files.
#
# Outputs (under /tmp/ymir_test_roms/):
#   saturn_bios.bin        — Yabause-validated open BIOS (512 KiB)
#   ymir_smoke.chd         — small homebrew CHD (size varies)
#
# Both files are public domain or freely-redistributable. Do NOT use
# these for anything other than CI smoke tests.

set -euo pipefail

OUT="${OUT:-/tmp/ymir_test_roms}"
mkdir -p "$OUT"

# Yabause ships a freeware BIOS image under docs/.
YABUSE_URL="https://github.com/Yabause/yabause/raw/master/yabause/data/saturn_bios.bin"

# 8BitDo's "8B Saturn Demo" — tiny public-domain Saturn homebrew.
HOMEbrew_URL="https://github.com/ijacquez/saturn-demos/raw/main/demos/8b-demo/8b-demo.chd"

echo "Fetching Yabause freeware Saturn BIOS..."
curl -fL --retry 3 -o "$OUT/saturn_bios.bin" "$YABUSE_URL" \
    || { echo "FATAL: could not fetch BIOS from $YABUSE_URL" >&2; exit 1; }

echo "Fetching 8BitDo Saturn Demo (homebrew CHD)..."
curl -fL --retry 3 -o "$OUT/8b-demo.chd" "$HOMEbrew_URL" \
    || { echo "FATAL: could not fetch homebrew from $HOMEbrew_URL" >&2; exit 1; }

echo
echo "Test ROMs ready under: $OUT"
ls -la "$OUT"