#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace kairo {

struct Module {
    std::string name;
    bool enabled = false;
};

struct Category {
    std::string name;
    std::vector<Module> modules;
};

// Shared, thread-safe toggle: the input-polling thread flips this,
// the render thread (inside the eglSwapBuffers hook) reads it.
extern std::atomic<bool> g_visible;

class KairoGui {
public:
    KairoGui();

    // Call once per frame from the render hook, after ImGui::NewFrame().
    void Render();

private:
    void DrawCategoryColumn(const Category& cat, float columnWidth);

    char searchBuffer_[128] = {0};
    std::vector<Category> categories_;
};

} // namespace kairo
