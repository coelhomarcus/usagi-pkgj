#pragma once

#include "http.hpp"
#include "thread.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

#include <string>

// Fetches a game screenshot/thumbnail image.
//
// Resolution order (non-blocking, runs on a background thread):
//   1. Load from  {folder}/{titleid}.jpg  if it already exists on-device.
//   2. Download   {base_url}/{titleid}.jpg from the network (if base_url != "").
//      Download is aborted if the file would exceed MAX_SIZE_BYTES (100 KB).
//      On success the file is cached to {folder}/{titleid}.jpg for future use.
//
// The folder and base_url come from config.txt:
//   thumbnail_folder  ux0:usagi-pkgj/thumbnails
//   thumbnail_url     https://example.com/thumbs
//
// Users can also just copy JPEG files manually to the configured folder.
class ThumbnailFetcher
{
public:
    static constexpr size_t MAX_SIZE_BYTES = 100 * 1024; // 100 KB hard limit

    // folder    : directory that stores the cached JPEG files
    // base_url  : URL prefix; full URL = base_url + "/" + titleid + ".jpg"
    //             Pass an empty string to disable network fetching.
    ThumbnailFetcher(
            const std::string& titleid,
            const std::string& folder,
            const std::string& base_url);
    ~ThumbnailFetcher();

    vita2d_texture* get_texture();

private:
    Mutex _mutex;

    std::string _path;    // local file path
    std::string _url;     // full download URL (empty = disabled)
    bool _abort{false};
    std::unique_ptr<Http> _http;
    vita2d_texture* _texture{nullptr};

    Thread _thread;

    void do_request();
};
