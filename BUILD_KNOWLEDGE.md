# 8086tiny Android 1.6 Build Knowledge Base

## Canonical Build Command
```bash
bash /home/andrew/Projects/8086tiny/build-apk.sh
```
This script handles: clean → ndk-build → ant debug → jarsigner → zipalign.

## Known Failure Modes & Fixes

### 9. JNI method name conflict with sdl_main
**Symptom:** `nativeInit` blocks the Java thread; no renderFrame logs; screen stays black.
**Cause:** Both `sdl_main` and `8086tiny` libraries export `Java_com_eight086tiny_MainActivity_nativeInit`. Because `sdl_main` is loaded first (`System.loadLibrary("sdl_main")`), its `nativeInit` is called. That function invokes `SDL_main` synchronously, so the Java thread never enters the render loop.
**Fix:** Rename 8086tiny JNI methods to avoid the conflict:
- `nativeInit` → `nativeInit8086`
- `nativeStepFrame` → `nativeStepFrame8086`
- `nativeGetFramebuffer` → `nativeGetFramebuffer8086`
- `nativeGetScreenSize` → `nativeGetScreenSize8086`
Also add `chdir(curdir)` in `nativeInit8086` so file opens resolve correctly on Android.

### 10. Always uninstall before installing
**Symptom:** `adb install -r` fails with `INSTALL_PARSE_FAILED_UNEXPECTED_EXCEPTION` after rebuild.
**Fix:** Always uninstall first: `adb uninstall com.eight086tiny && adb install -r APK_PATH`. The `-r` flag alone is unreliable on Android 1.6 after rebuilds.

## Install Procedure (Android 1.6)
**Always uninstall before installing a new build:**
```bash
adb uninstall com.eight086tiny
adb install -r /home/andrew/Projects/8086tiny/8086tiny-debug.apk
adb shell am start -n com.eight086tiny/.MainActivity
```
The `-r` flag alone is unreliable on Android 1.6 after rebuilds.

### 1. R.java conflict
**Symptom:** `aapt` fails with duplicate class error for `R`.
**Cause:** Stale hand-committed `src/com/eight086tiny/R.java` conflicts with aapt-generated `gen/com/eight086tiny/R.java`.
**Fix:** Delete `src/com/eight086tiny/R.java` before building. The canonical script does this automatically in step 2.

### 2. INSTALL_PARSE_FAILED_UNEXPECTED_EXCEPTION
**Symptom:** `adb install` fails on Android 1.6 even though APK builds successfully.
**Cause:** APK not properly signed/aligned for legacy Android package manager.
**Fix:** Must sign with `jarsigner` AND align with `zipalign -v 4`. The canonical script does both in step 6.

### 3. zipalign location
**Symptom:** `zipalign: command not found`
**Cause:** Legacy SDK Tools r25.2.5 puts zipalign at `$ANDROID_HOME/tools/zipalign`, not in build-tools.
**Fix:** The canonical script searches multiple candidate paths. If not found, it uses the signed-but-unaligned APK as fallback.

### 4. NDK toolchain
**Toolchain:** Android NDK r5c (last to support API 4 / Android 1.6)
**Standalone toolchain:** `/opt/android/android-ndk-r5c/build/tools/make-standalone-toolchain.sh --platform=android-4 --toolchain=arm-linux-androideabi-4.4.3 --install-dir=/opt/android-toolchain`
**Compiler:** `arm-linux-androideabi-gcc`

### 5. Docker build environment
**Preferred image:** `8086tiny-apk` (built from `Dockerfile.apk`)
**Contents:** Ubuntu 12.04 + NDK r5c + standalone ARM toolchain + SDK Tools r25.2.5 + Ant + Java 6 + zipalign + keytool + jarsigner
**Volume mount:** `-v /home/andrew/Projects/8086tiny:/src`
**Build command inside container:** `bash /src/build-apk.sh`

### 6. APK output
**Unsigned:** `bin/MainActivity-debug.apk` (from `ant debug`)
**Signed + aligned:** `8086tiny-debug.apk` (project root, produced by canonical script)
**Install procedure:** ALWAYS uninstall first on Android 1.6:
```bash
adb uninstall com.eight086tiny
adb install -r /home/andrew/Projects/8086tiny/8086tiny-debug.apk
```

### 7. Device prerequisites (Sharp IS01)
Before launching the app, these files must exist at `/sdcard/8086tiny/`:
- `bios` (65280 bytes)
- `fd.img` (1474560 bytes)
Push with: `adb push bios /sdcard/8086tiny/` and `adb push fd.img /sdcard/8086tiny/`

### 8. Code changes that must not be lost
- `8086tiny.c`: Text mode framebuffer rendering added for Android (lines ~927-980)
- `8086tiny.c`: Duplicate `#ifdef ANDROID` block removed (only one JNI block at line 36)
- `8086tiny.c`: Syntax error fixed (stray `}` removed from if/else chain around display rendering)
- `sdl-android/project/jni/sdl_main/sdl_main.c`: argv parsing fixed (argv[0]="8086tiny", proper space-splitting)
- `src/com/eight086tiny/MainActivity.java`: Canvas renderer + SurfaceView + background thread

## Install Procedure (Android 1.6)
**Always uninstall before installing a new build:**
```bash
adb uninstall com.eight086tiny
adb install -r /home/andrew/Projects/8086tiny/8086tiny-debug.apk
adb shell am start -n com.eight086tiny/.MainActivity
```
The `-r` flag alone is unreliable on Android 1.6 after rebuilds.

## Docker Image Tool Dependencies
If an agent gets "command not found" for a tool inside the `8086tiny-apk` Docker image, add it to `Dockerfile.apk`'s `apt-get install` list and rebuild the image with:
```bash
docker build -f Dockerfile.apk -t 8086tiny-apk .
```
Known required tools: `keytool`, `jarsigner`, `zipalign`, `ant`, `ndk-build`, `adb`.

## Font Fix (Text Mode Garbling) — 2026-09-12

**Symptom:** Text mode on Android rendered garbled characters; several glyphs were blank.
**Cause:** The inline `font8x8_basic[96][8]` array in `jni/8086tiny.c` (text-mode block ~lines 1057-1154) had blank glyphs for `[` `]` `` ` `` `{` `|` `}` `~` and wrong byte counts for `Z` (7 bytes) and `p` (9 bytes). The separate `jni/font8x8.h` file is dead code — no build source includes it; the compiled font is the inline array only.

**Fix:** Replaced the inline array body with the corrected 96-entry 8x8 font (copied from `jni/font8x8.h`, plus a 96th DEL entry). Canonical build `bash build-apk.sh` (ndk-build → ant debug → jarsigner SHA1 → zipalign) produced `8086tiny-debug.apk`. Installed after `adb uninstall com.eight086tiny`.

**Verification:** Test agent confirmed app launches, text mode active (`Screen size: 640x200` = 80x25), no crashes, and the corrected glyph byte patterns are present in the shipped `lib8086tiny.so`.

**Note:** Both copies of the source exist — `jni/8086tiny.c` (compiled by ndk-build) and root `8086tiny.c` (desktop Makefile). Edits must be synced to both or the root copy will diverge.

## Anti-Patterns (Do Not Repeat)
- Do NOT build APK with modern `zip`/`jarsigner` manually — use the canonical script.
- Do NOT commit auto-generated files (`R.java`, `BuildConfig.java`) to git.
- Do NOT attempt `ant debug` without first cleaning `bin/`, `libs/`, `obj/`, `gen/`.
- Do NOT use Dockerfile.android16 (native build only) — use Dockerfile.apk or Dockerfile.android16-full for full APK.
- **Do NOT use separate agents for different phases of work** (e.g., one agent to code, another to build, another to test). All coding, building, and testing must be done within a single agent/tool call sequence. Using separate agents for different phases wastes context, causes file sync issues (e.g., root `8086tiny.c` and `jni/8086tiny.c` can diverge), and leads to unnecessary back-and-forth. The single agent that makes a change MUST also sync both copies of files and verify the build.
- Do NOT attempt `adb screencap` over ADB — it has failed consistently since the first attempts and wastes time/tokens. Use logcat text output for verification instead.

## Progress Log (for session continuity)
**File:** `/home/andrew/Projects/8086tiny/progress.log` — tracks next steps for agents to resume after timeout.

**Current Issue (2026-09-13):** Emulator runs but BIOS halts after ~90ms. Architecture wrong: `main()` runs infinite loop blocking Java thread. Must restructure:
- `nativeInit8086()` → initialization only (SDL, BIOS load, register setup)
- `nativeStepFrame8086()` → execute N instructions per frame, return to Java

**Next Steps:**
1. Refactor `jni/8086tiny.c`: split `main()` into `emulator_init()` + `emulator_step(inst_count)`
2. Update `nativeInit8086()` to call `emulator_init()`
3. Update `nativeStepFrame8086()` to call `emulator_step(10000)` + `render_text_mode()`
4. Sync changes to root `8086tiny.c`
5. Build APK and test on device
