#pragma once

#include "imagefetcher.hpp"
#include "imgui.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

#include <optional>
#include <string>

// A fixed pool of persistent cover-art texture slots for the grid view,
// keyed by titleid, that are NEVER individually freed during the session.
//
// The grid used to give each visible cover its own vita2d_texture, created
// on demand and freed when scrolled off-screen. On Vita3K that crashed
// (renderer::vulkan::VKTextureCache::upload_texture_impl, reading a freed
// source buffer at a guard page): the emulator's Vulkan backend syncs a
// texture's memory to a real VkImage lazily, on the first draw call that
// references it, and does so on its own async render thread — so a texture
// freed shortly after being drawn could still have a queued-but-not-yet-
// -executed upload command pointing at memory that's already gone. A
// cooldown-delayed free (see git history) narrowed the window but never
// closed it, and fast scrolling — many evictions in quick succession — was
// enough to hit it again.
//
// This pool sidesteps the problem structurally instead of timing around it:
// kSlotCount textures are allocated once (lazily, as first needed) and kept
// alive for the process lifetime. A cover is displayed by copying its
// decoded pixels (ImageFetcher::take_decoded_cover(), which never creates a
// lasting texture of its own) into a slot's existing texture and drawing
// that — content changes, but the texture object and its GPU memory never
// do, so there's never a moment where Vita3K's render thread could be
// referencing memory that's already been returned to the system. Slots are
// reused LRU once all kSlotCount are in use.
//
// All slots share one fixed canvas size (kSlotW x kSlotH) so every cover —
// PS Vita/PSP's vertical box art or PSX's square art — fits inside without
// needing per-content-type texture formats; each store() proportionally
// fits the source into that canvas (never upscaling) and reports back the
// UV sub-rect actually holding pixels, since anything smaller than the full
// canvas is letterboxed with transparent padding.
class GridTexturePool
{
public:
    static constexpr int kSlotW = 256;
    static constexpr int kSlotH = 320;
    static constexpr int kSlotCount = 24; // ~7.5MB worst case (256*320*4B/slot)

    struct Entry
    {
        vita2d_texture* tex = nullptr;
        // Cover's actual size within the slot, in pixels, after
        // proportional-fit scaling (<= kSlotW x kSlotH). Use this — not the
        // slot's full size — for on-screen box-fit math, the same way
        // callers used to use the texture's own reported width/height.
        float content_w = 0.f;
        float content_h = 0.f;
        // Sub-rect of the slot (in [0,1] UV space) actually holding the
        // cover; the rest of the canvas is transparent padding.
        ImVec2 uv_min{0.f, 0.f};
        ImVec2 uv_max{1.f, 1.f};
    };

    // Returns the slot already holding `key`'s cover, marking it
    // most-recently-used, or nullopt if nothing is cached for it yet.
    std::optional<Entry> find(const std::string& key);

    // Fits `cover` proportionally into a slot — reusing an unallocated one
    // if any remain, else evicting the least-recently-used populated one —
    // and blits it in, replacing that slot's previous content entirely
    // (including clearing the old letterbox padding). Returns the resulting
    // entry, or an entry with tex == nullptr if `cover` is empty/invalid or
    // slot allocation failed (the caller should fall back to a placeholder).
    Entry store(const std::string& key, const DecodedCover& cover);

private:
    struct Slot
    {
        vita2d_texture* tex = nullptr; // null until lazily allocated
        std::string key;               // empty if unpopulated
        float content_w = 0.f;
        float content_h = 0.f;
        ImVec2 uv_min{0.f, 0.f};
        ImVec2 uv_max{1.f, 1.f};
        uint32_t last_used = 0;
    };

    void blit_into_slot(Slot& slot, const DecodedCover& cover);

    Slot _slots[kSlotCount];
    uint32_t _access_counter = 0;
};
