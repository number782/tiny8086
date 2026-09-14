LOCAL_PATH := $(call my-dir)

# Required by SDL build system
SDL_JAVA_PACKAGE_PATH := com_eight086tiny
SDL_CURDIR_PATH := /sdcard/8086tiny
SDL_TRACKBALL_KEYUP_DELAY := 100
SDL_VIDEO_RENDER_RESIZE_KEEP_ASPECT := 1
SDL_VIDEO_RENDER_RESIZE := 1

# Save LOCAL_PATH
MY_LOCAL_PATH := $(LOCAL_PATH)

# Build SDL 1.2 as shared library by including its Android.mk
# Define OpenGL ES version for Android
SDL_ADDITIONAL_CFLAGS := -DSDL_VIDEO_OPENGL_ES_VERSION=1 -std=c99
APPLICATION_GLES_LIBRARY := -lGLESv1_CM
SDL_PATH := $(MY_LOCAL_PATH)/../sdl-android/project/jni/sdl-1.2
include $(SDL_PATH)/Android.mk

# Restore LOCAL_PATH
LOCAL_PATH := $(MY_LOCAL_PATH)

# Build sdl_main
include $(CLEAR_VARS)

LOCAL_MODULE := sdl_main

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../sdl-android/project/jni/sdl-1.2/include
LOCAL_CFLAGS := -DSDL_JAVA_PACKAGE_PATH=$(SDL_JAVA_PACKAGE_PATH) -DSDL_CURDIR_PATH=\"$(SDL_CURDIR_PATH)\" -O3 -fsigned-char

LOCAL_SRC_FILES := ../sdl-android/project/jni/sdl_main/sdl_main.c

LOCAL_SHARED_LIBRARIES := sdl-1.2
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)

# Build 8086tiny
include $(CLEAR_VARS)

LOCAL_MODULE := 8086tiny
LOCAL_SRC_FILES := 8086tiny.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../sdl-android/project/jni/sdl-1.2/include
LOCAL_CFLAGS := -O3 -fsigned-char -std=c99 -DANDROID -D__ANDROID__ -DNO_MAIN -DGRAPHICS_UPDATE_DELAY=360000 -DLOG_TAG=\"8086tiny\"
LOCAL_LDFLAGS := -Wl,--no-gc-sections
LOCAL_SHARED_LIBRARIES := sdl-1.2
LOCAL_LDLIBS := -lGLESv1_CM -llog

include $(BUILD_SHARED_LIBRARY)