adb uninstall com.eight086tiny
nano jni/8086tiny.c
docker run --rm -v /home/andrew/Projects/8086tiny:/src 8086tiny-android16 /bin/bash -c "cd /src && ./build-apk.sh"
adb install -r 8086tiny-debug.apk
adb logcat -c
