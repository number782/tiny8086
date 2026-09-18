# 8086tiny Android 1.6 Build Knowledge Base

## Build Command
```bash
# Build, uninstall, install, and launch in one command:
bash /home/andrew/Projects/8086tiny/build.sh
```

## Device Prerequisites (Sharp IS01)
Before launching the app, these files must exist at `/sdcard/8086tiny/`:
- `bios` (65280 bytes)
- `fd.img` (1474560 bytes)

Push with:
```bash
adb push bios /sdcard/8086tiny/
adb push fd.img /sdcard/8086tiny/
```

## Anti-Patterns (Do Not Repeat)
- **Do NOT use separate agents for different phases of work** (e.g., one agent to code, another to build, another to test). All coding, building, and testing must be done within a single agent/tool call sequence. Using separate agents for different phases wastes context, causes file sync issues (e.g., root `8086tiny.c` and `jni/8086tiny.c` can diverge), and leads to unnecessary back-and-forth. The single agent that makes a change MUST also sync both copies of files and verify the build.

## Font Fix (Text Mode Garbling) — 2026-09-12

**Symptom:** Text mode on Android rendered garbled characters; several glyphs were blank.
**Cause:** The inline `font8x8_basic[96][8]` array in `jni/8086tiny.c` (text-mode block ~lines 1057-1154) had blank glyphs for `[` `]` `` ` `` `{` `|` `}` `~` and wrong byte counts for `Z` (7 bytes) and `p` (9 bytes). The separate `jni/font8x8.h` file is dead code — no build source includes it; the compiled font is the inline array only.

**Fix:** Replaced the inline array body with the corrected 96-entry 8x8 font (copied from `jni/font8x8.h`, plus a 96th DEL entry). Canonical build `bash build.sh` produced `8086tiny-debug.apk`. Installed after `adb uninstall com.eight086tiny`.

**Verification:** Test agent confirmed app launches, text mode active (`Screen size: 640x200` = 80x25), no crashes, and the corrected glyph byte patterns are present in the shipped `lib8086tiny.so`.

**Note:** Both copies of the source exist — `jni/8086tiny.c` (compiled by ndk-build) and root `8086tiny.c` (desktop Makefile). Edits must be synced to both or the root copy will diverge.
