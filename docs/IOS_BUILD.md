# IOS_BUILD — Cross-building libymircore.dylib from Linux via Docker

The iOS device slice is cross-built from Linux using the
`mobaiapp/iosbox` Docker image (clang targeting arm64-apple-ios +
ld64.lld + the iOS SDK extracted from Xcode in a named Docker volume).
This mirrors the ViceMultiplatform pattern.

## 1. Pull the iosbox image

```bash
docker pull mobaiapp/iosbox:latest
# Or build our thinner image (apt: flex bison file ca-certificates):
docker build -t ymircore-iosbox:latest native/ymir_core/ios/
```

## 2. Create the iosbox-sdk volume

The `mobaiapp/iosbox` image is designed to receive the Apple SDK
through a named Docker volume called `iosbox-sdk`. The standard
recipe (from the `iosbox` project):

```bash
# In a Mac with Xcode installed:
docker volume create iosbox-sdk
CID=$(docker create -v iosbox-sdk:/sdk --rm alpine:latest sleep infinity)
docker cp "$CID".json /tmp  # ignore
# Then push /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform
# /Developer/SDKs/iPhoneOS.sdk into the volume via a helper container.
# See https://github.com/mikecoutermarsh/iosbox for the exact incantation.
```

`native/ymir_core/ios/build.sh` expects this volume to exist.

## 3. Build the arm64 device slice

```bash
tools/build-ios-linux.sh
```

Two stages:

1. **`native/ymir_core/ios/build-ymir-ios-headless.sh`** — cross-compiles
   `libs/ymir-core` for arm64-apple-ios13.0 using clang inside the
   container. Sets `CMAKE_OSX_SYSROOT` to the SDK extracted into the
   volume.
2. **`native/ymir_core/ios/build-core-ios.sh`** — links
   `libymircore.dylib` from the ymir-core static archive + the bridge
   object files. Uses `clang++ -dynamiclib` with `install_name
   "@rpath/YmirCore.framework/YmirCore"`.

Outputs:
- `native/ymir_core/ios/build/libymircore.dylib`
- `flutter_app/ios/Frameworks/YmirCore.framework/YmirCore` (renamed copy)
- `flutter_app/ios/Frameworks/YmirCore.framework/Info.plist`
- `flutter_app/ios/Frameworks/libymircore.dylib` (bare copy for the
  simulator-swap script)

## 4. Build the iOS Simulator slice (Mac-only)

The simulator slice uses `arm64-apple-ios-simulator` SDK and needs the
Mac-bundled `xcrun` + `ld64.lld`:

```bash
native/ymir_core/ios/build-core-simulator.sh
```

Outputs `flutter_app/ios/vicecore/iphonesimulator/libymircore.dylib`
which `tools/run-simulator.sh` swaps into `Runner.app/Frameworks/`
before launch.

## 5. flutter build ios

```bash
cd flutter_app
flutter build ios --release --no-codesign
# Or to produce an IPA:
flutter build ipa --export-method ad-hoc
```

## 6. Install + launch on a device

iOS TAP builds are JIT-only — the kernel kills them in <1s unless a
debugger is attached. Use `tools/device-push.sh --target ios --run`
which delegates to the ViceMultiplatform MobAI + usbmuxd pattern
(`device-push.sh --run` there). For ad-hoc TestFlight builds the
debugger-attach step is skipped automatically.

## Why no `llvm-objcopy --redefine-sym`?

ViceMultiplatform needs it because the VICE bridge shim defines
wrappers for `archdep_init`, `vsync_do_vsync`, etc. Mach-O's ld64 has
no `--wrap=` so the script rewrites symbols in staged static archives
with `llvm-objcopy --redefine-sym` (and falls back to
`ld -r -alias + -unexported_symbol` for Mac-only Xcode).

ymir-core has no equivalent set of overrideable symbols, so no
symbol renaming is needed. The bridge owns the `ymir::Saturn` object
and uses the public callback APIs (`VDP.SetSoftwareRenderCallback`,
`SCSP.SetSampleCallback`, `SMPC.GetPeripheralPort1()` +
`Connect*()`).