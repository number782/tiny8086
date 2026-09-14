# Application.mk for Android 1.6 (API 4) build

# Target Android 1.6 (API level 4)
APP_PLATFORM := android-4

# ARMv5TE (armeabi) for maximum compatibility - Sharp IS01 uses Snapdragon QSD8250 (ARMv6/ARM1136 Scorpion)
# ARMv6 is backward compatible with ARMv5TE, so armeabi runs correctly
APP_ABI := armeabi

# Use GNU STL (statically linked for smaller binary)
APP_STL := gnustl_static

# C++11 support (not strictly needed for C, but good for SDL)
APP_CPPFLAGS := -std=c++11 -frtti -fexceptions

# Optimize for size
APP_CFLAGS := -O3 -fsigned-char
APP_LDFLAGS := -Wl,--gc-sections

# Use toolchain version 4.4.3 (compatible with NDK r5c)
NDK_TOOLCHAIN_VERSION := 4.4.3

# Build both debug and release
APP_OPTIM := release