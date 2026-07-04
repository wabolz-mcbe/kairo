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

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"

static std::atomic<bool> kairo_visible(false);
static bool kairo_initialized = false;
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay display, EGLSurface surface);
static eglSwapBuffers_t original_eglSwapBuffers = nullptr;
static std::atomic<bool> input_thread_running(false);
static std::thread* input_thread_handle = nullptr;
static std::atomic<bool> o_key_pressed(false);
static bool o_key_was_pressed = false;

void input_thread_func() {
    input_thread_running = true;
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return;
    struct input_event ev;
    while (input_thread_running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev) && ev.type == EV_KEY && ev.code == 24) {
            o_key_pressed = (ev.value != 0);
        }
        usleep(1000);
    }
    close(fd);
}

void kairo_init() {
    if (kairo_initialized) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.95f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.4f, 0.6f, 0.8f);
    ImGui_ImplOpenGL3_Init("#version 100");
    ImGui_ImplAndroid_Init();
    kairo_initialized = true;
    input_thread_handle = new std::thread(input_thread_func);
}

void kairo_gui() {
    if (!kairo_visible) return;
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Kairo")) {
        ImGui::Text("Kairo v1.0");
        ImGui::Separator();
        static char search_buf[128] = "";
        ImGui::InputTextWithHint("##search", "Search...", search_buf, IM_ARRAYSIZE(search_buf));
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::BulletText("Empty");
        if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::BulletText("Empty");
        if (ImGui::CollapsingHeader("Visual", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::BulletText("Empty");
        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::BulletText("Empty");
        if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::BulletText("Empty");
        ImGui::Separator();
        ImGui::TextDisabled("Press O to toggle");
    }
    ImGui::End();
}

EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!kairo_initialized) kairo_init();
    if (o_key_pressed && !o_key_was_pressed) {
        kairo_visible = !kairo_visible;
        o_key_was_pressed = true;
    } else if (!o_key_pressed) {
        o_key_was_pressed = false;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    kairo_gui();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return original_eglSwapBuffers(display, surface);
}

__attribute__((constructor)) void kairo_load() {
    original_eglSwapBuffers = (eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
    const char* home = getenv("HOME");
    if (home) {
        std::string log_path = std::string(home) + "/kairo_loaded.txt";
        FILE* f = fopen(log_path.c_str(), "a");
        if (f) {
            fprintf(f, "Kairo loaded\n");
            fclose(f);
        }
    }
}

__attribute__((destructor)) void kairo_unload() {
    if (input_thread_handle) {
        input_thread_running = false;
        input_thread_handle->join();
        delete input_thread_handle;
    }
}

extern "C" EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    return hook_eglSwapBuffers(display, surface);
}
