#!/usr/bin/env bash
# Cross-builds the iOS arm64 device slice of libymircore.dylib from Linux
# via mobaiapp/iosbox. Run on a host with Docker; needs an `iosbox-sdk`
# named volume (extracted from Xcode) on the host.
#
# Output:
#   native/ymir_core/ios/build/libymircore.dylib
#
# tools/build-ios-linux.sh wraps this and also copies the dylib into
# flutter_app/ios/Frameworks/YmirCore.framework/.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
SDK_VOLUME="${IOSBOX_SDK_VOLUME:-iosbox-sdk}"
IMAGE="${YMIRCORE_IOS_IMAGE:-ymircore-iosbox:latest}"

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$HERE"

run_in_container() {
    docker run --rm \
        -v "$SDK_VOLUME:/root/.iosbox" \
        -v "$REPO_ROOT:/proj" \
        "$IMAGE" bash -lc "$1"
}

# Stage 1: build ymir-core for arm64-apple-ios (~5 min, can be skipped
# with SKIP_CORE=1 to relink the dylib only after bridge changes).
if [ "${SKIP_CORE:-0}" != "1" ]; then
    run_in_container "/proj/native/ymir_core/ios/build-ymir-ios-headless.sh"
fi

# Stage 2: link the dylib.
run_in_container "/proj/native/ymir_core/ios/build-core-ios.sh"

# chown the output so the host UID can read it
docker run --rm -v "$REPO_ROOT:/proj" alpine chown -R "$(id -u):$(id -g)" /proj/native/ymir_core/ios/build

echo
echo "Built: $HERE/build/libymircore.dylib"