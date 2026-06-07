#include "thumbnailfetcher.hpp"

#include "file.hpp"
#include "pkgi.hpp"
#include "vitahttp.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#endif

#include <chrono>
#include <fmt/format.h>
#include <mutex>

#ifdef PKGI_SIMULATOR
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
extern SDL_Renderer* g_sdl_renderer;
// SDL2 drop-in replacements for the vita2d JPEG loaders
static vita2d_texture* sim_load_jpeg_file(const char* path)
{
    SDL_Surface* s = IMG_Load(path);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
static vita2d_texture* sim_load_jpeg_buf(const void* data, size_t size)
{
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    SDL_Surface* s = rw ? IMG_Load_RW(rw, 1) : nullptr;
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
#define vita2d_load_JPEG_file(p)     sim_load_jpeg_file(p)
#define vita2d_load_JPEG_buffer(d,n) sim_load_jpeg_buf(d,n)
#endif

ThumbnailFetcher::ThumbnailFetcher(
        const std::string& titleid,
        const std::string& folder,
        const std::string& base_url)
    : _mutex("thumbnail_fetcher_mutex")
    , _path(fmt::format("{}/{}.jpg", folder, titleid))
    , _url(base_url.empty() ? "" : fmt::format("{}/{}.jpg", base_url, titleid))
    , _thread("thumbnail_fetcher", [this] { do_request(); })
{
    pkgi_mkdirs(folder.c_str());
}

ThumbnailFetcher::~ThumbnailFetcher()
{
    Http* http;
    {
        std::lock_guard<Mutex> lock(_mutex);
        _abort = true;
        http = _http.get();
    }
    if (http)
        http->abort();
    _thread.join();
}

vita2d_texture* ThumbnailFetcher::get_texture()
{
    std::lock_guard<Mutex> lock(_mutex);
    return _texture;
}

void ThumbnailFetcher::do_request()
{
    using namespace std::chrono;
    auto start_time = steady_clock::now();
    const auto timeout = seconds(8);

    try
    {
        // 1. Try loading from local cached file
        if (pkgi_file_exists(_path.c_str()))
        {
            std::lock_guard<Mutex> lock(_mutex);
            _texture = vita2d_load_JPEG_file(_path.c_str());
            if (_texture)
                return;
            // File exists but failed to decode — fall through and re-download
        }

        // 2. No URL configured → nothing more to do
        if (_url.empty())
            return;

        // 3. Abort check before network
        {
            std::lock_guard<Mutex> lock(_mutex);
            if (_abort)
                return;
            _http = std::make_unique<VitaHttp>();
        }

        // 4. Open connection
        _http->start(_url, 0);

        if (_http->get_status() == 404)
        {
            LOGF("thumbnail not found (404): {}", _url);
            std::lock_guard<Mutex> lock(_mutex);
            _http = nullptr;
            return;
        }

        // 5. Download with hard 100 KB size cap
        std::vector<uint8_t> data;
        data.reserve(32 * 1024);
        size_t pos = 0;
        bool too_large = false;

        while (true)
        {
            // Timeout check
            if (steady_clock::now() - start_time > timeout)
            {
                LOGFW("thumbnail fetch timed out after {} seconds: {}", timeout.count(), _url);
                std::lock_guard<Mutex> lock(_mutex);
                _http = nullptr;
                return;
            }

            // Check abort between each chunk
            {
                std::lock_guard<Mutex> lock(_mutex);
                if (_abort)
                {
                    _http = nullptr;
                    return;
                }
            }

            if (pos == data.size())
                data.resize(pos + 4096);

            const auto read = _http->read(data.data() + pos, data.size() - pos);
            if (read == 0)
                break;
            pos += read;

            if (pos > MAX_SIZE_BYTES)
            {
                too_large = true;
                LOGFW("thumbnail exceeds {} KB, aborting download: {}",
                     MAX_SIZE_BYTES / 1024,
                     _url);
                break;
            }
        }

        {
            std::lock_guard<Mutex> lock(_mutex);
            _http = nullptr;
        }

        if (too_large || data.empty())
            return;

        data.resize(pos);

        // 6. Decode JPEG into vita2d texture
        vita2d_texture* tex = vita2d_load_JPEG_buffer(data.data(), data.size());

        {
            std::lock_guard<Mutex> lock(_mutex);
            _texture = tex;
        }

        // 7. Save to folder so future opens don't need network
        if (tex)
        {
            auto f = pkgi_create(_path.c_str());
            if (f)
            {
                pkgi_write(f, data.data(), data.size());
                pkgi_close(f);
            }
        }
    }
    catch (const std::exception& e)
    {
        LOGFW("ThumbnailFetcher error: {}", e.what());
        std::lock_guard<Mutex> lock(_mutex);
        _http = nullptr;
    }
}
