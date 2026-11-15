# retro/ — the embedding layer

Everything here is Crown Park Computing's. It turns Ymir from an application
into `libymircore`, a shared library with a plain C ABI that a separate front
end drives over `dart:ffi`.

| Path | What |
|---|---|
| `bridge/` | The C ABI (`ymir_bridge.cpp`), save-state IO, and the audio backends: ALSA, AAudio, CoreAudio, and a silent stub. |
| `linux/` | Host build plus the native test executables. `build-ymir-linux.sh` builds both. |
| `android/` | NDK build, arm64-v8a. Writes to `prebuilt/` unless `JNI_LIBS_DIR` says otherwise. |
| `ios/` | Docker `mobaiapp/iosbox` cross-build scripts. |
| `NATIVE_BUILD.md` | How the native side fits together. |

Ymir itself is not patched by this layer: `Ymir_LIBRARY_ONLY=ON` restricts its
build to `libs/ymir-core`, and everything we add lives in this directory.

## The gate

```sh
./retro/linux/build-ymir-linux.sh
cd retro/linux/build && for t in ymir_*_test; do ./$t || echo "FAIL $t"; done
```

**All nine must pass**, and `libymircore.so` must export 273 symbols, 34 of
them `ymir_bridge_*`. The tests drive the C ABI with no Flutter and no device,
which is the only way to tell a core regression from a front-end one.

## Who consumes this

- **Retro-Saturn** — the Android application.
- **Rhea** — the iOS application.
