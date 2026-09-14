# AGENTS.md - 8086tiny

## What this is

Single-file C PC XT emulator (~760 lines). **Target: Android 1.6 (API 4)** using **commandergenius/pelya SDL 1.2** port. No submodules, no tests, no CI, no package manager.

## Build - Android 1.6 (Primary Target)

**Required toolchain (project-local only):**
- Android NDK r5b or r5c (last to support API 4 / Android 1.6)
- commandergenius/pelya SDL 1.2 for Android (prebuilt or built from source)
- ARM cross-compiler: `arm-linux-androideabi-gcc`

**Docker approach (recommended):**
```bash
# Use Dockerfile.android16 in project root - contains Ubuntu 10.04/12.04 + NDK r5c + SDL
docker build -f Dockerfile.android16 -t 8086tiny-android16 .
docker run --rm -v $(pwd):/src 8086tiny-android16
```

**Build output:** `libs/armeabi/lib8086tiny.so` + `jni/` wrapper for APK packaging.

## Build - Desktop Linux (for testing)

Requires SDL 1.2 development headers. On Arch/CachyOS: `pacman -S sdl` (provides `sdl-config`).

```bash
make                    # default: graphics + sound, strips binary (SDL 1.2)
make 8086tiny_slowcpu   # slower graphics update (GRAPHICS_UPDATE_DELAY=25000)
make no_graphics        # headless, no SDL needed
make clean              # remove binary
```

The Makefile hardcodes `sdl-config --cflags --libs`. If only SDL2 is available, edit Makefile to use `sdl2-config`.

## Run (Desktop)

Must run from repo root; needs `bios` and `fd.img` in working directory.

```bash
./8086tiny bios fd.img
./8086tiny bios fd.img hd.img   # optional hard disk image
```

`runme` sets terminal to cbreak/raw mode for keyboard passthrough.

## Architecture / editing notes

- **One file:** `8086tiny.c` contains the entire emulator. No separate headers.
- **BIOS:** `bios/` is a precompiled binary blob. Do not edit it; modify `bios_source/` and rebuild if you need to regenerate it (requires `bcc`/`ld86` toolchain).
- **Code style:** heavily macro-driven, C99, size-optimized (`-O3 -fsigned-char`). Existing indentation is 4 spaces.
- **No tests.** Validate changes by building and manually running with the included disk images.
- **Strip:** Makefile runs `strip` on the output. If debugging, bypass with a manual `gcc` invocation.
- **Android specifics:** No `main()` - uses `SDL_main` / `Android_JNI_OnLoad`. Touch input maps to keyboard. Screen orientation: landscape.

## Files you probably don't need to touch

- `docs/` - old website assets
- `fd.img`, `hd.img` - included boot disk images
- `bios_source/` - BIOS source (needs bcc/ld86 to rebuild)
