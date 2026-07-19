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

// Fetches a game's cover image, trying an ordered list of sources and
// falling back to the next one on a 404/failure.
//
// Default (no config->thumbnail_url): tries the HexFlow-Covers vertical
// box-art PNG first, then falls back to the PlayStation Store JPEG if the
// title isn't in that set. If config->thumbnail_url is set, it is the only
// source tried (matches the documented "custom cover source" override).
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

    ImageFetcher(const Config* config, DbItem* item);
    ~ImageFetcher();

    // Must be called from the MAIN thread every frame.
    // Retries submission to the global WorkerSlot while the slot is busy.
    vita2d_texture* get_texture();
    Status          get_status();

private:
    struct Source
    {
        std::string path; // local cache path (extension implies format)
        std::string url;
    };
    // Priority order: index 0 tried first, later entries are fallbacks.
    std::vector<Source> _sources;
    static std::vector<Source> _build_sources(const Config* config, DbItem* item);

    bool   _submitted{false};       // true once the slot accepted the task
    Status _status{Status::Pending};

    // Slow-path result from the worker (released once processed).
    std::shared_ptr<ImageFetchResult> _result;

    vita2d_texture* _texture{nullptr};
    bool            _upload_pending{false};
    std::string     _pending_image_path;

    // Try to hand the download task to WorkerSlot::image_worker().
    // Called every frame via get_status() until the slot accepts it.
    void _try_submit();
};
