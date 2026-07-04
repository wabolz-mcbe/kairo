#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <ctime>

// Dear ImGui headers
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"

// Global state
static std::atomic<bool> kairo_visible(false);
static bool kairo_initialized = false;
static ImGuiIO* g_io = nullptr;

// Hook for eglSwapBuffers
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay display, EGLSurface surface);
static eglSwapBuffers_t original_eglSwapBuffers = nullptr;

// Keyboard input thread
static std::atomic<bool> input_thread_running(false);
static std::thread* input_thread_handle = nullptr;

// Key state
static std::atomic<bool> o_key_pressed(false);
static bool o_key_was_pressed = false;

void input_thread_func() {
    input_thread_running = true;
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/input/event0");
        return;
    }

    struct input_event ev;
    while (input_thread_running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            // KEY_O is keycode 24 on most Linux input systems
            if (ev.type == EV_KEY && ev.code == 24) {
                o_key_pressed = (ev.value != 0);
            }
        }
        usleep(1000); // 1ms sleep to avoid busy-waiting
    }
    close(fd);
}

void kairo_init() {
    if (kairo_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_io = &io;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.95f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.4f, 0.6f, 0.8f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.4f, 0.6f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 8);

    // Initialize ImGui backends
    ImGui_ImplOpenGL3_Init("#version 100");
    ImGui_ImplAndroid_Init();

    kairo_initialized = true;

    // Start input thread
    input_thread_handle = new std::thread(input_thread_func);
}

void kairo_shutdown() {
    if (!kairo_initialized) return;

    input_thread_running = false;
    if (input_thread_handle) {
        input_thread_handle->join();
        delete input_thread_handle;
        input_thread_handle = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();

    kairo_initialized = false;
}

void kairo_gui() {
    if (!kairo_visible) return;

    ImGuiIO& io = ImGui::GetIO();

    // Set window position and size
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Kairo", nullptr, ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Kairo v1.0");
        ImGui::Separator();

        // Search bar
        static char search_buf[128] = "";
        ImGui::InputTextWithHint("##search", "Search modules...", search_buf, IM_ARRAYSIZE(search_buf));

        ImGui::Separator();

        // Categories with collapsible sections
        if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Visual", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Press O to toggle");
    }
    ImGui::End();
}

EGLBoolean kairo_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    // Initialize ImGui on first frame
    if (!kairo_initialized) {
        kairo_init();
    }

    // Handle O key toggle
    if (o_key_pressed && !o_key_was_pressed) {
        kairo_visible = !kairo_visible;
        o_key_was_pressed = true;
    } else if (!o_key_pressed) {
        o_key_was_pressed = false;
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    // Render Kairo UI
    kairo_gui();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Call original eglSwapBuffers
    return original_eglSwapBuffers(display, surface);
}

// Hook registration
__attribute__((constructor))
void kairo_load() {
    // Get original eglSwapBuffers
    original_eglSwapBuffers = (eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
    if (!original_eglSwapBuffers) {
        perror("Failed to get eglSwapBuffers");
        return;
    }

    // Log that the mod loaded
    const char* home = getenv("HOME");
    if (home) {
        std::string log_path = std::string(home) + "/kairo_loaded.txt";
        FILE* log_file = fopen(log_path.c_str(), "a");
        if (log_file) {
            time_t now = time(nullptr);
            fprintf(log_file, "Kairo mod loaded at %s\n", ctime(&now));
            fclose(log_file);
        }
    }
}

__attribute__((destructor))
void kairo_unload() {
    kairo_shutdown();
}

// Intercept eglSwapBuffers
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    return kairo_eglSwapBuffers(display, surface);
}void input_thread_func() {
    input_thread_running = true;
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/input/event0");
        return;
    }

    struct input_event ev;
    while (input_thread_running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            // KEY_O is keycode 24 on most Linux input systems
            if (ev.type == EV_KEY && ev.code == 24) {
                o_key_pressed = (ev.value != 0);
            }
        }
        usleep(1000); // 1ms sleep to avoid busy-waiting
    }
    close(fd);
}

void kairo_init() {
    if (kairo_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_io = &io;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.95f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.4f, 0.6f, 0.8f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.4f, 0.6f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.5f, 0.7f, 1.0f);
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(8, 8);

    // Initialize ImGui backends
    ImGui_ImplOpenGL3_Init("#version 100");
    ImGui_ImplAndroid_Init();

    kairo_initialized = true;

    // Start input thread
    input_thread_handle = new std::thread(input_thread_func);
}

void kairo_shutdown() {
    if (!kairo_initialized) return;

    input_thread_running = false;
    if (input_thread_handle) {
        input_thread_handle->join();
        delete input_thread_handle;
        input_thread_handle = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();

    kairo_initialized = false;
}

void kairo_gui() {
    if (!kairo_visible) return;

    ImGuiIO& io = ImGui::GetIO();

    // Set window position and size
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Kairo", nullptr, ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Kairo v1.0");
        ImGui::Separator();

        // Search bar
        static char search_buf[128] = "";
        ImGui::InputTextWithHint("##search", "Search modules...", search_buf, IM_ARRAYSIZE(search_buf));

        ImGui::Separator();

        // Categories with collapsible sections
        if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Visual", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("No modules yet");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Press O to toggle");
    }
    ImGui::End();
}

EGLBoolean kairo_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    // Initialize ImGui on first frame
    if (!kairo_initialized) {
        kairo_init();
    }

    // Handle O key toggle
    if (o_key_pressed && !o_key_was_pressed) {
        kairo_visible = !kairo_visible;
        o_key_was_pressed = true;
    } else if (!o_key_pressed) {
        o_key_was_pressed = false;
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    // Render Kairo UI
    kairo_gui();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Call original eglSwapBuffers
    return original_eglSwapBuffers(display, surface);
}

// Hook registration
__attribute__((constructor))
void kairo_load() {
    // Get original eglSwapBuffers
    original_eglSwapBuffers = (eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
    if (!original_eglSwapBuffers) {
        perror("Failed to get eglSwapBuffers");
        return;
    }

    // Log that the mod loaded
    FILE* log_file = fopen(getenv("HOME") ? std::string(std::string(getenv("HOME")) + "/kairo_loaded.txt").c_str() : "/tmp/kairo_loaded.txt", "a");
    if (log_file) {
        time_t now = time(nullptr);
        fprintf(log_file, "Kairo mod loaded at %s\n", ctime(&now));
        fclose(log_file);
    }
}

__attribute__((destructor))
void kairo_unload() {
    kairo_shutdown();
}

// Intercept eglSwapBuffers
EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    return kairo_eglSwapBuffers(display, surface);
}
