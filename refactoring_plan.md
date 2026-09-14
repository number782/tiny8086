# 8086tiny JNI Bridge Refactoring Plan

## Current State Analysis

The 8086tiny Android emulator has an incomplete refactoring where the JNI bridge still calls the blocking `main()` function instead of the proper frame-based execution functions (`emulator_init()` and `emulator_step()`).

**Files Involved:**
- `jni/8086tiny.c` - JNI bridge (1227 lines total)
- `jni/8086tiny.c` - Core emulator functions (emulator_init: 47 lines, emulator_step: 353 lines)
- `8086tiny.c` - Desktop build (1068 lines)

**Key Functions:**
- `Java_com_eight086tiny_MainActivity_nativeInit8086()` (lines 436-473) - Calls `main()` (blocking)
- `Java_com_eight086tiny_MainActivity_nativeStepFrame8086()` (lines 476-480) - Only calls `render_text_mode()`
- `emulator_init()` (lines 285-331) - Proper initialization (already working)
- `emulator_step()` (lines 334-687) - Frame-based execution (already working)

## Refactoring Required

### 1. Update JNI Bridge (`jni/8086tiny.c`)

**File: `jni/8086tiny.c`**

**Change nativeInit8086()** (lines 436-473):
```c
// OLD: main(argc, argv);
// NEW: emulator_init(argc, argv);
```

**Change nativeStepFrame8086()** (lines 476-480):
```c
// OLD: render_text_mode();
// NEW: emulator_step(10000);
```

### 2. Remove/Modify Blocking main() Function

**File: `jni/8086tiny.c`**

The current `main()` function (lines 538-1068) contains a blocking infinite loop that needs to be:

**Option A: Keep as stub** (for desktop builds):
```c
int main(int argc, char **argv) {
    // Call emulator_init then loop with emulator_step
    emulator_init(argc, argv);
    while (1) emulator_step(10000);
}
```

**Option B: Remove entirely** (since JNI handles Android):
```c
// Just keep the implementation for reference but comment out actual execution
// int main(int argc, char **argv) {
//     LOGW("main() deprecated - use JNI bridge for Android");
// }
```

### 3. Verify Dependencies

All necessary functions are already implemented:
- `emulator_init()` - Handles BIOS loading, register setup, disk opening
- `emulator_step()` - Handles instruction execution, reset handling, graphics updates
- `reset_requested` - Shared between functions for reset support
- `io_ports[0x64]` - Keyboard controller reset trigger

## Build Steps

1. Edit `jni/8086tiny.c` to update JNI bridge functions
2. Optional: Modify/update `main()` function for desktop compatibility
3. Run `build-apk.sh` to verify the build works
4. Test the APK on device/emulator

## Expected Results

- Android app runs without UI blocking
- Emulator executes in frame-based chunks (~10,000 instructions per frame)
- Reset functionality works (keyboard controller 0xFE to port 0x64)
- Keyboard input works via JNI bridge
- Build produces working APK

## Files to Check After Changes

- `jni/8086tiny.c` - Main JNI bridge and emulator functions
- `build-apk.sh` - Build script (should still work)
- Generated APK files in `bin/` and root directory
- Any documentation mentioning the old architecture

## Timeline

- Analysis: Complete (done)
- Refactoring: 1-2 hours
- Build verification: 30-60 minutes
- Testing: Depends on device availability

The refactoring is straightforward since the frame-based execution functions already exist and only need to be properly wired into the JNI bridge.