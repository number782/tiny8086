#!/bin/bash
# build-apk.sh - Canonical Android 1.6 APK build script for 8086tiny
# This is the ONLY supported way to build the APK. No other scripts are authoritative.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- Configuration ---
NDK_ROOT="${NDK_ROOT:-/opt/android/android-ndk-r5c}"
ANT_DEBUG_OUT="bin/MainActivity-debug.apk"
FINAL_APK="8086tiny-debug.apk"
KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"
KEY_ALIAS="androiddebugkey"
STORE_PASS="android"
KEY_PASS="android"

# --- Pre-flight checks ---
echo "=== 8086tiny APK Build ==="

if [ ! -d "$NDK_ROOT" ]; then
    echo "ERROR: NDK not found at $NDK_ROOT"
    echo "Set NDK_ROOT env var or install NDK r5c to /opt/android/android-ndk-r5c"
    exit 1
fi

if [ ! -x "$NDK_ROOT/ndk-build" ]; then
    echo "ERROR: ndk-build not found/executable at $NDK_ROOT/ndk-build"
    exit 1
fi

# --- Step 1: Clean previous build ---
echo "[1/6] Cleaning previous build..."
rm -rf bin/ libs/ obj/ gen/
mkdir -p bin/ libs/ obj/ gen/
# NDK r5c workaround: ensure intermediate dirs exist for dependency files (.org)
mkdir -p obj/local/armeabi/objs/sdl-1.2/src/ obj/local/armeabi/objs/sdl-1.2/src/audio obj/local/armeabi/objs/sdl-1.2/src/cdrom obj/local/armeabi/objs/sdl-1.2/src/cpuinfo obj/local/armeabi/objs/sdl-1.2/src/events obj/local/armeabi/objs/sdl-1.2/src/file obj/local/armeabi/objs/sdl-1.2/src/haptic obj/local/armeabi/objs/sdl-1.2/src/joystick obj/local/armeabi/objs/sdl-1.2/src/stdlib obj/local/armeabi/objs/sdl-1.2/src/thread obj/local/armeabi/objs/sdl-1.2/src/timer obj/local/armeabi/objs/sdl-1.2/src/video obj/local/armeabi/objs/sdl-1.2/src/main obj/local/armeabi/objs/sdl-1.2/src/power obj/local/armeabi/objs/sdl-1.2/src/thread/pthread obj/local/armeabi/objs/sdl-1.2/src/timer/unix obj/local/armeabi/objs/sdl-1.2/src/audio/android obj/local/armeabi/objs/sdl-1.2/src/cdrom/dummy obj/local/armeabi/objs/sdl-1.2/src/video/android obj/local/armeabi/objs/sdl-1.2/src/video/android/cpp obj/local/armeabi/objs/sdl-1.2/src/haptic/dummy obj/local/armeabi/objs/sdl-1.2/src/loadso/dlopen obj/local/armeabi/objs/sdl-1.2/src/atomic/dummy

# --- Step 2: Remove stale auto-generated files ---
echo "[2/6] Removing stale generated files..."
rm -f src/com/eight086tiny/R.java
rm -f gen/com/eight086tiny/R.java

# --- Step 3: Ensure local.properties points to SDK ---
echo "[3/6] Configuring local.properties..."
if [ -n "${ANDROID_HOME:-}" ]; then
    echo "sdk.dir=$ANDROID_HOME" > local.properties
elif [ -n "${SDK_DIR:-}" ]; then
    echo "sdk.dir=$SDK_DIR" > local.properties
else
    # Try to find SDK in common locations
    for candidate in /opt/android-sdk/android-sdk-linux "$HOME/Android/Sdk" "$HOME/android-sdk"; do
        if [ -d "$candidate" ]; then
            echo "sdk.dir=$candidate" > local.properties
            break
        fi
    done
fi

if [ ! -f local.properties ]; then
    echo "ERROR: Cannot determine SDK location. Set ANDROID_HOME or SDK_DIR."
    exit 1
fi
echo "      SDK: $(grep sdk.dir local.properties | cut -d= -f2)"

# --- Step 4: Native build ---
echo "[4/6] Building native libraries (ndk-build)..."
"$NDK_ROOT/ndk-build" -C "$SCRIPT_DIR/jni"

# Verify lib was built
if [ ! -f "libs/armeabi/lib8086tiny.so" ]; then
    echo "ERROR: lib8086tiny.so not built!"
    exit 1
fi
echo "      lib8086tiny.so: $(ls -lh libs/armeabi/lib8086tiny.so | awk '{print $5}')"

# --- Step 5: Java/APK build ---
echo "[5/6] Building APK (ant debug)..."
ant debug

if [ ! -f "$ANT_DEBUG_OUT" ]; then
    echo "ERROR: ant debug did not produce $ANT_DEBUG_OUT"
    exit 1
fi
echo "      Unsigned APK: $(ls -lh "$ANT_DEBUG_OUT" | awk '{print $5}')"

# --- Step 6: Sign and align ---
echo "[6/6] Signing and aligning APK..."

# Create debug keystore if missing
if [ ! -f "$KEYSTORE" ]; then
    echo "      Creating debug keystore..."
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkey -v \
        -keystore "$KEYSTORE" \
        -alias "$KEY_ALIAS" \
        -storepass "$STORE_PASS" \
        -keypass "$KEY_PASS" \
        -keyalg RSA \
        -keysize 2048 \
        -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" \
        >/dev/null 2>&1
fi

# Sign with jarsigner - MUST use SHA1 for Android 1.6 (API 4) compatibility.
# Modern JDKs default to SHA256/SHA384 which PackageParser rejects on old devices.
echo "      Signing with jarsigner (SHA1)..."
jarsigner -verbose \
    -digestalg SHA1 \
    -sigalg SHA1withRSA \
    -keystore "$KEYSTORE" \
    -storepass "$STORE_PASS" \
    -keypass "$KEY_PASS" \
    "$ANT_DEBUG_OUT" \
    "$KEY_ALIAS" \
    2>&1 | grep -E "^(signing|jar|echo)" || true

# Align with zipalign (try both possible locations)
ZIPALIGN=""
for candidate in "$ANDROID_HOME/build-tools/19.1.0/zipalign" "$ANDROID_HOME/tools/zipalign" "${SDK_HOME:-}/tools/zipalign" "${SDK_ROOT:-}/tools/zipalign"; do
    if [ -x "$candidate" ]; then
        ZIPALIGN="$candidate"
        break
    fi
done

if [ -n "$ZIPALIGN" ]; then
    echo "      Aligning with zipalign..."
    rm -f "$FINAL_APK" "$FINAL_APK.aligning"
    "$ZIPALIGN" -f -v 4 "$ANT_DEBUG_OUT" "$FINAL_APK.aligning"
    mv "$FINAL_APK.aligning" "$FINAL_APK"
    "$ZIPALIGN" -c -v 4 "$FINAL_APK"
else
    echo "      WARNING: zipalign not found, using signed but unaligned APK"
    cp "$ANT_DEBUG_OUT" "$FINAL_APK"
fi

echo ""
echo "=== Build Complete ==="
echo "APK: $SCRIPT_DIR/$FINAL_APK"
echo "Size: $(ls -lh "$FINAL_APK" | awk '{print $5}')"
echo ""
echo "To install on device:"
echo "  adb uninstall com.eight086tiny   # <-- ALWAYS uninstall first on Android 1.6"
echo "  adb install -r $FINAL_APK"
echo ""
echo "To launch:"
echo "  adb shell am start -n com.eight086tiny/.MainActivity"
