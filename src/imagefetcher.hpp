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

// Result written by the worker thread, read by the main thread.
// The worker sets error/path, then stores ready = true  (release).
// The main thread loads ready (acquire) before reading error/path.
// Acquire-release ordering makes path visible without a mutex.
struct ImageFetchResult
{
    std::atomic<bool> ready{false};
    bool              error{false}; // written before ready = true
    std::string       path;         // written before ready = true
};

class ImageFetcher
{
public:
    static constexpr size_t MAX_SIZE_BYTES = 100 * 1024;

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
    std::string _path;
    std::string _url;

    bool   _submitted{false};       // true once the slot accepted the task
    Status _status{Status::Pending};

    // Slow-path result from the worker (released once processed).
    std::shared_ptr<ImageFetchResult> _result;

    vita2d_texture* _texture{nullptr};
    bool            _upload_pending{false};
    std::string     _pending_jpeg_path;

    // Try to hand the download task to WorkerSlot::image_worker().
    // Called every frame via get_status() until the slot accepts it.
    void _try_submit();
};
