# DO NOT TRY THIS - ADB Commands That Don't Work on Sharp IS01 (Android 1.6 / API 4)

## Device: Sharp IS01 (Android 1.6, API Level 4)

### ❌ Commands That FAIL or DON'T WORK

| Command | Why It Fails | Workaround |
|---------|--------------|------------|
| `adb shell am force-stop <package>` | `am` command doesn't have `force-stop` subcommand on Android 1.6 | **Uninstall → wait 20s → reinstall** |
| `adb shell kill <pid>` | No effect / permission denied | Uninstall |
| `adb shell am kill <package>` | Doesn't exist | Uninstall |
| `adb shell pkill <package>` | No pkill command | Uninstall |
| `adb shell am force-stop` + reinstall immediately | App not fully stopped, old process persists | **MUST wait 15-20 seconds after uninstall** |

### ✅ Required Fresh Install Cycle
```bash
adb uninstall com.eight086tiny
sleep 20
adb install -r 8086tiny-debug.apk
adb logcat -c
adb shell am start -n com.eight086tiny/.MainActivity
sleep 10
adb logcat -d
```

### ⚠️ Critical Notes
- **Android 1.6 does NOT support `am force-stop`** - this command was added in later Android versions
- **App processes persist across `am start`** - "Warning: Activity not started, its current task has been brought to the front" means old process still running
- **Must uninstall to truly reset** - wait 15-20s after uninstall for package manager to fully clean up
- **Logcat buffer is small** - clear with `adb logcat -c` immediately before starting app
- **Java Log.i() calls may not appear if native code crashes the thread** - use try/catch/Error handling in Java

### ✅ Working Commands
- `adb uninstall com.eight086tiny` - works
- `adb install -r <apk>` - works  
- `adb shell am start -n <package>/<activity>` - works (but brings existing task to front)
- `adb logcat -c` / `adb logcat -d` - works
- `adb shell ls /sdcard/8086tiny/` - works