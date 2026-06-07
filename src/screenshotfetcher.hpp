#pragma once

#include "config.hpp"
#include "db.hpp"
#include "thread.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

#include <string>

// Fetches screenshots for a game from the PlayStation Store chihiro API.
// Phase 1: downloads the container JSON to discover actual screenshot URLs
// (skipping the first image, which is typically the cover / box-art).
// Phase 2: downloads each screenshot image sequentially and caches to disk.
//
// All public methods (get_status, get_texture) must be called from the
// MAIN thread only.
class ScreenshotFetcher
{
public:
    static constexpr int    MAX_SCREENSHOTS = 4;
    static constexpr size_t MAX_SIZE_BYTES  = 256 * 1024; // 256 KB per image

    enum class Status
    {
        Pending,
        Downloading,
        Ready,
        Error,
    };

    ScreenshotFetcher(const Config* config, const DbItem* item);
    ~ScreenshotFetcher();

    // Returns the texture for 'index', or nullptr if not ready / error.
    // Also drives the texture-upload step (must be called every frame).
    vita2d_texture* get_texture(int index);
    Status          get_status(int index);

private:
    struct Slot
    {
        Status          status{Status::Pending};
        std::string     path;            // cache file path (set under mutex)
        vita2d_texture* texture{nullptr};
        bool            upload_pending{false}; // file ready, needs vita2d load
    };

    std::string _titleid;
    std::string _folder;   // cache directory
    std::string _json_url; // chihiro container URL (returns JSON)

    Slot _slots[MAX_SCREENSHOTS];
    int  _count{0};        // screenshot count discovered from JSON (0 until known)
    bool _json_done{false};

    Mutex  _mutex;
    bool   _abort{false};
    Thread _thread;

    void do_work(); // runs on background thread
};
