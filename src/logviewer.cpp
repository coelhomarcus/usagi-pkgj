#include "logviewer.hpp"

#include "imgui.hpp"
#include "logbuffer.hpp"

extern "C"
{
#include "style.h"
}

namespace
{
constexpr float ViewerW = VITA_WIDTH * 0.9f;
constexpr float ViewerH = VITA_HEIGHT * 0.85f;
}

void LogViewer::render(const pkgi_input& input)
{
    const auto lines = pkgi_log_buffer_snapshot();
    const int  n     = static_cast<int>(lines.size());

    // ── Navigation ────────────────────────────────────────────────────────────
    // Use input.active — same field as pkgi_do_main — so repeat rate and
    // initial-press delay are identical to the main game list.
    bool nav_step = false;

    if ((input.active & PKGI_BUTTON_UP) && n > 0)
    {
        _selected = std::max(0, _selected - 1);
        nav_step  = true;
    }
    else if ((input.active & PKGI_BUTTON_DOWN) && n > 0)
    {
        _selected = std::min(n - 1, _selected + 1);
        nav_step  = true;
    }

    // Clamp in case log buffer shrank
    if (n == 0)
        _selected = 0;
    else
        _selected = std::max(0, std::min(_selected, n - 1));

    // Inverted order: index 0 is newest entry (top of viewport).
    // _selected is still in [0, n-1], where 0 means newest.
    
    // ── Window ────────────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(
            ImVec2((VITA_WIDTH  - ViewerW) / 2.f,
                   (VITA_HEIGHT - ViewerH) / 2.f));
    ImGui::SetNextWindowSize(ImVec2(ViewerW, ViewerH), 0);

    ImGui::Begin(
            "Log Viewer###logviewer",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::Text("Lines: %d", n);
    ImGui::Separator();

    const float footer_h =
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    // NoNav: we handle navigation ourselves
    ImGui::BeginChild(
            "##logrows",
            ImVec2(0.f, -footer_h),
            false,
            ImGuiWindowFlags_NoNav);

    if (n == 0)
    {
        ImGui::TextDisabled("(no log messages yet)");
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            ImGui::PushID(i);

            const int        idx      = n - 1 - i; // newest first
            const bool       selected = (i == _selected);
            const LogEntry&  entry    = lines[idx];
            const char*      text     = entry.text.empty() ? " " : entry.text.c_str();

            // Color by level
            ImVec4 col;
            switch (entry.level)
            {
            case LogLevel::Error:
                col = ImVec4(1.00f, 0.35f, 0.35f, 1.f);
                break;
            case LogLevel::Warn:
                col = ImVec4(1.00f, 0.85f, 0.10f, 1.f);
                break;
            default:
                col = ImVec4(1.00f, 1.00f, 1.00f, 1.f);
                break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Selectable(text, selected, ImGuiSelectableFlags_Disabled);
            ImGui::PopStyleColor();

            // Scroll the selected row into view whenever navigation occurred
            if (selected && nav_step)
                ImGui::SetScrollHereY(0.5f);

            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled(
            "[Up/Down] navigate   "
            "(hold 1s = fast scroll)   "
            "[Circle] close");

    ImGui::End();
}