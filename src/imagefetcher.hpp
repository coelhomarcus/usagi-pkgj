#pragma once

#include "config.hpp"
#include "workerpool.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
// Forward-declare as an opaque pointer in simulator builds.
// In sdl_backend.cpp the type is reinterpreted as SDL_Texture*.
struct vita2d_texture;
#endif

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Result written by the worker thread, read by the main thread.
// The worker sets error/path, then stores ready = true  (release).
// The main thread loads ready (acquire) before reading error/path.
// Acquire-release ordering makes path visible without a mutex.
struct ImageFetchResult
{
    std::atomic<bool> ready{false};
    bool              error{false}; // written before ready = true
    std::string       path;         // written before ready = true (extension
                                     // tells get_texture() which decoder to use)
};

// Plain CPU-side pixel data for a decoded cover: tightly packed RGBA8
// (4 bytes/pixel, row-major, R,G,B,A byte order, stride == width*4). Used
// only by ImageFetcher::take_decoded_cover() — see its comment for why the
// grid needs this instead of a lasting vita2d_texture.
struct DecodedCover
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Fetches a game's cover image, trying an ordered list of sources and
// falling back to the next one on a 404/failure.
//
// Tries the HexFlow-Covers box-art PNG first (folder picked from `mode` —
// PS Vita/PSP are vertical, PSX is square), then falls back to the
// PlayStation Store JPEG if the title isn't in that set.
class ImageFetcher
{
public:
    // A handful of the ~3989 HexFlow covers run up to ~157KB; give some
    // headroom over that so legitimate covers aren't rejected as "too large".
    static constexpr size_t MAX_SIZE_BYTES = 200 * 1024;

    enum class Status
    {
        Pending,
        Downloading,
        Ready,
        Error,
    };

    ImageFetcher(const Config* config, DbItem* item, Mode mode = ModeGames);
    ~ImageFetcher();

    // Must be called from the MAIN thread every frame.
    // Retries submission to the global WorkerPool while every slot is busy.
    // Used by GameView only — the grid uses take_decoded_cover() instead
    // (see its comment for why).
    vita2d_texture* get_texture();
    Status          get_status();

    // Grid-only alternative to get_texture(): decodes into a plain pixel
    // buffer (see DecodedCover) instead of a lasting vita2d_texture, for the
    // caller to copy into its own persistent texture-pool slot
    // (GridTexturePool, src/gridtexturepool.hpp).
    //
    // On Vita this path decodes with CPU-side libpng/libjpeg helpers instead
    // of making a temporary vita2d_texture. That keeps grid cover decode out
    // of the GXM map/unmap lifetime entirely; the only texture touched by the
    // grid is the persistent GridTexturePool slot. The caller decides how many
    // of these to request per frame; skip the call entirely to rate-limit
    // rather than passing a flag in.
    //
    // Returns std::nullopt if not ready yet (still downloading), on decode
    // failure, or if already taken — each ImageFetcher only ever yields one
    // cover, once.
    std::optional<DecodedCover> take_decoded_cover();

private:
    struct Source
    {
        std::string path; // local cache path (extension implies format)
        std::string url;
    };
    // Priority order: index 0 tried first, later entries are fallbacks.
    std::vector<Source> _sources;
    static std::vector<Source> _build_sources(
            const Config* config, DbItem* item, Mode mode);

    bool   _submitted{false};       // true once the slot accepted the task
    bool   _disk_checked{false};    // true once the on-disk cache check ran
    bool   _waiting_for_existing_task{false};
    bool   _cover_taken{false};     // true once take_decoded_cover() consumed it
    Status _status{Status::Pending};

    // Slow-path result from the worker (released once processed).
    std::shared_ptr<ImageFetchResult> _result;

    vita2d_texture* _texture{nullptr};
    bool            _upload_pending{false};
    std::string     _pending_image_path;

    // Try to hand the download task to WorkerPool::image_workers().
    // Called every frame via get_status() until the slot accepts it.
    void _try_submit();
};
