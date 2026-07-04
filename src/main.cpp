// Kairo - QoL client for mcpelauncher
//
// Two independent pieces, deliberately decoupled from Minecraft's own
// internals so we don't have to guess at unverified engine symbols:
//
//   1. RENDERING: we intercept eglSwapBuffers, the standard EGL function
//      the game calls once per frame to present it. This is public,
//      documented API - not a guessed internal symbol.
//
//   2. INPUT: we read the "O" key straight from the Linux kernel's evdev
//      layer (/dev/input/eventX), independent of whatever input path
//      Minecraft itself uses. This needs your user account to have read
//      access to input devices (see README).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <thread>
#include <unistd.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "kairo_gui.h"

namespace {

FILE* LogFile() {
    static FILE* f = [] {
        const char* home = getenv("HOME");
        if (!home) home = "/tmp";
        char path[512];
        snprintf(path, sizeof(path), "%s/kairo_log.txt", home);
        return fopen(path, "a");
    }();
    return f;
}

void Log(const char* msg) {
    FILE* f = LogFile();
    if (!f) return;
    time_t now = time(nullptr);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&now));
    fprintf(f, "[%s] %s\n", timebuf, msg);
    fflush(f);
}

// ---------------------------------------------------------------------
// Input: poll every readable /dev/input/eventX for the "O" key.
// ---------------------------------------------------------------------

void WatchKeyboardFor(int fd) {
    struct input_event ev;
    while (true) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.code == KEY_O && ev.value == 1) {
                // value == 1 is "key down" (0 = up, 2 = auto-repeat).
                bool newState = !kairo::g_visible.load();
                kairo::g_visible.store(newState);
                Log(newState ? "O pressed: showing Kairo" : "O pressed: hiding Kairo");
            }
        } else if (n < 0) {
            // No data right now; avoid busy-spinning.
            usleep(2000);
        }
    }
}

void StartInputWatcherThreads() {
    DIR* dir = opendir("/dev/input");
    if (!dir) {
        Log("Could not open /dev/input - check permissions (see README).");
        return;
    }
    struct dirent* entry;
    int opened = 0;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue; // skip devices we can't read
        opened++;
        std::thread(WatchKeyboardFor, fd).detach();
    }
    closedir(dir);

    char msg[128];
    snprintf(msg, sizeof(msg), "Watching %d input device(s) for the O key.", opened);
    Log(msg);
    if (opened == 0) {
        Log("No input devices were readable. Run: sudo usermod -aG input $USER, then log out/in.");
    }
}

// ---------------------------------------------------------------------
// Rendering: intercept eglSwapBuffers.
// ---------------------------------------------------------------------

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
SwapBuffersFn g_realSwapBuffers = nullptr;

bool g_imguiInitialized = false;
kairo::KairoGui* g_gui = nullptr;

void EnsureImGuiInitialized(EGLDisplay dpy, EGLSurface surface) {
    if (g_imguiInitialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplOpenGL3_Init("#version 100");

    EGLint width = 0, height = 0;
    eglQuerySurface(dpy, surface, EGL_WIDTH, &width);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &height);
    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;
    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);

    g_gui = new kairo::KairoGui();
    g_imguiInitialized = true;
    Log("ImGui initialized inside eglSwapBuffers hook.");
}

} // namespace

extern "C" __attribute__((visibility("default")))
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    static long callCount = 0;
    callCount++;
    if (callCount == 1) {
        Log("eglSwapBuffers hook reached for the first time.");
    } else if (callCount % 300 == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "eglSwapBuffers hook still active (call #%ld).", callCount);
        Log(msg);
    }

    if (!g_realSwapBuffers) {
        g_realSwapBuffers = (SwapBuffersFn)dlsym(RTLD_NEXT, "eglSwapBuffers");
        if (!g_realSwapBuffers) {
            Log("FATAL: could not find real eglSwapBuffers via dlsym.");
        }
    }

    EnsureImGuiInitialized(dpy, surface);

    if (g_imguiInitialized) {
        EGLint width = 0, height = 0;
        eglQuerySurface(dpy, surface, EGL_WIDTH, &width);
        eglQuerySurface(dpy, surface, EGL_HEIGHT, &height);
        ImGuiIO& io = ImGui::GetIO();
        if (width > 0 && height > 0) io.DisplaySize = ImVec2((float)width, (float)height);

        static struct timespec lastTime = {0, 0};
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (lastTime.tv_sec != 0) {
            double dt = (now.tv_sec - lastTime.tv_sec) +
                        (now.tv_nsec - lastTime.tv_nsec) / 1e9;
            io.DeltaTime = dt > 0.0001 ? (float)dt : 0.0001f;
        } else {
            io.DeltaTime = 1.0f / 60.0f;
        }
        lastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        if (g_gui) g_gui->Render();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    if (g_realSwapBuffers) {
        return g_realSwapBuffers(dpy, surface);
    }
    return EGL_FALSE;
}

__attribute__((constructor))
static void kairo_on_load() {
    Log("Kairo mod loaded.");

    // If mcpelauncher loaded us with RTLD_LOCAL, our exported symbols
    // (like eglSwapBuffers) would be invisible to other already-loaded
    // modules' symbol lookups, silently breaking interposition. Re-opening
    // ourselves with RTLD_GLOBAL | RTLD_NOLOAD promotes us into the
    // process-wide global scope without loading a second copy.
    Dl_info info;
    if (dladdr((void*)&kairo_on_load, &info) && info.dli_fname) {
        void* h = dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
        if (h) {
            Log("Promoted Kairo to RTLD_GLOBAL scope.");
        } else {
            Log("Could not promote to RTLD_GLOBAL (dlopen/NOLOAD failed).");
        }
    } else {
        Log("dladdr could not determine our own module path.");
    }

    std::thread(StartInputWatcherThreads).detach();
}
