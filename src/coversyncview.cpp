#include "coversyncview.hpp"

#include "imgui.hpp"

#include <fmt/format.h>

extern "C"
{
#include "style.h"
}

namespace
{
constexpr float ViewW = VITA_WIDTH * 0.7f;
}

CoverSyncView::CoverSyncView(const Config* config, std::vector<DbItem*> items)
    : _config(config)
    , _items(std::move(items))
{
}

void CoverSyncView::render(const pkgi_input& input)
{
    // Advance the state machine: at most one ImageFetcher in flight at a
    // time, same discipline the grid view uses — this never competes with
    // (or starves) whatever the grid/gameview is also trying to fetch,
    // since they all share the single global WorkerSlot.
    if (!_current && _index < _items.size())
        _current = std::make_unique<ImageFetcher>(_config, _items[_index]);

    if (_current)
    {
        const auto status = _current->get_status();
        if (status == ImageFetcher::Status::Ready)
        {
            ++_ok;
            _current.reset();
            ++_index;
        }
        else if (status == ImageFetcher::Status::Error)
        {
            ++_failed;
            _current.reset();
            ++_index;
        }
    }

    const bool done = _index >= _items.size();

    if ((input.pressed & pkgi_ok_button()) ||
        (input.pressed & pkgi_cancel_button()))
        _closed = true;

    ImGui::SetNextWindowPos(
            ImVec2(VITA_WIDTH / 2.f, VITA_HEIGHT / 2.f),
            0,
            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ViewW, 0.f), 0);

    ImGui::Begin(
            "Cover sync###coversync",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_AlwaysAutoResize);

    if (done)
    {
        ImGui::Text(
                "Done — %zu downloaded/cached, %zu unavailable.", _ok,
                _failed);
        ImGui::Spacing();
        ImGui::TextDisabled(
                "%s / %s close", pkgi_get_ok_str(), pkgi_get_cancel_str());
    }
    else
    {
        const std::string item_name =
                _index < _items.size() ? _items[_index]->name : "";
        ImGui::Text(
                "Syncing covers %zu/%zu", _index + 1, _items.size());
        ImGui::TextDisabled("%s", item_name.c_str());
        ImGui::Spacing();

        const float frac = _items.empty()
                ? 1.f
                : static_cast<float>(_index) /
                        static_cast<float>(_items.size());
        ImGui::ProgressBar(frac, ImVec2(-1.f, 0.f));
        ImGui::Spacing();
        ImGui::TextDisabled(
                "%s cancel (covers already synced stay cached)",
                pkgi_get_cancel_str());
    }

    ImGui::End();
}
