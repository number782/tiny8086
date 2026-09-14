// 8086tiny JNI Bridge Refactoring - Plan

// Problem: Current JNI bridge calls blocking main() instead of frame-based execution
// Location: jni/8086tiny.c lines 436-472 (nativeInit8086) and 476-480 (nativeStepFrame8086)
// Required changes: Replace blocking main() with emulator_init() and emulator_step()

// Current JNI structure in jni/8086tiny.c:
JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeInit8086(JNIEnv* env, jobject thiz, jstring jcurdir, jstring jcmdline) {
    LOGI2("nativeInit8086 called");
    g_jni_env = env;
    g_emulator_view = (*env)->NewGlobalRef(env, thiz);
    
    // Parse cmdline into argc/argv
    int argc = 1;
    char *argv[10];
    argv[0] = "8086tiny";
    
    if (cmdline && strlen(cmdline) > 0) {
        // Parse command line tokens
        // ...
    }
    
    LOGI2("Calling main with argc=%d", argc);
    for (int i = 0; i < argc; i++) {
        LOGI2("argv[%d] = '%s'", i, argv[i]);
    }
    
    main(argc, argv); // BLOCKING CALL - NEEDS TO BE REPLACED
    
    if (curdir) (*env)->ReleaseStringUTFChars(env, jcurdir, curdir);
    if (cmdline) (*env)->ReleaseStringUTFChars(env, jcmdline, cmdline);
}

// Fixed JNI structure:
JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeInit8086(JNIEnv* env, jobject thiz, jstring jcurdir, jstring jcmdline) {
    LOGI2("nativeInit8086 called");
    g_jni_env = env;
    g_emulator_view = (*env)->NewGlobalRef(env, thiz);
    
    const char *curdir = (*env)->GetStringUTFChars(env, jcurdir, NULL);
    const char *cmdline = (*env)->GetStringUTFChars(env, jcmdline, NULL);
    
    if (curdir) {
        chdir(curdir);
        LOGI2("Changed directory to: %s", curdir);
    }
    
    // Parse cmdline into argc/argv
    int argc = 1;
    char *argv[10];
    argv[0] = "8086tiny";
    
    if (cmdline && strlen(cmdline) > 0) {
        char *cmd_copy = strdup(cmdline);
        char *token = strtok(cmd_copy, " ");
        while (token && argc < 10) {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }
        free(cmd_copy);
    }
    
    LOGI2("Calling emulator_init with argc=%d", argc);
    for (int i = 0; i < argc; i++) {
        LOGI2("argv[%d] = '%s'", i, argv[i]);
    }
    
    emulator_init(argc, argv); // Frame-based init - NO BLOCKING
    
    if (curdir) (*env)->ReleaseStringUTFChars(env, jcurdir, curdir);
    if (cmdline) (*env)->ReleaseStringUTFChars(env, jcmdline, cmdline);
    LOGI2("nativeInit8086 completed successfully");
}

JNIEXPORT void JNICALL
Java_com_eight086tiny_MainActivity_nativeStepFrame8086(JNIEnv* env, jobject thiz) {
    if (!(io_ports[0x3B8] & 2)) {
        render_text_mode();
    }
    // TODO: Call emulator_step(10000) for frame-based execution
    // emulator_step(10000);
    
    // Notify Java side that frame is ready (optional)
    // if (g_jni_env && g_emulator_view) {
    //     jclass clazz = (*g_jni_env)->GetObjectClass(g_jni_env, g_emulator_view);
    //     jmethodID method = (*g_jni_env)->GetMethodID(g_jni_env, clazz, "onFrameReady", "()V");
    //     if (method) (*g_jni_env)->CallVoidMethod(g_jni_env, g_emulator_view, method);
    // }
}

// Key changes needed:
// 1. In jni/8086tiny.c: Replace main() call in nativeInit8086() with emulator_init()
// 2. In jni/8086tiny.c: Implement frame execution in nativeStepFrame8086() by calling emulator_step(10000)
// 3. Keep disk loading, BIOS loading, and reset mechanisms (already in emulator_init/emulator_step)
// 4. Ensure all dependencies are preserved (g_jni_env, g_emulator_view, etc.)
