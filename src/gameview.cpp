#include "gameview.hpp"

#include <fmt/format.h>

#include "coverplaceholder.hpp"
#include "dialog.hpp"
#include "file.hpp"
#include "imgui.hpp"
#include "pkgi.hpp"
extern "C"
{
#include "style.h"
}

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
// vita2d_texture_get_width / _height are declared in vita2d.h
#else
#include <SDL2/SDL.h>
// On Linux/simulator, vita2d_texture is opaque (reinterpreted as SDL_Texture).
// Provide thin wrappers so the rest of gameview.cpp compiles without ifdefs.
static inline float vita2d_texture_get_width(vita2d_texture* t)
{
    int w = 0;
    if (t) SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, &w, nullptr);
    return static_cast<float>(w);
}
static inline float vita2d_texture_get_height(vita2d_texture* t)
{
    int h = 0;
    if (t) SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, nullptr, &h);
    return static_cast<float>(h);
}
#endif

namespace
{
constexpr unsigned GameViewWidth  = VITA_WIDTH  * 0.95;
constexpr unsigned GameViewHeight = VITA_HEIGHT * 0.82;

// Thumbnail panel size presets indexed by config.thumbnail_size
// 0=off, 1=small, 2=medium, 3=large
struct ThumbSize { float w, h; };
constexpr ThumbSize kThumbSizes[] = {
    {  0.f,   0.f}, // 0 off
    {203.f, 203.f}, // 1 small   (square, 90% of previous width)
    {284.f, 284.f}, // 2 medium
    {365.f, 365.f}, // 3 large
};
constexpr int kThumbSizeCount = 4;

const char* presence_label(DbPresence presence)
{
    switch (presence)
    {
    case PresenceUnknown:
        return "Unknown";
    case PresenceIncomplete:
        return "Incomplete";
    case PresenceInstalling:
        return "Installing";
    case PresenceInstalled:
        return "Installed";
    case PresenceMissing:
        return "Missing";
    case PresenceGamePresent:
        return "Base game present";
    }
    return "Unknown";
}

std::string friendly_size(int64_t size)
{
    if (size <= 0)
        return "unknown";
    if (size < 1000LL)
        return fmt::format("{} B", size);
    if (size < 1000LL * 1000)
        return fmt::format("{:.1f} kB", static_cast<double>(size) / 1000.0);
    if (size < 1000LL * 1000 * 1000)
        return fmt::format("{:.1f} MB", static_cast<double>(size) / 1000.0 / 1000.0);
    return fmt::format("{:.2f} GB", static_cast<double>(size) / 1000.0 / 1000.0 / 1000.0);
}

void draw_centered_status_text(
        ImDrawList* dl,
        ImVec2 panel_min,
        float panel_w,
        float panel_h,
        const char* line1,
        const char* line2,
        ImU32 color)
{
    ImVec2 s1 = ImGui::CalcTextSize(line1);
    ImVec2 s2 = line2 ? ImGui::CalcTextSize(line2) : ImVec2(0.f, 0.f);
    const float gap = line2 ? 2.f : 0.f;
    const float total_h = s1.y + (line2 ? gap + s2.y : 0.f);

    dl->AddText(
            ImVec2(panel_min.x + (panel_w - s1.x) * 0.5f,
                   panel_min.y + (panel_h - total_h) * 0.5f),
            color,
            line1);

    if (line2)
    {
        dl->AddText(
                ImVec2(panel_min.x + (panel_w - s2.x) * 0.5f,
                       panel_min.y + (panel_h - total_h) * 0.5f + s1.y + gap),
                color,
                line2);
    }
}
}

GameView::GameView(
        Mode mode,
        const Config* config,
        Downloader* downloader,
        DbItem* item,
        std::optional<CompPackDatabase::Item> base_comppack,
        std::optional<CompPackDatabase::Item> patch_comppack)
    : _mode(mode)
    , _config(config)
    , _downloader(downloader)
    , _item(item)
    , _base_comppack(base_comppack)
    , _patch_comppack(patch_comppack)
    , _image_fetcher(config, item, mode)
{
    if (is_vita_mode())
    {
        _patch_info_fetcher = std::make_unique<PatchInfoFetcher>(item->titleid);
    }

    refresh();
}

void GameView::render()
{
    ImGui::SetNextWindowPos(
            ImVec2((VITA_WIDTH - GameViewWidth) / 2,
                   (VITA_HEIGHT - GameViewHeight) / 2));
    ImGui::SetNextWindowSize(ImVec2(GameViewWidth, GameViewHeight), 0);

    ImGui::Begin(
            fmt::format("{} ({})###gameview", _item->name, _item->titleid)
                    .c_str(),
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings);

    // ── Layout constants ─────────────────────────────────────────────────────
    const int tsz = std::max(0, std::min(
            _config->thumbnail_size, kThumbSizeCount - 1));
    const float cover_w  = kThumbSizes[tsz].w;
    const float cover_h  = kThumbSizes[tsz].h;
    const bool  two_col  = (cover_w > 0.f);

    // Reserve one line at the bottom for hint text
    const float hint_h  = ImGui::GetFrameHeightWithSpacing();
    const float avail_h  = ImGui::GetContentRegionAvail().y - hint_h;
    const float col_gap  = ImGui::GetStyle().ItemSpacing.x;
    const float left_w   = two_col ? (cover_w + 4.f) : 0.f;
    const float right_w  = ImGui::GetContentRegionAvail().x
                           - (two_col ? left_w + col_gap : 0.f);

    // ── LEFT COLUMN: cover (only when two_col) ────────────────────────────────
    // Always non-interactive (static image) — never receives nav focus.
    if (two_col)
    {
        ImGui::BeginChild(
                "##lc",
                ImVec2(left_w, avail_h),
                false,
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);

        auto* thumb_tex     = _image_fetcher.get_texture();
        const auto img_stat = _image_fetcher.get_status();
        ImDrawList* ldl     = ImGui::GetWindowDrawList();

        if (thumb_tex)
        {
            float tw = static_cast<float>(vita2d_texture_get_width(thumb_tex));
            float th = static_cast<float>(vita2d_texture_get_height(thumb_tex));
            if (tw > cover_w) { th = th * cover_w / tw; tw = cover_w; }
            if (th > cover_h) { tw = tw * cover_h / th; th = cover_h; }
            const float ox = (cover_w - tw) * 0.5f;
            if (ox > 0.f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
            ImGui::Image(reinterpret_cast<ImTextureID>(thumb_tex),
                         ImVec2(tw, th));
        }
        else
        {
            const bool is_loading =
                    img_stat == ImageFetcher::Status::Downloading;
            vita2d_texture* placeholder =
                    pkgi_get_cover_placeholder(is_loading);

            ImVec2 pm = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cover_w, cover_h));

            if (placeholder)
            {
                float tw =
                        static_cast<float>(vita2d_texture_get_width(placeholder));
                float th =
                        static_cast<float>(vita2d_texture_get_height(placeholder));
                if (tw > cover_w) { th = th * cover_w / tw; tw = cover_w; }
                if (th > cover_h) { tw = tw * cover_h / th; th = cover_h; }
                const float ox = pm.x + (cover_w - tw) * 0.5f;
                const float oy = pm.y + (cover_h - th) * 0.5f;
                ldl->AddImage(
                        reinterpret_cast<ImTextureID>(placeholder),
                        ImVec2(ox, oy),
                        ImVec2(ox + tw, oy + th));
            }
            else
            {
                ldl->AddRectFilled(
                        pm,
                        {pm.x + cover_w, pm.y + cover_h},
                        IM_COL32(18, 22, 40, 220),
                        4.f);
                ldl->AddRect(
                        pm,
                        {pm.x + cover_w, pm.y + cover_h},
                        IM_COL32(70, 80, 110, 255),
                        4.f);
                const char* l1 = is_loading ? "Downloading" : "No image";
                const char* l2 = is_loading ? "cover..." : nullptr;
                draw_centered_status_text(
                        ldl, pm, cover_w, cover_h, l1, l2,
                        IM_COL32(160, 170, 200, 200));
            }
        }

        ImGui::EndChild(); // ##lc
        ImGui::SameLine(0, col_gap);
    }

    // ── RIGHT COLUMN (or full-width single column) ─────────────────────────
    // Always interactive — this is the only panel with anything to focus,
    // so it grabs nav focus on the first render and just keeps it.
    if (_request_focus)
    {
        ImGui::SetNextWindowFocus();
        _request_focus = false;
    }

    ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding, ImVec2(4.f, 2.f));
    ImGui::BeginChild(
            "##rc",
            ImVec2(right_w, avail_h),
            false,
            ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    ImGui::PushTextWrapPos(0.f); // wrap at right edge of this child

    if (is_vita_mode())
    {
        // ── Metadata rows ────────────────────────────────────────────────────
        // Helper: label in dim text, value at a fixed x offset.
        const float label_x = 190.f;
        auto row = [&](const char* label,
                       const char* value,
                       ImVec4 col = ImVec4(-1.f, -1.f, -1.f, -1.f))
        {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(label_x);
            if (col.x >= 0.f)
                ImGui::TextColored(col, "%s", value);
            else
                ImGui::Text("%s", value);
        };

        const auto sys_ver = pkgi_get_system_version();
        const auto min_ver = get_min_system_version();
        const bool fw_ok   = !min_ver.empty() && sys_ver >= min_ver;

        // Single combined firmware line: "Required: X.XX (current: Y.YY)"
        {
            const std::string req_str =
                    min_ver.empty() ? "unknown" : min_ver;
            const std::string fw_line =
                    fmt::format("{} (current: {})", req_str, sys_ver);
            row("Required firmware:",
                fw_line.c_str(),
                fw_ok ? ImVec4(0.3f, 1.f, 0.5f, 1.f)
                      : ImVec4(1.f, 0.35f, 0.35f, 1.f));
        }

        const bool installed = !_game_version.empty();

        // Installed version + base compat pack on one line
        {
            ImGui::TextDisabled("Installed version:");
            ImGui::SameLine(label_x);
            if (installed)
                ImGui::TextColored(
                        ImVec4(0.3f, 1.f, 0.5f, 1.f),
                        "%s",
                        _game_version.c_str());
            else
                ImGui::TextColored(
                        ImVec4(1.f, 0.88f, 0.25f, 1.f), "not installed");

            // Base compat pack status on the same line if there is info
            if (_comppack_versions.present ||
                !_comppack_versions.base.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  Base cp:");
                ImGui::SameLine();
                if (_comppack_versions.base.empty())
                    ImGui::TextColored(
                            ImVec4(1.f, 0.88f, 0.25f, 1.f), "no");
                else
                    ImGui::TextColored(
                            ImVec4(0.3f, 1.f, 0.5f, 1.f), "yes");
            }
        }

        if (_comppack_versions.present &&
            _comppack_versions.base.empty() &&
            _comppack_versions.patch.empty())
        {
            ImGui::TextColored(
                    ImVec4(1.f, 0.9f, 0.2f, 1.f),
                    "Compat pack: installed (unknown version)");
        }
        else if (!_comppack_versions.patch.empty())
        {
            row("Patch compat pack:", _comppack_versions.patch.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Diagnostic ───────────────────────────────────────────────────────
        printDiagnostic();
        ImGui::Spacing();

        // ── Action buttons ───────────────────────────────────────────────────
        if (_patch_info_fetcher &&
            _patch_info_fetcher->get_status() ==
                    PatchInfoFetcher::Status::Found)
        {
            if (ImGui::Button("Install game and patch###installgame"))
                start_download_package();
        }
        else
        {
            if (ImGui::Button("Install game###installgame"))
                start_download_package();
        }
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemFocused())
            ImGui::SetScrollY(0.0f);

        if (_base_comppack)
        {
            ImGui::SameLine();
            if (!_downloader->is_in_queue(CompPackBase, _item->titleid))
            {
                if (ImGui::Button(
                            "Install base compat pack###installbasecomppack"))
                    start_download_comppack(false);
            }
            else
            {
                if (ImGui::Button(
                            "Cancel base compat pack###installbasecomppack"))
                    cancel_download_comppacks(false);
            }
        }
        if (_patch_comppack)
        {
            ImGui::SameLine();
            if (!_downloader->is_in_queue(CompPackPatch, _item->titleid))
            {
                if (ImGui::Button(fmt::format(
                                          "Install patch compat {}###installpatchcommppack",
                                          _patch_comppack->app_version)
                                          .c_str()))
                    start_download_comppack(true);
            }
            else
            {
                if (ImGui::Button(
                            "Cancel patch compat###installpatchcommppack"))
                    cancel_download_comppacks(true);
            }
        }
    }
    else
    {
        // ── PSP / non-vita mode ──────────────────────────────────────────────
        ImGui::Text(fmt::format(
                            "Content ID: {}",
                            _item->content.empty() ? "unknown"
                                                   : _item->content)
                            .c_str());
        ImGui::Text(fmt::format("Package size: {}", friendly_size(_item->size))
                            .c_str());
        ImGui::Text(fmt::format(
                            "Last update: {}",
                            _item->date.empty() ? "unknown" : _item->date)
                            .c_str());
        ImGui::Spacing();

        ImGui::Text("Diagnostic:");
        ImGui::Text(fmt::format("- Status: {}",
                                presence_label(_item->presence))
                            .c_str());
        ImGui::Text(fmt::format(
                            "- NoPspEmuDrm kernel plugin: {}",
                            _nopspemudrm_present ? "present" : "not detected")
                            .c_str());
        ImGui::Text("- Install as ISO: available");
        if (_nopspemudrm_present)
            ImGui::Text("- LiveArea PBP queue: available");
        else
            ImGui::Text("- LiveArea PBP queue: unavailable without plugin");
        ImGui::Spacing();

        if (ImGui::Button("Install as ISO###installpspiso"))
            start_download_package(PspInstallMode::Iso);
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemFocused())
            ImGui::SetScrollY(0.0f);

        if (_nopspemudrm_present)
        {
            if (ImGui::Button("Queue as PBP in LiveArea###installpsppbp"))
                start_download_package(PspInstallMode::LiveAreaPbp);
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndChild(); // ##rc

    // ── Hint bar ─────────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::TextDisabled("  [O] Close");

    ImGui::End();
}

bool GameView::handle_cancel()
{
    // No nested focus levels anymore — Circle always closes the view.
    return false;
}

static const auto Red = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
static const auto Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
static const auto Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

void GameView::printDiagnostic()
{
    bool ok = true;
    auto const printError = [&](auto const& str)
    {
        ok = false;
        ImGui::TextColored(Red, str);
    };

    auto const systemVersion = pkgi_get_system_version();
    auto const minSystemVersion = get_min_system_version();

    ImGui::Text("Diagnostic:");

    if (systemVersion < minSystemVersion)
    {
        if (!_comppack_versions.present)
        {
            if (_refood_present)
                ImGui::Text("- This game will work thanks to reF00D");
            else if (_0syscall6_present)
                ImGui::Text("- This game will work thanks to 0syscall6");
            else
                printError(
                        "- Your firmware is too old to play this game, you "
                        "must install reF00D or 0syscall6");
        }
    }
    else
    {
        ImGui::Text("- Your firmware is recent enough");
    }

    if (_comppack_versions.present && _comppack_versions.base.empty() &&
        _comppack_versions.patch.empty())
    {
        ImGui::TextColored(
                Yellow,
                "- A compatibility pack is installed but not by PKGj, please "
                "make sure it matches the installed version or reinstall it "
                "with PKGj");
        ok = false;
    }

    if (_comppack_versions.base.empty() && !_comppack_versions.patch.empty())
        printError(
                "- You have installed an update compatibility pack without "
                "installing the base pack, install the base pack first and "
                "reinstall the update compatibility pack.");

    std::string comppack_version;
    if (!_comppack_versions.patch.empty())
        comppack_version = _comppack_versions.patch;
    else if (!_comppack_versions.base.empty())
        comppack_version = _comppack_versions.base;

    if (_item->presence == PresenceInstalled && !comppack_version.empty() &&
        comppack_version < _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. If you have updated the game, also "
                "install the update compatibility pack.");

    if (_item->presence == PresenceInstalled &&
        comppack_version > _game_version)
        printError(
                "- The version of the game does not match the installed "
                "compatibility pack. Downgrade to the base compatibility "
                "pack or update the game through the Live Area.");

    if (_item->presence != PresenceInstalled)
    {
        ImGui::Text("- Game not installed");
        ok = false;
    }

    (void)ok; // "All green" omitted — installed state is shown in metadata above
}

std::string GameView::get_min_system_version()
{
    if (!_patch_info_fetcher)
        return _item->fw_version;

    auto const patchInfo = _patch_info_fetcher->get_patch_info();
    if (patchInfo)
        return patchInfo->fw_version;
    else
        return _item->fw_version;
}

bool GameView::is_vita_mode() const
{
    return _mode == ModeGames;
}

void GameView::refresh()
{
    LOGF("Refreshing game view");
    if (is_vita_mode())
    {
        _refood_present = pkgi_is_module_present("ref00d");
        _0syscall6_present = pkgi_is_module_present("0syscall6");
        _game_version = pkgi_get_game_version(_item->titleid);
        _comppack_versions = pkgi_get_comppack_versions(_item->titleid);
    }
    else
    {
        _refood_present = false;
        _0syscall6_present = false;
        _nopspemudrm_present = pkgi_is_module_present("NoPspEmuDrm_kern");
        _game_version.clear();
        _comppack_versions = {};
    }
}


void GameView::do_download(PspInstallMode psp_install_mode) {
    pkgi_start_download(*_downloader, *_item, psp_install_mode);
    _item->presence = PresenceUnknown;
}

void GameView::start_download_package(PspInstallMode psp_install_mode)
{
    if (_item->presence == PresenceInstalled)
    {
        LOGF("[{}] {} - already installed", _item->titleid, _item->name);
        pkgi_dialog_question(
        fmt::format(
                "{} is already installed."
                "Would you like to redownload it?",
                _item->name)
                .c_str(),
        {{"Redownload.", [this, psp_install_mode] { this->do_download(psp_install_mode); }},
         {"Dont Redownload.", [] {} }});
        return;
    }
    this->do_download(psp_install_mode);
}

void GameView::cancel_download_package()
{
    _downloader->remove_from_queue(Game, _item->content);
    _item->presence = PresenceUnknown;
}

void GameView::start_download_comppack(bool patch)
{
    const auto& entry = patch ? _patch_comppack : _base_comppack;

    _downloader->add(DownloadItem{
            patch ? CompPackPatch : CompPackBase,
            _item->name,
            _item->titleid,
            _config->comppack_url + entry->path,
            std::vector<uint8_t>{},
            std::vector<uint8_t>{},
            false,
            "ux0:",
            entry->app_version});
}

void GameView::cancel_download_comppacks(bool patch)
{
    _downloader->remove_from_queue(
            patch ? CompPackPatch : CompPackBase, _item->titleid);
}
