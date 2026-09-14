#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("Testing 8086tiny emulator\n");
    
    // Set working directory
    chdir("/sdcard/8086tiny");
    setenv("HOME", "/sdcard/8086tiny", 1);
    
    // Load libraries in dependency order
    void *sdl_handle = dlopen("/data/local/tmp/libsdl-1.2.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!sdl_handle) {
        printf("Failed to load libsdl-1.2.so: %s\n", dlerror());
        return 1;
    }
    printf("Loaded libsdl-1.2.so\n");
    
    void *sdl_main_handle = dlopen("/data/local/tmp/libsdl_main.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!sdl_main_handle) {
        printf("Failed to load libsdl_main.so: %s\n", dlerror());
        return 1;
    }
    printf("Loaded libsdl_main.so\n");
    
    void *tiny_handle = dlopen("/data/local/tmp/lib8086tiny.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!tiny_handle) {
        printf("Failed to load lib8086tiny.so: %s\n", dlerror());
        return 1;
    }
    printf("Loaded lib8086tiny.so\n");
    
    // Find SDL_main
    int (*SDL_main)(int, char**) = dlsym(tiny_handle, "SDL_main");
    if (!SDL_main) {
        printf("Failed to find SDL_main: %s\n", dlerror());
        return 1;
    }
    printf("Found SDL_main\n");
    
    // Call SDL_main
    static int sdl_argc = 3;
    static char *sdl_argv[3] = {"8086tiny", "bios", "fd.img"};
    
    printf("Calling SDL_main...\n");
    return SDL_main(sdl_argc, sdl_argv);
}
