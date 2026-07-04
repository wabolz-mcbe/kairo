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
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

// Global state
static std::atomic<bool> kairo_visible(false);
static bool kairo_initialized = false;
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay display, EGLSurface surface);
static eglSwapBuffers_t original_eglSwapBuffers = nullptr;

// Key state
static std::atomic<bool> o_key_pressed(false);
static bool o_key_was_pressed = false;

// Module states
static bool storage_esp_enabled = false;
static bool green_text_enabled = false;

// Storage ESP settings
static float esp_outline_color[3] = {0.0f, 1.0f, 0.5f}; // Cyan
static float esp_outline_thickness = 2.0f;

// Detected storage blocks
struct StorageBlock {
    float x, y, z;
    std::string type;
};

static std::vector<StorageBlock> detected_storage_blocks;
static std::atomic<int> scan_count(0);

// Module structs
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

void input_thread_func() {
    int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return;
    
    struct input_event ev;
    while (true) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.code == 24) {
                o_key_pressed = (ev.value != 0);
            }
        }
        usleep(1000);
    }
    close(fd);
}

// Known tile entity type strings in the binary
const char* tile_entity_strings[] = {
    "minecraft:chest",
    "minecraft:shulker_box",
    "minecraft:trapped_chest",
    "minecraft:ender_chest",
    "minecraft:furnace",
    "minecraft:blast_furnace",
    "minecraft:barrel",
    "minecraft:copper_chest",
    nullptr
};

// Map string to display name
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

// Memory scanner that looks for tile entity patterns
void scan_tile_entities() {
    if (!storage_esp_enabled) {
        detected_storage_blocks.clear();
        return;
    }
    
    detected_storage_blocks.clear();
    
    // Get heap memory range
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return;
    
    char line[512];
    std::vector<std::pair<uintptr_t, uintptr_t>> memory_ranges;
    
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start, end;
        char perm[5], path[256];
        path[0] = 0;
        
        if (sscanf(line, "%lx-%lx %s %*s %*s %*s %s", &start, &end, perm, path) >= 3) {
            // Scan readable, writable memory (heap, stack, mmap regions where game data lives)
            if (perm[0] == 'r' && (perm[1] == 'w' || perm[2] == 'x')) {
                memory_ranges.push_back({start, end});
            }
        }
    }
    fclose(maps);
    
    if (memory_ranges.empty()) return;
    
    // Pattern: tile entity data typically contains:
    // - A pointer to a string (type identifier)
    // - Coordinates as floats or doubles
    // - Some padding/metadata
    
    for (auto& range : memory_ranges) {
        if (range.second - range.first > 100 * 1024 * 1024) {
            // Skip huge ranges to avoid scanning forever
            continue;
        }
        
        for (uintptr_t addr = range.first; addr < range.second - 64; addr += 4) {
            try {
                // Look for patterns of float triples that look like coordinates
                float* ptr = (float*)addr;
                
                // Check if this could be a coordinate triple
                // Valid Minecraft coordinates: x,z in ±30000, y in 0-256
                if (ptr[0] > -30000.0f && ptr[0] < 30000.0f &&
                    ptr[1] > -256.0f && ptr[1] < 512.0f &&
                    ptr[2] > -30000.0f && ptr[2] < 30000.0f) {
                    
                    // Look backwards for a string pointer that matches our tile entity types
                    for (int offset = -256; offset <= 0; offset += 8) {
                        uintptr_t* ptr_addr = (uintptr_t*)(addr + offset);
                        uintptr_t potential_string = *ptr_addr;
                        
                        // Validate pointer is in reasonable range
                        if (potential_string < range.first || potential_string > range.second + 1000000) {
                            continue;
                        }
                        
                        try {
                            const char* str = (const char*)potential_string;
                            
                            // Check if this string matches our known tile entity types
                            for (int i = 0; tile_entity_strings[i]; i++) {
                                if (strstr(str, tile_entity_strings[i])) {
                                    // Found a match!
                                    StorageBlock block;
                                    block.x = ptr[0];
                                    block.y = ptr[1];
                                    block.z = ptr[2];
                                    block.type = get_type_name(str);
                                    
                                    // Avoid duplicates (check if similar block already exists)
                                    bool is_duplicate = false;
                                    for (auto& existing : detected_storage_blocks) {
                                        float dx = existing.x - block.x;
                                        float dy = existing.y - block.y;
                                        float dz = existing.z - block.z;
                                        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                                        if (dist < 0.5f) {
                                            is_duplicate = true;
                                            break;
                                        }
                                    }
                                    
                                    if (!is_duplicate) {
                                        detected_storage_blocks.push_back(block);
                                    }
                                    
                                    goto next_check;
                                }
                            }
                        } catch (...) {
                            continue;
                        }
                    }
                }
                
                next_check:
                if (detected_storage_blocks.size() > 100) {
                    // Limit to 100 blocks to avoid performance issues
                    goto done_scanning;
                }
                
            } catch (...) {
                continue;
            }
        }
    }
    
    done_scanning:
    scan_count++;
}

void draw_box_3d(float x, float y, float z, float size, float r, float g, float b, float thickness) {
    float vertices[8][3] = {
        {x - size, y - size, z - size},
        {x + size, y - size, z - size},
        {x + size, y + size, z - size},
        {x - size, y + size, z - size},
        {x - size, y - size, z + size},
        {x + size, y - size, z + size},
        {x + size, y + size, z + size},
        {x - size, y + size, z + size},
    };
    
    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    
    glLineWidth(thickness);
    glColor4f(r, g, b, 0.8f);
    
    glBegin(GL_LINES);
    for (int i = 0; i < 12; i++) {
        float* v1 = vertices[edges[i][0]];
        float* v2 = vertices[edges[i][1]];
        glVertex3f(v1[0], v1[1], v1[2]);
        glVertex3f(v2[0], v2[1], v2[2]);
    }
    glEnd();
    
    glLineWidth(1.0f);
}

void kairo_init() {
    if (kairo_initialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.08f, 0.95f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.15f, 0.4f, 0.7f, 0.9f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.2f, 0.5f, 0.8f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.3f, 0.6f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.4f, 0.7f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.6f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.1f, 0.8f);
    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(6, 6);

    ImGui_ImplOpenGL3_Init("#version 100");
    kairo_initialized = true;

    std::thread* input_thread = new std::thread(input_thread_func);
    input_thread->detach();
}

void render_module_item(int idx) {
    Module& mod = modules[idx];
    ImGui::PushID(idx);
    
    float avail_width = ImGui::GetContentRegionAvail().x;
    ImVec2 button_size(avail_width, 30);
    
    std::string label = std::string(*mod.enabled ? "[ON]  " : "[OFF] ") + mod.name;
    
    if (ImGui::Button(label.c_str(), button_size)) {
        *mod.enabled = !(*mod.enabled);
    }
    
    if (ImGui::IsItemClicked(1)) {
        show_settings[idx] = !show_settings[idx];
    }
    
    if (show_settings[idx] && mod.has_settings) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::SetNextWindowSize(ImVec2(220, 140));
        ImGui::Begin(("##settings" + mod.name).c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        
        if (idx == 0) {
            ImGui::Text("Outline Color");
            ImGui::ColorEdit3("##color", esp_outline_color);
            ImGui::SliderFloat("Thickness", &esp_outline_thickness, 0.5f, 5.0f);
        }
        
        ImGui::End();
    }
    
    ImGui::PopID();
}

void kairo_gui() {
    if (!kairo_visible) return;

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Kairo", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::Text("KAIRO");
        ImGui::Spacing();
        
        static char search_buf[128] = "";
        ImGui::InputTextWithHint("##search", "Search modules...", search_buf, IM_ARRAYSIZE(search_buf));
        ImGui::Separator();
        
        ImGui::Text("Modules:");
        ImGui::Spacing();
        
        for (int i = 0; i < modules.size(); i++) {
            render_module_item(i);
            ImGui::Spacing();
        }
        
        ImGui::Separator();
        
        if (storage_esp_enabled) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Storage blocks: %lu", detected_storage_blocks.size());
            ImGui::TextDisabled("Scans: %d", (int)scan_count);
            
            if (ImGui::BeginChild("BlockList", ImVec2(0, 150), true)) {
                for (auto& block : detected_storage_blocks) {
                    ImGui::TextWrapped("%s @ (%.0f, %.0f, %.0f)", 
                        block.type.c_str(), block.x, block.y, block.z);
                }
                ImGui::EndChild();
            }
        }
        
        ImGui::Separator();
        ImGui::TextDisabled("LClick: Toggle | RClick: Settings");
        ImGui::TextDisabled("Press O to hide");
    }
    ImGui::End();
}

void render_esp_boxes() {
    if (!storage_esp_enabled || detected_storage_blocks.empty()) return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    
    for (auto& block : detected_storage_blocks) {
        draw_box_3d(block.x, block.y, block.z, 0.5f, 
                   esp_outline_color[0], esp_outline_color[1], esp_outline_color[2],
                   esp_outline_thickness);
    }
    
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    if (!kairo_initialized) kairo_init();
    
    if (o_key_pressed && !o_key_was_pressed) {
        kairo_visible = !kairo_visible;
        o_key_was_pressed = true;
    } else if (!o_key_pressed) {
        o_key_was_pressed = false;
    }
    
    // Scan every other frame to avoid performance impact
    static int frame = 0;
    if (++frame % 2 == 0) {
        scan_tile_entities();
    }
    render_esp_boxes();
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
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
