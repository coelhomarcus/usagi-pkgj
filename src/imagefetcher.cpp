#include "imagefetcher.hpp"

#include "db.hpp"
#include "file.hpp"
#include "pkgi.hpp"
#include "curlhttp.hpp"
#include "log.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
extern SDL_Renderer* g_sdl_renderer;
static vita2d_texture* sim_load_jpeg_file(const char* path)
{
    SDL_Surface* s = IMG_Load(path);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
#define vita2d_load_JPEG_file(p)     sim_load_jpeg_file(p)
#define vita2d_wait_rendering_done() ((void)0)
#define vita2d_free_texture(t)       SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t))
#endif

#include <chrono>
#include <fmt/format.h>

namespace
{
bool uses_default_store_source(const Config* config)
{
    return !config || config->thumbnail_url.empty();
}

std::string get_store_image_url(DbItem* item)
{
    std::string country_abbv = "USA";
    std::string language = "en";
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
    {
        language = "zh";
        country_abbv = "HK";
        const std::string region = item->content.substr(0, 6);
        if (item->name.find("CHN") != std::string::npos)
        {
            country_abbv = "CN";
        }
        else if (region.compare("HP0507") == 0)
        {
            language = "ko";
            country_abbv = "KR";
        }
        else if (region.compare("HP2005") == 0)
        {
            language = "en";
        }
    }
    break;
    case RegionJPN:
        country_abbv = "JP";
        language = "ja";
        break;
    case RegionEUR:
        country_abbv = "GB";
        break;
    default:
        country_abbv = "US";
    }
    return fmt::format(
            "https://store.playstation.com/store/api/chihiro/"
            "00_09_000/container/{}/{}/19/{}/{}/image?w=248",
            country_abbv,
            language,
            item->content,
            pkgi_time_msec());
}

std::string get_image_path(const Config* config, DbItem* item)
{
    const std::string folder = config && !config->thumbnail_folder.empty()
            ? config->thumbnail_folder
            : "ux0:pkgj/cover";

    if ((!config || config->thumbnail_folder.empty()) &&
        uses_default_store_source(config))
        return fmt::format("{}/{}.cover.jpg", folder, item->titleid);

    return fmt::format("{}/{}.jpg", folder, item->titleid);
}

std::string get_image_url(const Config* config, DbItem* item)
{
    if (config && !config->thumbnail_url.empty())
        return fmt::format("{}/{}.jpg", config->thumbnail_url, item->titleid);
    return get_store_image_url(item);
}

void ensure_image_folder(const Config* config)
{
    const std::string folder = config && !config->thumbnail_folder.empty()
            ? config->thumbnail_folder
            : "ux0:pkgj/cover";
    pkgi_mkdirs(folder.c_str());
}
}

ImageFetcher::ImageFetcher(const Config* config, DbItem* item)
    : _path(get_image_path(config, item))
    , _url(get_image_url(config, item))
{
    ensure_image_folder(config);
    // Download is NOT started here.  get_status() / get_texture() drive
    // _try_submit() every frame until the WorkerSlot accepts the task.
}

ImageFetcher::~ImageFetcher()
{
    // The worker thread is owned by the global WorkerSlot singleton and
    // runs to natural completion — we never block here.
    // We simply drop _result; if the worker is still writing to it the
    // shared_ptr keeps it alive until the worker releases its own ref.
    if (_texture)
    {
        // Wait for any in-flight GPU frame before freeing the texture.
        // vita2d queues draw commands asynchronously; the destructor can
        // run right after render() while the GPU is still reading this
        // texture.  Without the wait, vita2d_free_texture triggers a GPU
        // driver crash.  The stall is at most one frame (~16 ms) and only
        // occurs when the user closes the game view.
        vita2d_wait_rendering_done();
        vita2d_free_texture(_texture);
    }
}

// ── _try_submit ──────────────────────────────────────────────────────────────
// Called every frame (via get_status) until the WorkerSlot accepts the task.
void ImageFetcher::_try_submit()
{
    // ── Fast path: file already on disc ──────────────────────────────────
    // No network I/O needed — signal the main thread to load it directly.
    if (pkgi_file_exists(_path.c_str()))
    {
        _pending_jpeg_path = _path;
        _upload_pending    = true;
        _submitted         = true;
        return; // status stays Pending until get_texture() builds the texture
    }

    if (_url.empty())
    {
        _status    = Status::Error;
        _submitted = true;
        return;
    }

    // ── Slow path: submit download to the global WorkerSlot ───────────────
    // All data captured by VALUE — the lambda must not reference 'this'.
    // If ImageFetcher is destroyed before the worker finishes, the
    // shared_ptr keeps ImageFetchResult alive until both sides drop it.
    auto result = std::make_shared<ImageFetchResult>();
    const std::string path = _path;
    const std::string url  = _url;

    if (!WorkerSlot::image_worker().try_submit(
                _path, // task_id = file path (duplicate detection)
                [result, path, url]()
                {
                    using namespace std::chrono;
                    const auto t0      = steady_clock::now();
                    const auto timeout = seconds(8);

                    auto done_error = [&]()
                    {
                        result->error = true;
                        result->ready.store(true, std::memory_order_release);
                    };
                    auto done_ok = [&](std::string p)
                    {
                        result->error = false;
                        result->path  = std::move(p);
                        result->ready.store(true, std::memory_order_release);
                    };

                    // ── Network ───────────────────────────────────────────
                    // CurlHttp uses libcurl (TLS 1.2 + ECDHE ciphers).
                    // VitaHttp (sceHttp) lacks elliptic-curve support and
                    // fails on modern HTTPS servers — curl is used instead.
                    CurlHttp http;
                    try { http.start(url, 0); }
                    catch (const std::exception& e)
                    {
                        LOGFW("[ImageFetcher] HTTP start failed for {}: {}",
                              path, e.what());
                        done_error(); return;
                    }

                    if (http.get_status() == 404) { done_error(); return; }

                    std::vector<uint8_t> data;
                    data.reserve(32 * 1024);
                    size_t pos       = 0;
                    bool   too_large = false;

                    while (true)
                    {
                        if (steady_clock::now() - t0 > timeout)
                            { done_error(); return; }

                        if (pos == data.size())
                            data.resize(pos + 4096);

                        int64_t n = 0;
                        try
                        {
                            n = http.read(
                                    data.data() + pos, data.size() - pos);
                        }
                        catch (const std::exception& e)
                        {
                            LOGFW("[ImageFetcher] HTTP read failed for {}: {}",
                                  path, e.what());
                            done_error(); return;
                        }

                        if (n == 0) break;
                        pos += static_cast<size_t>(n);
                        if (pos > ImageFetcher::MAX_SIZE_BYTES)
                            { too_large = true; break; }
                    }

                    if (too_large || pos == 0) { done_error(); return; }
                    data.resize(pos);

                    // ── Save ──────────────────────────────────────────────
                    // Write to .tmp then rename so the file is never partial.
                    void* f = nullptr;
                    try
                    {
                        const std::string tmp = path + ".tmp";
                        f = pkgi_create(tmp);
                        pkgi_write(f, data.data(), data.size());
                        pkgi_close(f); f = nullptr;
                        pkgi_rename(tmp, path);
                        done_ok(path);
                    }
                    catch (const std::exception& e)
                    {
                        if (f) pkgi_close(f);
                        LOGFW("[ImageFetcher] Failed to save {} : {}",
                              path, e.what());
                        done_error();
                    }
                }))
    {
        // Slot is busy — keep _submitted = false and retry next frame.
        return;
    }

    _result    = std::move(result);
    _submitted = true;
    _status    = Status::Downloading;
}

// ── get_status ───────────────────────────────────────────────────────────────
ImageFetcher::Status ImageFetcher::get_status()
{
    // Attempt submission every frame until the slot accepts the task.
    if (!_submitted)
        _try_submit();

    // Check if the slow-path worker has finished.
    if (_result && _result->ready.load(std::memory_order_acquire))
    {
        if (_result->error || _result->path.empty())
        {
            _status = Status::Error;
        }
        else
        {
            // Signal get_texture() to create the vita2d texture on the
            // main thread.  Status stays Pending until that happens.
            _pending_jpeg_path = std::move(_result->path);
            _upload_pending    = true;
        }
        _result.reset(); // release shared ownership
    }

    return _status;
}

// ── get_texture ──────────────────────────────────────────────────────────────
vita2d_texture* ImageFetcher::get_texture()
{
    // Process any pending worker result first.
    get_status();

    if (!_upload_pending)
        return _texture;

    // Consume the pending path and create the vita2d texture.
    // vita2d_load_JPEG_file must run on the main (render) thread.
    _upload_pending = false;
    const std::string path = std::move(_pending_jpeg_path);

    vita2d_texture* tex = vita2d_load_JPEG_file(path.c_str());
    if (!tex)
    {
        // Corrupt or unreadable cache file — delete it so the next open
        // triggers a fresh download instead of looping on the same error.
        LOGFW("[ImageFetcher] vita2d_load_JPEG_file failed for {}, removing",
              path);
        pkgi_rm(path.c_str());
    }

    _texture = tex;
    _status  = tex ? Status::Ready : Status::Error;
    return tex;
}
