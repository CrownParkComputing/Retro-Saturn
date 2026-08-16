# NATIVE_BUILD — Building the C bridge for Linux host + Android + iOS

The ymir-multiport Flutter FE talks to `libymircore.so` /
`libymircore.dylib` via `dart:ffi`. That shared library is built from
`native/ymir_core/bridge/ymir_bridge.cpp` linked against the upstream
**ymir-core** library at `libs/ymir-core/` (vendored in this fork as
the full upstream Ymir repository).

## Linux host build

Prereqs: `cmake ninja-build clang pkg-config libasound2-dev liblzma-dev
libstdc++-12-dev` (Arch: `alsa-lib cmake ninja pkgconf`) plus the
ymir-core vendored at `libs/ymir-core/`.

```bash
native/ymir_core/linux/build-ymir-linux.sh
```

Outputs `native/ymir_core/linux/build/libymircore.so` plus four native
test executables that prove the C ABI in isolation:

- `ymir_smoke_test` — creates an instance, ticks 1s, asserts the
  worker thread is producing frames and the VDP framebuffer is
  non-zero-sized.
- `ymir_peripheral_test` — cycles all 7 peripheral types on both
  ports; round-trips every set/get.
- `ymir_swap_snapshot_test` — proves the bridge-defined save state
  returns `YMIR_ERR_GENERIC` in v1 (ymir-core's `savestate::SaveState`
  has no Serialize API; on-disk save state is a v2 task).
- `ymir_audio_tone_test` — asserts the SCSP sample callback fires and
  the audio ring buffer is draining.

## Android arm64-v8a build

Prereqs: Android NDK 28.2.13676358 (other recent NDKs work; this
matches ViceMultiplatform's choice), `cmake`, JDK 17.

```bash
native/ymir_core/android/build.sh
```

Produces `flutter_app/android/app/src/main/jniLibs/arm64-v8a/libymircore.so`,
which Flutter's `DynamicLibrary.open('libymircore.so')` resolves at
runtime.

## iOS arm64 device build

Prereqs: Docker, an `iosbox-sdk` named volume populated from an
extracted Xcode (see [IOS_BUILD.md](IOS_BUILD.md)).

```bash
tools/build-ios-linux.sh
```

Cross-compiles `libs/ymir-core` for `arm64-apple-ios13.0`, links
`libymircore.dylib`, wraps it in `YmirCore.framework/`, and copies both
into `flutter_app/ios/Frameworks/`.

## iOS Simulator slice (Mac-only)

For running in the simulator (arm64-apple-ios-simulator), the device
slice won't load. Build the simulator slice on a Mac:

```bash
native/ymir_core/ios/build-core-simulator.sh
```

Outputs to `flutter_app/ios/vicecore/iphonesimulator/libymircore.dylib`,
which `tools/run-simulator.sh` swaps into `Runner.app/Frameworks/` before
launch.

## Why no `--wrap=` / `llvm-objcopy --redefine-sym`?

ViceMultiplatform needs them because VICE has ELF symbol overrides
(`archdep_init`, `log_init`, `vsync_do_vsync`, …) that have to be
replaced with bridge shims. ymir-core has **no such overrides** — the
bridge owns the `ymir::Saturn` object directly and registers its own
callbacks via the public `VDP.SetSoftwareRenderCallback(...)` and
`SCSP.SetSampleCallback(...)` APIs. We can link straight through.

## Force-set CMake options

All platform CMakeLists force these upstream Ymir options:

```
Ymir_LIBRARY_ONLY        = ON    # no apps/ymir-sdl3 GUI
Ymir_ENABLE_IPO          = OFF   # no LTO (faster builds, smaller .so)
Ymir_ENABLE_TESTS        = OFF
Ymir_ENABLE_SANDBOX      = OFF
Ymir_ENABLE_YMDASM       = OFF
Ymir_ENABLE_YMIR_HEADLESS= OFF
Ymir_ENABLE_YMIR_DBG     = OFF
Ymir_DEV_BUILD           = OFF   # no devlog spam
Ymir_FF_VIRTUA_GUN        = ON    # required for the Virtua Gun overlay
```

The Virtua Gun flag is what enables `ymir::peripheral::VirtuaGun` in
the peripheral port. Without it the C ABI still works but
`ymir_bridge_set_peripheral_type(port, virtuaGun)` returns
`nullptr` from `ConnectVirtuaGun()`.