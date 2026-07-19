#include "gridview.hpp"

#include "coverplaceholder.hpp"
#include "gridtexturepool.hpp"
#include "imagefetcher.hpp"
#include "imgui.hpp"
#include "regionflag.hpp"
extern "C"
{
#include "style.h"
}

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
#include <SDL2/SDL.h>
// On Linux/simulator, vita2d_texture is opaque (reinterpreted as
// SDL_Texture). Same shim as gameview.cpp / imagefetcher.cpp use.
static inline float vita2d_texture_get_width(vita2d_texture* t)
{
    int w = 0;
    if (t)
        SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, &w, nullptr);
    return static_cast<float>(w);
}
static inline float vita2d_texture_get_height(vita2d_texture* t)
{
    int h = 0;
    if (t)
        SDL_QueryTexture(reinterpret_cast<SDL_Texture*>(t), nullptr, nullptr, nullptr, &h);
    return static_cast<float>(h);
}
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
// Cell size is tuned for vertical box art (~250x320 PS Vita, 180x320 PSP,
// HexFlow-Covers); draw_scaled() fits any other aspect (e.g. PSX's square
// 256x256) within the same box without distortion.
constexpr int kCols = 4;
constexpr int kRows = 2;
constexpr int kCellsPerPage = kCols * kRows;

constexpr float kMargin = 8.f;
constexpr float kGap = 12.f;

constexpr ImU32 kSelBorderCol   = IM_COL32(90, 160, 255, 220);
constexpr ImU32 kCellBgCol      = IM_COL32(18, 22, 40, 220);
constexpr ImU32 kCellBorderCol  = IM_COL32(70, 80, 110, 255);
constexpr ImU32 kTitleCol       = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kStatusCol      = IM_COL32(160, 170, 200, 200);
constexpr ImU32 kInstalledCol   = IM_COL32(50, 50, 255, 255);

// Cap on how many covers are decoded + blitted into the texture pool in a
// single frame. Reset to 0 at the top of every pkgi_do_main_grid() draw.
// Fast scrolling makes a whole page (up to kCellsPerPage) of covers become
// visible at once; decoding and blitting all of them in one frame is real
// CPU work (PNG/JPEG decode, then a pixel copy) that would show up as a
// frame hitch. Spreading it over a few frames keeps each frame's cost
// bounded; cells whose cover isn't blitted yet just show the placeholder
// for an extra frame or two. (This budget is unrelated to GPU texture
// lifetime safety — see GridTexturePool for that; it exists purely to keep
// per-frame decode/blit cost smooth.)
int g_tex_uploads_this_frame = 0;
constexpr int kMaxTexUploadsPerFrame = 2;

// ── Per-cell cover download/decode coordination ─────────────────────────────
// Keyed by titleid (never by DbItem*: TitleDatabase::reload() invalidates
// every DbItem pointer, but titleids of surviving items stay valid).
//
// Owns each visible cell's ImageFetcher — the download/decode-in-progress
// state — for as long as it's on screen. This is NOT where the displayed
// texture lives (see GridTexturePool for that); an ImageFetcher instance
// here never holds a lasting vita2d_texture, so destroying one when its
// title scrolls off-screen is always safe, immediately, no cooldown needed
// (unlike the grid's very first texture-ownership design, which crashed
// Vita3K this same way — see GridTexturePool's comment for the full story).
class GridImageCache
{
    using CacheMap = std::unordered_map<std::string, std::unique_ptr<ImageFetcher>>;

public:
    void sync(const std::vector<DbItem*>& wanted, const Config& config, Mode mode)
    {
        for (DbItem* item : wanted)
        {
            if (_cache.find(item->titleid) != _cache.end())
                continue;

            _cache.emplace(
                    item->titleid,
                    std::make_unique<ImageFetcher>(&config, item, mode));
        }

        for (auto it = _cache.begin(); it != _cache.end();)
        {
            const bool still_wanted = std::any_of(
                    wanted.begin(),
                    wanted.end(),
                    [&](DbItem* item) { return item->titleid == it->first; });
            if (!still_wanted)
                it = _cache.erase(it);
            else
                ++it;
        }
    }

    ImageFetcher* get(const std::string& titleid)
    {
        auto it = _cache.find(titleid);
        return it == _cache.end() ? nullptr : it->second.get();
    }

    // Drops every cached fetcher (download/decode state) — NOT the texture
    // pool, whose slots stay populated so scrolling back to a title already
    // shown shows its cover instantly with no re-download. Called once when
    // the grid stops being the active renderer, so a screen that's no
    // longer shown doesn't keep its download state around forever (sync()
    // would eventually evict it too, but only on its next call, which may
    // never come once the grid is inactive).
    void clear()
    {
        _cache.clear();
    }

private:
    CacheMap _cache;
};

GridImageCache g_image_cache;
GridTexturePool g_texture_pool;

const char* status_badge(DbPresence presence, ImU32& out_color)
{
    switch (presence)
    {
    case PresenceInstalled:
        out_color = kTitleCol;
        return PKGI_UTF8_INSTALLED;
    case PresenceIncomplete:
        out_color = kTitleCol;
        return PKGI_UTF8_PARTIAL;
    case PresenceInstalling:
        out_color = kTitleCol;
        return PKGI_UTF8_INSTALLING;
    case PresenceGamePresent:
        out_color = kInstalledCol;
        return PKGI_UTF8_INSTALLED;
    default:
        return nullptr;
    }
}

std::string truncate_to_width(const std::string& text, float max_w)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= max_w)
        return text;

    std::string s = text;
    while (!s.empty() &&
           ImGui::CalcTextSize((s + "...").c_str()).x > max_w)
        s.pop_back();
    return s.empty() ? s : s + "...";
}

void draw_scaled(ImDrawList* dl, vita2d_texture* tex, ImVec2 box_min, float box_w, float box_h)
{
    float tw = vita2d_texture_get_width(tex);
    float th = vita2d_texture_get_height(tex);
    if (tw > box_w)
    {
        th = th * box_w / tw;
        tw = box_w;
    }
    if (th > box_h)
    {
        tw = tw * box_h / th;
        th = box_h;
    }
    const float ox = box_min.x + (box_w - tw) * 0.5f;
    const float oy = box_min.y + (box_h - th) * 0.5f;
    dl->AddImage(
            reinterpret_cast<ImTextureID>(tex),
            ImVec2(ox, oy),
            ImVec2(ox + tw, oy + th));
}

// Same box-fit as draw_scaled(), but for a GridTexturePool entry: the
// texture is a fixed-size pool slot, so the fit has to be computed from the
// cover's actual content size within it (content_w/content_h), not the
// slot's own full width/height — and only the matching UV sub-rect (the
// content region, excluding letterbox padding) is drawn.
void draw_scaled_uv(
        ImDrawList* dl,
        vita2d_texture* tex,
        ImVec2 box_min,
        float box_w,
        float box_h,
        float content_w,
        float content_h,
        ImVec2 uv_min,
        ImVec2 uv_max)
{
    float tw = content_w;
    float th = content_h;
    if (tw > box_w)
    {
        th = th * box_w / tw;
        tw = box_w;
    }
    if (th > box_h)
    {
        tw = tw * box_h / th;
        th = box_h;
    }
    const float ox = box_min.x + (box_w - tw) * 0.5f;
    const float oy = box_min.y + (box_h - th) * 0.5f;
    dl->AddImage(
            reinterpret_cast<ImTextureID>(tex),
            ImVec2(ox, oy),
            ImVec2(ox + tw, oy + th),
            uv_min,
            uv_max);
}

void draw_cell(
        ImDrawList* dl,
        DbItem* item,
        ImVec2 cell_min,
        float cell_w,
        float cell_h,
        bool selected,
        Mode mode)
{
    const float title_h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 cov_min = cell_min;
    const ImVec2 cov_max(cell_min.x + cell_w, cell_min.y + cell_h - title_h);
    const float box_w = cov_max.x - cov_min.x;
    const float box_h = cov_max.y - cov_min.y;

    ImageFetcher* fetcher = g_image_cache.get(item->titleid);

    // 1) Already in the texture pool (from an earlier visit, possibly under
    //    a since-destroyed ImageFetcher — the pool outlives those) — just
    //    draw it, no decode needed.
    bool drawn = false;
    if (auto found = g_texture_pool.find(item->titleid))
    {
        draw_scaled_uv(
                dl, found->tex, cov_min, box_w, box_h,
                found->content_w, found->content_h,
                found->uv_min, found->uv_max);
        drawn = true;
    }
    // 2) Not in the pool yet: if the download has finished and we're still
    //    under this frame's decode/blit budget, decode it now and store it.
    else if (fetcher && g_tex_uploads_this_frame < kMaxTexUploadsPerFrame)
    {
        if (auto cover = fetcher->take_decoded_cover())
        {
            const GridTexturePool::Entry entry =
                    g_texture_pool.store(item->titleid, *cover);
            if (entry.tex)
            {
                draw_scaled_uv(
                        dl, entry.tex, cov_min, box_w, box_h,
                        entry.content_w, entry.content_h,
                        entry.uv_min, entry.uv_max);
                drawn = true;
            }
            ++g_tex_uploads_this_frame;
        }
    }

    if (!drawn)
    {
        const auto status = fetcher ? fetcher->get_status()
                                     : ImageFetcher::Status::Pending;
        const bool is_loading =
                status == ImageFetcher::Status::Downloading ||
                status == ImageFetcher::Status::Pending;

        vita2d_texture* placeholder = pkgi_get_cover_placeholder(is_loading, mode);
        if (placeholder)
        {
            draw_scaled(dl, placeholder, cov_min, box_w, box_h);
        }
        else
        {
            dl->AddRectFilled(cov_min, cov_max, kCellBgCol, 4.f);
            dl->AddRect(cov_min, cov_max, kCellBorderCol, 4.f);

            const char* label = is_loading ? "..." : "No image";
            const ImVec2 sz    = ImGui::CalcTextSize(label);
            dl->AddText(
                    ImVec2(cov_min.x + (box_w - sz.x) * 0.5f,
                           cov_min.y + (box_h - sz.y) * 0.5f),
                    kStatusCol,
                    label);
        }
    }

    ImU32 badge_col = kTitleCol;
    if (const char* badge = status_badge(item->presence, badge_col))
    {
        const ImVec2 bsz = ImGui::CalcTextSize(badge);
        dl->AddText(
                ImVec2(cov_max.x - bsz.x - 6.f, cov_min.y + 4.f),
                badge_col,
                badge);
    }

    if (vita2d_texture* flag_tex =
                pkgi_get_region_flag(pkgi_get_region(item->titleid)))
    {
        const float flag_h = ImGui::GetTextLineHeight() * 0.8f;
        const float flag_w = flag_h * 1.5f; // matches the 3:2 flag asset canvas
        const ImVec2 fmin(cov_min.x + 6.f, cov_min.y + 4.f);
        const ImVec2 fmax(fmin.x + flag_w, fmin.y + flag_h);
        dl->AddRectFilled(fmin, fmax, kCellBgCol, 2.f);
        dl->AddImage(reinterpret_cast<ImTextureID>(flag_tex), fmin, fmax);
    }

    const std::string title = truncate_to_width(item->name, cell_w - 4.f);
    const ImVec2 tsz         = ImGui::CalcTextSize(title.c_str());
    dl->AddText(
            ImVec2(cell_min.x + (cell_w - tsz.x) * 0.5f, cov_max.y + 2.f),
            kTitleCol,
            title.c_str());

    if (selected)
        dl->AddRect(
                cell_min,
                ImVec2(cell_min.x + cell_w, cell_min.y + cell_h),
                kSelBorderCol,
                4.f,
                0,
                3.f);
}
} // namespace

GridResult pkgi_do_main_grid(
        TitleDatabase& db,
        const Config& config,
        pkgi_input* input,
        uint32_t& first_item,
        uint32_t& selected_item,
        int font_height,
        int avail_height,
        Mode mode)
{
    GridResult result;

    const uint32_t db_count = db.count();

    if (db_count == 0)
    {
        first_item = selected_item = 0;
    }
    else
    {
        if (selected_item >= db_count)
            selected_item = db_count - 1;
        // Row-align first_item: it may be arbitrary right after switching
        // from list view, where "first item on screen" isn't row-aligned.
        first_item -= first_item % kCols;
    }

    // Keeps first_item aligned to a row boundary and scrolled so that
    // selected_item stays inside the visible kCols x kRows window.
    auto reclamp_window = [&]()
    {
        const uint32_t sel_row   = selected_item / kCols;
        const uint32_t first_row = first_item / kCols;
        if (sel_row < first_row)
            first_item = sel_row * kCols;
        else if (sel_row >= first_row + kRows)
            first_item = (sel_row - kRows + 1) * kCols;
    };

    if (db_count != 0)
        reclamp_window();

    if (input && db_count != 0)
    {
        if (input->active & PKGI_BUTTON_UP)
        {
            if (selected_item >= static_cast<uint32_t>(kCols))
            {
                selected_item -= kCols;
            }
            else
            {
                const uint32_t col = selected_item % kCols;
                const uint32_t last_row_start =
                        ((db_count - 1) / kCols) * kCols;
                const uint32_t candidate = last_row_start + col;
                selected_item = candidate < db_count ? candidate : db_count - 1;
            }
            reclamp_window();
        }

        if (input->active & PKGI_BUTTON_DOWN)
        {
            if (selected_item + kCols < db_count)
            {
                selected_item += kCols;
            }
            else
            {
                const uint32_t col = selected_item % kCols;
                selected_item = col < db_count ? col : db_count - 1;
            }
            reclamp_window();
        }

        if (input->active & PKGI_BUTTON_LEFT)
        {
            selected_item = (selected_item == 0) ? db_count - 1
                                                  : selected_item - 1;
            reclamp_window();
        }

        if (input->active & PKGI_BUTTON_RIGHT)
        {
            selected_item =
                    (selected_item + 1 >= db_count) ? 0 : selected_item + 1;
            reclamp_window();
        }

        if (input->pressed & PKGI_BUTTON_LT)
        {
            bool present[PKGI_GROUP_COUNT] = {};
            for (uint32_t i = 0; i < db_count; ++i)
                present[pkgi_name_group(db.get(i)->name)] = true;
            const int current_group =
                    pkgi_name_group(db.get(selected_item)->name);
            const int group = pkgi_next_group(current_group, present, false);
            if (group != current_group || !present[current_group])
            {
                selected_item = pkgi_first_item_with_group(group);
                reclamp_window();
                pkgi_set_group_overlay(group);
            }
        }

        if (input->pressed & PKGI_BUTTON_RT)
        {
            bool present[PKGI_GROUP_COUNT] = {};
            for (uint32_t i = 0; i < db_count; ++i)
                present[pkgi_name_group(db.get(i)->name)] = true;
            const int current_group =
                    pkgi_name_group(db.get(selected_item)->name);
            const int group = pkgi_next_group(current_group, present, true);
            if (group != current_group || !present[current_group])
            {
                selected_item = pkgi_first_item_with_group(group);
                reclamp_window();
                pkgi_set_group_overlay(group);
            }
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(VITA_WIDTH, VITA_HEIGHT), 0);
    ImGui::Begin(
            "##grid",
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Reset the per-frame texture-creation budget consumed by draw_cell().
    g_tex_uploads_this_frame = 0;

    const float content_top =
            static_cast<float>(font_height + PKGI_MAIN_HLINE_EXTRA);
    const float content_h = static_cast<float>(avail_height);

    if (db_count == 0)
    {
        const char* text = "No items! Try to refresh.";
        const ImVec2 sz  = ImGui::CalcTextSize(text);
        dl->AddText(
                ImVec2((VITA_WIDTH - sz.x) * 0.5f,
                       content_top + (content_h - sz.y) * 0.5f),
                kTitleCol,
                text);
    }
    else
    {
        const float cell_w =
                (VITA_WIDTH - 2.f * kMargin - (kCols - 1) * kGap) / kCols;
        const float cell_h =
                (content_h - (kRows - 1) * kGap) / kRows;

        std::vector<DbItem*> wanted;
        wanted.reserve(kCellsPerPage);
        for (int slot = 0; slot < kCellsPerPage; ++slot)
        {
            const uint32_t idx = first_item + slot;
            if (idx >= db_count)
                break;
            wanted.push_back(db.get(idx));
        }
        g_image_cache.sync(wanted, config, mode);

        for (int slot = 0; slot < kCellsPerPage; ++slot)
        {
            const uint32_t idx = first_item + slot;
            if (idx >= db_count)
                break;

            const int row = slot / kCols;
            const int col = slot % kCols;
            const ImVec2 cell_min(
                    kMargin + col * (cell_w + kGap),
                    content_top + row * (cell_h + kGap));

            draw_cell(
                    dl,
                    db.get(idx),
                    cell_min,
                    cell_w,
                    cell_h,
                    idx == selected_item,
                    mode);
        }
    }

    ImGui::End();

    pkgi_draw_group_overlay();

    if (input && db_count != 0 && (input->pressed & pkgi_ok_button()))
    {
        input->pressed &= ~pkgi_ok_button();
        result.item_activated = true;
    }

    if (input && (input->pressed & PKGI_BUTTON_T))
    {
        input->pressed &= ~PKGI_BUTTON_T;
        result.open_menu_requested = true;
    }

    return result;
}

// Drops every cached fetcher (download/decode state) — not the texture
// pool, which stays populated. Call when leaving ModeGames or when grid
// view is toggled off, so a screen that's no longer shown doesn't keep its
// download state around forever (sync() would eventually evict it too, but
// only on its next call, which may never come once the grid is inactive).
void pkgi_grid_deactivate()
{
    g_image_cache.clear();
}
