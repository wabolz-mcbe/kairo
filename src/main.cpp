#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <unistd.h>
#include <thread>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

// X11 dynamic function mapping definitions
typedef void* (*XOpenDisplay_t)(const char*);
typedef int (*XQueryKeymap_t)(void*, char*);
typedef int (*XCloseDisplay_t)(void*);

static XOpenDisplay_t p_XOpenDisplay = nullptr;
static XQueryKeymap_t p_XQueryKeymap = nullptr;
static XCloseDisplay_t p_XCloseDisplay = nullptr;
static void* x11_lib = nullptr;

// Global state
static std::atomic<bool> kairo_visible(true); // Default to TRUE so it shows right away!
static bool kairo_initialized = false;
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay display, EGLSurface surface);
static eglSwapBuffers_t original_eglSwapBuffers = nullptr;

// Module states
static bool storage_esp_enabled = false;
static bool green_text_enabled = false;

// Storage ESP settings
static float esp_outline_color[3] = {0.0f, 1.0f, 0.5f}; // Cyan
static float esp_outline_thickness = 2.0f;

struct StorageBlock {
    float x, y, z;
    std::string type;
};

static std::vector<StorageBlock> detected_storage_blocks;
static std::atomic<int> scan_count(0);

struct Module {
    std::string name;
    bool* enabled;
    bool has_settings;
};

static std::vector<Module> modules = {
    {"Storage ESP", &storage_esp_enabled, true},
    {"Green Text", &green_text_enabled, false}
};

static bool show_settings[2] = {false, false};

// X11 Global input checker loop thread
void x11_input_thread() {
    // Try to load X11 from standard Linux runtime paths
    x11_lib = dlopen("libX11.so.6", RTLD_LAZY);
    if (!x11_lib) x11_lib = dlopen("libX11.so", RTLD_LAZY);
    if (!x11_lib) return; // Silent fallback if not on Linux desktop

    p_XOpenDisplay = (XOpenDisplay_t)dlsym(x11_lib, "XOpenDisplay");
    p_XQueryKeymap = (XQueryKeymap_t)dlsym(x11_lib, "XQueryKeymap");
    p_XCloseDisplay = (XCloseDisplay_t)dlsym(x11_lib, "XCloseDisplay");

    if (!p_XOpenDisplay || !p_XQueryKeymap || !p_XCloseDisplay) {
        dlclose(x11_lib);
        return;
    }

    void* display = p_XOpenDisplay(nullptr);
    if (!display) return;

    char keys_return[32];
    bool o_was_pressed = false;
    
    // Keycode for 'O' on standard Linux X11 systems is usually 32
    const int X11_KEYCODE_O = 32; 

    while (true) {
        p_XQueryKeymap(display, keys_return);
        
        // Check byte map state of the key code
        bool o_is_pressed = (keys_return[X11_KEYCODE_O / 8] & (1 << (X11_KEYCODE_O % 8))) != 0;

        if (o_is_pressed && !o_was_pressed) {
            kairo_visible = !kairo_visible; // Toggle visibility bypass
        }
        o_was_pressed = o_is_pressed;

        usleep(10000); // Poll every 10ms to keep CPU usage at 0%
    }

    p_XCloseDisplay(display);
    dlclose(x11_lib);
}

const char* tile_entity_strings[] = {
    "minecraft:chest", "minecraft:shulker_box", "minecraft:trapped_chest",
    "minecraft:ender_chest", "minecraft:furnace", "minecraft:blast_furnace",
    "minecraft:barrel", "minecraft:copper_chest", nullptr
};

const char* get_type_name(const char* str) {
    if (strstr(str, "chest")) {
        if (strstr(str, "shulker")) return "Shulker";
        if (strstr(str, "ender")) return "Ender Chest";
        if (strstr(str, "trapped")) return "Trapped Chest";
        if (strstr(str, "copper")) return "Copper Chest";
        return "Chest";
    }
    if (strstr(str, "furnace")) {
        if (strstr(str, "blast")) return "Blast Furnace";
        return "Furnace";
    }
    if (strstr(str, "barrel")) return "Barrel";
    return "Storage";
}

void scan_tile_entities() {
    if (!storage_esp_enabled) {
        detected_storage_blocks.clear();
        return;
    }
    
    detected_storage_blocks.clear();
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return;
    
    char line[512];
    std::vector<std::pair<uintptr_t, uintptr_t>> memory_ranges;
    
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perm[5];
        if (sscanf(line, "%lx-%lx %s", &start, &end, perm) >= 3) {
            if (perm[0] == 'r' && (perm[1] == 'w' || perm[2] == 'x')) {
                memory_ranges.push_back({start, end});
            }
        }
    }
    fclose(maps);
    
    for (auto& range : memory_ranges) {
        if (range.second - range.first > 100 * 1024 * 1024) continue;
        for (uintptr_t addr = range.first; addr < range.second - 64; addr += 4) {
            try {
                float* ptr = (float*)addr;
                if (ptr[0] > -30000.0f && ptr[0] < 30000.0f &&
                    ptr[1] > -256.0f && ptr[1] < 512.0f &&
                    ptr[2] > -30000.0f && ptr[2] < 30000.0f) {
                    
                    for (int offset = -256; offset <= 0; offset += 8) {
                        uintptr_t potential_string = *(uintptr_t*)(addr + offset);
                        if (potential_string < range.first || potential_string > range.second + 1000000) continue;
                        
                        try {
                            const char* str = (const char*)potential_string;
                            for (int i = 0; tile_entity_strings[i]; i++) {
                                if (strstr(str, tile_entity_strings[i])) {
                                    StorageBlock block = {ptr[0], ptr[1], ptr[2], get_type_name(str)};
                                    bool is_duplicate = false;
                                    for (auto& existing : detected_storage_blocks) {
                                        if (std::sqrt(std::pow(existing.x-block.x,2)+std::pow(existing.y-block.y,2)+std::pow(existing.z-block.z,2)) < 0.5f) {
                                            is_duplicate = true; break;
                                        }
                                    }
                                    if (!is_duplicate) detected_storage_blocks.push_back(block);
                                    goto next_check;
                                }
                            }
                        } catch (...) {}
                    }
                }
                next_check:
                if (detected_storage_blocks.size() > 100) goto done_scanning;
            } catch (...) {}
        }
    }
    done_scanning:
    scan_count++;
}

bool world_to_screen(float wx, float wy, float wz, ImVec2& out_screen) {
    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    float sw = (float)viewport[2], sh = (float)viewport[3];
    if (sw <= 0 || sh <= 0) return false;
    out_screen.x = sw * 0.5f + (wx * 10.0f); 
    out_screen.y = sh * 0.5f - (wy * 10.0f);
    return true;
}

void draw_box_3d_imgui(float x, float y, float z, float size, float r, float g, float b, float thickness) {
    float v[8][3] = {
        {x-size, y-size, z-size}, {x+size, y-size, z-size}, {x+size, y+size, z-size}, {x-size, y+size, z-size},
        {x-size, y-size, z+size}, {x+size, y-size, z+size}, {x+size, y+size, z+size}, {x-size, y+size, z+size}
    };
    ImVec2 s[8];
    for (int i = 0; i < 8; i++) if (!world_to_screen(v[i][0], v[i][1], v[i][2], s[i])) return;
    int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.8f));
    for (int i = 0; i < 12; i++) dl->AddLine(s[edges[i][0]], s[edges[i][1]], col, thickness);
}

void kairo_init() {
    if (kairo_initialized) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplOpenGL3_Init("#version 100");
    kairo_initialized = true;

    // Fire off the background X11 system-wide global key reader thread
    std::thread(x11_input_thread).detach();
}

void render_module_item(int idx) {
    Module& mod = modules[idx]; ImGui::PushID(idx);
    std::string label = std::string(*mod.enabled ? "[ON]  " : "[OFF] ") + mod.name;
    if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 30))) *mod.enabled = !(*mod.enabled);
    if (ImGui::IsItemClicked(1)) show_settings[idx] = !show_settings[idx];
    if (show_settings[idx] && mod.has_settings) {
        ImGui::Begin(("##set" + mod.name).c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (idx == 0) { ImGui::ColorEdit3("Color", esp_outline_color); ImGui::SliderFloat("Size", &esp_outline_thickness, 0.5f, 5.0f); }
        ImGui::End();
    }
    ImGui::PopID();
}

void kairo_gui() {
    if (!kairo_visible) return;
    ImGui::SetNextWindowSize(ImVec2(320, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Kairo Premium Overlay", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::Text("KAIRO LOADER"); ImGui::Separator();
        for (int i = 0; i < modules.size(); i++) { render_module_item(i); ImGui::Spacing(); }
        if (storage_esp_enabled) {
            ImGui::Text("Detected Storage: %lu", detected_storage_blocks.size());
            if (ImGui::BeginChild("Blocks", ImVec2(0, 120), true)) {
                for (auto& b : detected_storage_blocks) ImGui::Text("%s: (%.0f, %.0f, %.0f)", b.type.c_str(), b.x, b.y, b.z);
                ImGui::EndChild();
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Press physical 'O' key to hide menu.");
    }
    ImGui::End();
}

EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!kairo_initialized) kairo_init();
    static int frame = 0;
    if (++frame % 2 == 0) scan_tile_entities();
    
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    if (storage_esp_enabled) {
        for (auto& b : detected_storage_blocks) draw_box_3d_imgui(b.x, b.y, b.z, 0.5f, esp_outline_color[0], esp_outline_color[1], esp_outline_color[2], esp_outline_thickness);
    }
    kairo_gui();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return original_eglSwapBuffers(display, surface);
}

__attribute__((constructor)) void kairo_load() {
    original_eglSwapBuffers = (eglSwapBuffers_t)dlsym(RTLD_NEXT, "eglSwapBuffers");
}

extern "C" EGLBoolean eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    return hook_eglSwapBuffers(display, surface);
}
