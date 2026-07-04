#include "kairo_gui.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

namespace kairo {

std::atomic<bool> g_visible{false};

KairoGui::KairoGui() {
    categories_ = {
        { "Player",   {} },
        { "Movement", {} },
        { "Visual",   {} },
        { "World",    {} },
        { "Misc",     {} },
    };
}

static bool MatchesSearch(const std::string& name, const char* query) {
    if (query[0] == '\0') return true;
    std::string lowerName = name;
    std::string lowerQuery = query;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    return lowerName.find(lowerQuery) != std::string::npos;
}

void KairoGui::DrawCategoryColumn(const Category& cat, float columnWidth) {
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
    ImGui::Text("%s", cat.name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (cat.modules.empty()) {
        ImGui::TextDisabled("(no modules yet)");
    } else {
        for (auto& mod : cat.modules) {
            if (!MatchesSearch(mod.name, searchBuffer_)) continue;
            ImGui::Selectable(mod.name.c_str(), mod.enabled);
        }
    }
    ImGui::EndGroup();
}

void KairoGui::Render() {
    if (!g_visible.load()) return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.09f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

    bool open = true;
    if (ImGui::Begin("Kairo", &open, ImGuiWindowFlags_NoCollapse)) {
        float columnWidth = ImGui::GetContentRegionAvail().x / categories_.size();

        float bottomBarHeight = ImGui::GetFrameHeightWithSpacing() + 8.0f;
        ImGui::BeginChild("kairo_columns", ImVec2(0, -bottomBarHeight), false);
        ImGui::Columns((int)categories_.size(), "kairo_cat_columns", false);
        for (size_t i = 0; i < categories_.size(); ++i) {
            ImGui::SetColumnWidth((int)i, columnWidth);
            DrawCategoryColumn(categories_[i], columnWidth);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        ImGui::InputTextWithHint("##kairo_search", "Search modules...",
                                  searchBuffer_, sizeof(searchBuffer_));
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
        ImGui::Text("Kairo");
        ImGui::PopStyleColor();
    }
    ImGui::End();

    if (!open) g_visible.store(false); // clicking the X also hides it

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace kairo
