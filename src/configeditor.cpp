#include "configeditor.hpp"

#include <cstring>

#include <fmt/format.h>

#include "file.hpp"
#include "imgui.hpp"
#include "pkgi.hpp"
extern "C"
{
#include "style.h"
}

namespace
{
constexpr float EditorW = VITA_WIDTH  * 0.8f;
constexpr float EditorH = VITA_HEIGHT * 0.8f;
}

// ── Construction / destruction ───────────────────────────────────────────────

ConfigEditor::ConfigEditor(Config& config)
    : _config(config)
{
    _path = fmt::format("{}/config.txt", pkgi_get_config_folder());
    load();
}

void ConfigEditor::load()
{
    _lines.clear();
    if (!pkgi_file_exists(_path))
        return;

    auto data = pkgi_load(_path);

    // Split raw bytes into lines; strip Windows-style \r
    std::string cur;
    cur.reserve(128);
    for (uint8_t ch : data)
    {
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            _lines.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += static_cast<char>(ch);
        }
    }
    // Last line may have no trailing newline
    if (!cur.empty())
        _lines.push_back(cur);
}

void ConfigEditor::save()
{
    std::string out;
    out.reserve(_lines.size() * 64);
    for (const auto& l : _lines)
    {
        out += l;
        out += '\n';
    }
    pkgi_save(_path, out.data(), static_cast<uint32_t>(out.size()));
}

void ConfigEditor::save_and_close()
{
    save();
    _saved  = true;
    _closed = true;
}

void ConfigEditor::close()
{
    _closed = true;
}

// ── Rendering ────────────────────────────────────────────────────────────────

void ConfigEditor::render()
{
    // ── IME polling: must happen every frame ─────────────────────────────────
    if (_ime_active && pkgi_dialog_input_update())
    {
        pkgi_dialog_input_get_text(_ime_buf, sizeof(_ime_buf));
        if (_selected >= 0 && _selected < static_cast<int>(_lines.size()))
            _lines[_selected] = _ime_buf;
        _ime_active = false;
    }

    // ── Window ───────────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(
            ImVec2((VITA_WIDTH  - EditorW) / 2.f,
                   (VITA_HEIGHT - EditorH) / 2.f));
    ImGui::SetNextWindowSize(ImVec2(EditorW, EditorH), 0);

    ImGui::Begin(
            "Config Editor — config.txt###cfgedit",
            nullptr,
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove   |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // ── Footer height reservation ─────────────────────────────────────────────
    const float footer_h =
            ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    // ── Scrollable row list ───────────────────────────────────────────────────
    ImGui::BeginChild(
            "##rows",
            ImVec2(0.f, -footer_h),
            false,
            ImGuiWindowFlags_NavFlattened);

    if (_lines.empty())
    {
        ImGui::TextDisabled("(file is empty or not found)");
    }
    else
    {
        for (int i = 0; i < static_cast<int>(_lines.size()); ++i)
        {
            const bool sel = (i == _selected);

            // PushID makes each row unique even if two lines share identical text
            ImGui::PushID(i);

            const char* display =
                    _lines[i].empty() ? " " : _lines[i].c_str();

            const bool activated = ImGui::Selectable(
                    display,
                    sel,
                    ImGuiSelectableFlags_AllowDoubleClick);

            if (sel)
                ImGui::SetItemDefaultFocus();

            // D-pad navigation: track which row ImGui's nav cursor is on
            if (ImGui::IsItemFocused())
                _selected = i;

            // X / OK press on the focused row opens IME
            if (activated && !_ime_active)
            {
                _selected = i;
                std::strncpy(
                        _ime_buf, _lines[i].c_str(), sizeof(_ime_buf) - 1);
                _ime_buf[sizeof(_ime_buf) - 1] = '\0';
                pkgi_dialog_input_text("Edit line", _ime_buf);
                _ime_active = true;
            }

            // Auto-scroll so the selected row stays visible
            if (sel && ImGui::IsItemVisible() == false)
                ImGui::SetScrollHereY(0.5f);

            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    // ── Footer ────────────────────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled(
            "[Cross] Edit line    "
            "[Triangle] Save & close    "
            "[Circle] Discard");

    ImGui::End();
}
