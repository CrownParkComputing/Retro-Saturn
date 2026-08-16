#!/usr/bin/env bash
# Pulls the latest ymir-core from upstream StrikerX3/Ymir into the
# vendored tree, then prints a one-line diff summary so Jon can decide
# whether to merge the new SHA or roll back.
#
# This script only matters if we ever vendor ymir-core as a subtree
# (rather than add_subdirectory the in-repo copy that lives at
# libs/ymir-core). For the multiplatform port we use the in-repo copy
# directly, so this script is kept as a future-proofing tool.
#
# Usage:
#   tools/check-ymir-upstream.sh            # show latest upstream SHA
#   tools/check-ymir-upstream.sh update     # pull + show diff summary

set -euo pipefail

UPSTREAM_URL="${YMIR_UPSTREAM_URL:-https://github.com/StrikerX3/Ymir.git}"
LOCAL_DIR="${YMIR_LOCAL_DIR:-$HOME/AndroidStudioProjects/Ymir}"

case "${1:-}" in
    "")
        git ls-remote "$UPSTREAM_URL" HEAD | awk '{print "Latest upstream SHA: " $1}'
        echo "Local HEAD: $(cd "$LOCAL_DIR" && git rev-parse HEAD 2>/dev/null || echo unknown)"
        ;;
    update)
        echo "Fetching upstream + showing diff vs local $LOCAL_DIR ..."
        TMP=$(mktemp -d)
        git clone --depth 1 "$UPSTREAM_URL" "$TMP" >/dev/null
        echo "Latest upstream SHA: $(git -C "$TMP" rev-parse HEAD)"
        diff -rq --exclude=build --exclude=.git --exclude=vcpkg \
            "$TMP/libs/ymir-core" "$LOCAL_DIR/libs/ymir-core" | head -30 || true
        rm -rf "$TMP"
        ;;
    *)
        echo "Usage: $0 [update]" >&2
        exit 1
        ;;
esac