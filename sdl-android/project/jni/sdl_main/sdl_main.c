
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include "SDL_version.h"
#include "SDL_thread.h"
#include "SDL_main.h"

#include "SDL_android.h"

#include "jniwrapperstuff.h"

// Weak reference to SDL_main - resolved at runtime via dlsym
static int (*g_SDL_main)(int argc, char **argv) = NULL;

static JNIEnv *static_env = NULL;
static jobject static_thiz = NULL;
static int argc = 0;
static char **argv = NULL;
static int multiThreadedVideo = 0;

static void ensure_SDL_main() {
    if (!g_SDL_main) {
        void* handle = dlopen("lib8086tiny.so", RTLD_LAZY);
        if (handle) {
            g_SDL_main = dlsym(handle, "SDL_main");
            if (!g_SDL_main) {
                __android_log_print(ANDROID_LOG_ERROR, "libSDL", "Failed to find SDL_main in lib8086tiny.so");
            }
            dlclose(handle);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "libSDL", "Failed to dlopen lib8086tiny.so: %s", dlerror());
        }
    }
}

/* JNI-C wrapper stuff */

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    __android_log_print(ANDROID_LOG_INFO, "libSDL", "JNI_OnLoad called");
    return JNI_VERSION_1_6;
}

JNIEnv* SDL_ANDROID_JniEnv()
{
	return static_env;
}
jobject SDL_ANDROID_JniVideoObject()
{
	return static_thiz;
}

extern void SDL_ANDROID_MultiThreadedVideoLoopInit();
extern void SDL_ANDROID_MultiThreadedVideoLoop();

static int threadedMain(void * unused);

int threadedMain(void * unused)
{
    static JNIEnv *JavaEnv = NULL;
    (*SDL_ANDROID_JavaVM())->AttachCurrentThread(SDL_ANDROID_JavaVM(), &JavaEnv, NULL);
    ensure_SDL_main();
    if (g_SDL_main) {
        g_SDL_main( argc, argv );
    }
    __android_log_print(ANDROID_LOG_INFO, "libSDL", "Application closed");
    return 0;
}

JNIEXPORT void JNICALL
JAVA_EXPORT_NAME(MainActivity_nativeInit) (JNIEnv*  env, jobject thiz, jstring jcurdir, jstring cmdline, jint multiThreadedVideo, jint unused)
{
    __android_log_print(ANDROID_LOG_INFO, "libSDL", "nativeInit called");
    int i = 0;
	char curdir[PATH_MAX] = "";
	const jbyte *jstr;
	const char * str = "SDL_app";

	static_env = env;
	static_thiz = thiz;

	strcpy(curdir, "");
	strcat(curdir, SDL_CURDIR_PATH);

	jstr = (*env)->GetStringUTFChars(env, jcurdir, NULL);
	if (jstr != NULL && strlen(jstr) > 0)
		strcpy(curdir, jstr);
	(*env)->ReleaseStringUTFChars(env, jcurdir, jstr);

	chdir(curdir);
	setenv("HOME", curdir, 1);
	__android_log_print(ANDROID_LOG_INFO, "libSDL", "Changing curdir to \"%s\"", curdir);

	jstr = (*env)->GetStringUTFChars(env, cmdline, NULL);

	if (jstr != NULL && strlen(jstr) > 0)
		str = jstr;

	// Count arguments in cmdline
	int arg_count = 0;
	const char *s = str;
	while (*s) {
		if (*s == ' ') arg_count++;
		s++;
	}
	arg_count++; // number of args = spaces + 1
	
	// Allocate argv with room for program name
	argc = arg_count + 1;
	argv = (char **)malloc(argc*sizeof(char *));
	argv[0] = "8086tiny"; // program name
	
	// Parse cmdline into argv[1..]
	char *str1 = strdup(str);
	char *str2 = str1;
	i = 1;
	while(str2 && i < argc)
	{
		argv[i] = str2;
		i++;
		str2 = strchr(str2, ' ');
		if(str2)
			*str2 = 0;
		else
			break;
		str2++;
	}

	for (i = 0; i < argc; i++)
	{
		while( strchr(argv[i], '\t') != NULL )
			strchr(argv[i], '\t')[0] = ' ';
	}

	__android_log_print(ANDROID_LOG_INFO, "libSDL", "Calling SDL_main(\"%s\")", str);

	(*env)->ReleaseStringUTFChars(env, cmdline, jstr);

	for( i = 0; i < argc; i++ )
		__android_log_print(ANDROID_LOG_INFO, "libSDL", "param %d = \"%s\"", i, argv[i]);

if( ! multiThreadedVideo )
{
    ensure_SDL_main();
    if (g_SDL_main) {
        g_SDL_main( argc, argv );
    }
}
else
{
    SDL_ANDROID_MultiThreadedVideoLoopInit();
    SDL_CreateThread(threadedMain, NULL);
    SDL_ANDROID_MultiThreadedVideoLoop();
}
};

JNIEXPORT void JNICALL
JAVA_EXPORT_NAME(MainActivity_nativeKey) ( JNIEnv*  env, jobject thiz, jint keyCode, jint down, jint unicode, jint gamepadId )
{
    __android_log_print(ANDROID_LOG_INFO, "libSDL", "nativeKey: keyCode=%d down=%d unicode=%d", keyCode, down, unicode);
    // Forward to SDL's Android key handler
    extern JNIEXPORT jint JNICALL JAVA_EXPORT_NAME(DemoGLSurfaceView_nativeKey)(JNIEnv*, jobject, jint, jint, jint, jint);
    JAVA_EXPORT_NAME(DemoGLSurfaceView_nativeKey)(env, thiz, keyCode, down, unicode, gamepadId);
}
