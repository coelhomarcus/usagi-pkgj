#include "gridtexturepool.hpp"

#include "log.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
#include <SDL2/SDL.h>
extern SDL_Renderer* g_sdl_renderer;
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

std::optional<GridTexturePool::Entry> GridTexturePool::find(const std::string& key)
{
    for (Slot& s : _slots)
    {
        if (s.tex && s.key == key)
        {
            s.last_used = ++_access_counter;
            return Entry{s.tex, s.content_w, s.content_h, s.uv_min, s.uv_max};
        }
    }
    return std::nullopt;
}

GridTexturePool::Entry GridTexturePool::store(
        const std::string& key, const DecodedCover& cover)
{
    if (cover.width == 0 || cover.height == 0 || cover.pixels.empty())
        return {};

    // Prefer growing the pool (reuse an unallocated slot) over evicting a
    // populated one, so early scrolling doesn't unnecessarily discard
    // recently-shown covers while slots are still available.
    Slot* slot = nullptr;
    for (Slot& s : _slots)
    {
        if (!s.tex)
        {
            slot = &s;
            break;
        }
    }
    if (!slot)
    {
        slot = &_slots[0];
        for (Slot& s : _slots)
            if (s.last_used < slot->last_used)
                slot = &s;
    }

    const bool reusing_populated_slot = slot->tex && !slot->key.empty();

    if (!slot->tex)
    {
#ifndef PKGI_SIMULATOR
        slot->tex = vita2d_create_empty_texture_format(
                kSlotW, kSlotH, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
#else
        slot->tex = reinterpret_cast<vita2d_texture*>(SDL_CreateTexture(
                g_sdl_renderer,
                SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING,
                kSlotW,
                kSlotH));
#endif
        if (!slot->tex)
        {
            LOGFW("[GridTexturePool] failed to allocate slot texture");
            return {};
        }
    }

#ifndef PKGI_SIMULATOR
    if (reusing_populated_slot)
    {
        // The pool keeps texture objects alive, but reusing a slot still
        // mutates the same backing memory Vita3K may be copying into a Vulkan
        // staging buffer from the previous frame. Wait before overwriting the
        // slot's CPU-side pixels so reuse has the same lifetime guarantee as
        // the earlier "wait before free" path.
        vita2d_wait_rendering_done();
    }
#endif

    blit_into_slot(*slot, cover);
    slot->key       = key;
    slot->last_used = ++_access_counter;

    return {slot->tex, slot->content_w, slot->content_h, slot->uv_min, slot->uv_max};
}

// Fits `cover` proportionally into the slot's kSlotW x kSlotH canvas
// (nearest-neighbor; covers are small thumbnails so bilinear would be
// wasted complexity), clearing the rest as transparent padding so a reused
// slot's previous content never shows through, then records the resulting
// content size and UV sub-rect on the slot.
void GridTexturePool::blit_into_slot(Slot& slot, const DecodedCover& cover)
{
    // Never upscale: all real sources (HexFlow/PS Store covers) are already
    // <= the slot canvas, and upscaling a smaller-than-expected image would
    // look worse than just centering it with more padding.
    const float scale = std::min(
            1.0f,
            std::min(
                    static_cast<float>(kSlotW) / cover.width,
                    static_cast<float>(kSlotH) / cover.height));

    const int fit_w = std::max(
            1, static_cast<int>(std::lround(cover.width * scale)));
    const int fit_h = std::max(
            1, static_cast<int>(std::lround(cover.height * scale)));
    const int off_x = (kSlotW - fit_w) / 2;
    const int off_y = (kSlotH - fit_h) / 2;

    // In virtually every real case (HexFlow/PS Store covers are already
    // <= the slot canvas) scale == 1.0 and no resampling is needed at all —
    // a straight per-row memcpy. The per-pixel nearest-neighbor path below
    // only runs for the rare oversized/unexpected source, where it's worth
    // paying for since it's no longer the common case.
    const bool exact_fit = (scale == 1.0f);
    const size_t src_stride = static_cast<size_t>(cover.width) * 4;

    auto sample_row = [&](int dst_y) -> const uint8_t*
    {
        const int sy = std::min(
                static_cast<int>(dst_y / scale),
                static_cast<int>(cover.height) - 1);
        return cover.pixels.data() + static_cast<size_t>(sy) * src_stride;
    };
    auto sample_col = [&](int dst_x) -> int
    {
        return std::min(
                static_cast<int>(dst_x / scale),
                static_cast<int>(cover.width) - 1);
    };

#ifndef PKGI_SIMULATOR
    uint8_t* dst = static_cast<uint8_t*>(vita2d_texture_get_datap(slot.tex));
    const unsigned int dst_stride = vita2d_texture_get_stride(slot.tex);

    for (int y = 0; y < kSlotH; ++y)
        memset(dst + static_cast<size_t>(y) * dst_stride, 0, kSlotW * 4);

    if (exact_fit)
    {
        for (int y = 0; y < fit_h; ++y)
        {
            uint8_t* drow = dst + static_cast<size_t>(off_y + y) * dst_stride +
                    static_cast<size_t>(off_x) * 4;
            memcpy(drow,
                   cover.pixels.data() + static_cast<size_t>(y) * src_stride,
                   static_cast<size_t>(fit_w) * 4);
        }
    }
    else
    {
        for (int y = 0; y < fit_h; ++y)
        {
            const uint8_t* srow = sample_row(y);
            uint8_t* drow = dst + static_cast<size_t>(off_y + y) * dst_stride +
                    static_cast<size_t>(off_x) * 4;
            for (int x = 0; x < fit_w; ++x)
                memcpy(drow + x * 4, srow + sample_col(x) * 4, 4);
        }
    }
#else
    // Build the whole canvas in a temp CPU buffer, then hand it to SDL in
    // one SDL_UpdateTexture call — simpler than juggling SDL_LockTexture's
    // partial-rect semantics for both the clear and the blit.
    std::vector<uint8_t> canvas(static_cast<size_t>(kSlotW) * kSlotH * 4, 0);

    if (exact_fit)
    {
        for (int y = 0; y < fit_h; ++y)
        {
            uint8_t* drow = canvas.data() +
                    static_cast<size_t>(off_y + y) * kSlotW * 4 +
                    static_cast<size_t>(off_x) * 4;
            memcpy(drow,
                   cover.pixels.data() + static_cast<size_t>(y) * src_stride,
                   static_cast<size_t>(fit_w) * 4);
        }
    }
    else
    {
        for (int y = 0; y < fit_h; ++y)
        {
            const uint8_t* srow = sample_row(y);
            uint8_t* drow = canvas.data() +
                    static_cast<size_t>(off_y + y) * kSlotW * 4 +
                    static_cast<size_t>(off_x) * 4;
            for (int x = 0; x < fit_w; ++x)
                memcpy(drow + x * 4, srow + sample_col(x) * 4, 4);
        }
    }

    SDL_UpdateTexture(
            reinterpret_cast<SDL_Texture*>(slot.tex),
            nullptr,
            canvas.data(),
            kSlotW * 4);
#endif

    slot.content_w = static_cast<float>(fit_w);
    slot.content_h = static_cast<float>(fit_h);
    slot.uv_min = ImVec2(
            static_cast<float>(off_x) / kSlotW,
            static_cast<float>(off_y) / kSlotH);
    slot.uv_max = ImVec2(
            static_cast<float>(off_x + fit_w) / kSlotW,
            static_cast<float>(off_y + fit_h) / kSlotH);
}
