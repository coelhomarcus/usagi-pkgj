#include "screenshotfetcher.hpp"

#include "curlhttp.hpp"
#include "file.hpp"
#include "log.hpp"
#include "pkgi.hpp"

#include <fmt/format.h>
#include <mutex>
#include <vector>

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
extern SDL_Renderer* g_sdl_renderer;

static vita2d_texture* sim_ss_load_jpeg(const char* path)
{
    SDL_Surface* s = IMG_Load(path);
    if (!s)
        return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
#define vita2d_load_JPEG_file(p)     sim_ss_load_jpeg(p)
#define vita2d_wait_rendering_done() ((void)0)
#define vita2d_free_texture(t) \
    SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t))
#endif

namespace
{
// ── Region helpers ────────────────────────────────────────────────────────────

static std::string ss_get_country(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
        if (item->name.find("CHN") != std::string::npos)
            return "CN";
        if (item->content.size() >= 6 && item->content.substr(0, 6) == "HP0507")
            return "KR";
        return "HK";
    case RegionJPN:
        return "JP";
    case RegionEUR:
        return "GB";
    default:
        return "US";
    }
}

static std::string ss_get_language(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
        if (item->content.size() >= 6 && item->content.substr(0, 6) == "HP0507")
            return "ko";
        return "zh";
    case RegionJPN:
        return "ja";
    default:
        return "en";
    }
}

// ── JSON screenshot URL extractor ────────────────────────────────────────────
// Parses the "images" array from a chihiro container JSON response.
// The first entry in the array is typically the cover / box-art — we skip it
// and return up to max_count subsequent image URLs as screenshots.
static std::vector<std::string> extract_screenshot_urls(
        const std::string& json, int max_count)
{
    std::vector<std::string> result;

    // Find the "images" key and its array opening bracket.
    auto pos = json.find("\"images\"");
    if (pos == std::string::npos)
        return result;
    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return result;
    ++pos; // step past '['

    int img_index = 0;
    while (pos < json.size() && (int)result.size() < max_count)
    {
        // Advance to the next '{' (image object start) or ']' (array end).
        while (pos < json.size() && json[pos] != '{' && json[pos] != ']')
            ++pos;
        if (pos >= json.size() || json[pos] == ']')
            break;

        ++pos; // skip '{'

        // Find matching '}' — depth-aware, string-aware.
        const size_t obj_start = pos;
        int          depth     = 1;
        bool         in_str    = false;
        bool         esc       = false;
        while (pos < json.size() && depth > 0)
        {
            char c = json[pos];
            if (esc)
                esc = false;
            else if (c == '\\' && in_str)
                esc = true;
            else if (c == '"')
                in_str = !in_str;
            else if (!in_str)
            {
                if (c == '{')
                    ++depth;
                else if (c == '}')
                    --depth;
            }
            ++pos;
        }
        // obj_start .. pos-1 is the object body (without enclosing braces).
        const std::string obj = json.substr(obj_start, pos - 1 - obj_start);

        // Extract the "url" field value from this object.
        static const std::string URL_KEY = "\"url\":\"";
        auto u = obj.find(URL_KEY);
        if (u != std::string::npos)
        {
            u += URL_KEY.size();
            std::string url;
            bool        esc2 = false;
            for (; u < obj.size(); ++u)
            {
                char c = obj[u];
                if (esc2)
                {
                    url += c;
                    esc2 = false;
                }
                else if (c == '\\')
                    esc2 = true;
                else if (c == '"')
                    break;
                else
                    url += c;
            }
            // img_index == 0 → first image = cover art → skip.
            if (img_index > 0 && !url.empty())
                result.push_back(url);
            ++img_index;
        }
    }
    return result;
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

// Read all bytes from an already-started CurlHttp, up to max_bytes.
static bool read_http_body(
        CurlHttp& http, std::vector<uint8_t>& buf, size_t max_bytes)
{
    buf.clear();
    buf.reserve(32 * 1024);
    size_t pos = 0;
    while (true)
    {
        if (pos == buf.size())
            buf.resize(pos + 4096);
        int64_t n = 0;
        try
        {
            n = http.read(buf.data() + pos, buf.size() - pos);
        }
        catch (...)
        {
            break;
        }
        if (n == 0)
            break;
        pos += static_cast<size_t>(n);
        if (pos > max_bytes)
            break;
    }
    buf.resize(pos);
    return pos > 0;
}

// Save data to path atomically (write .tmp then rename).
static bool save_to_file(
        const std::string& path, const std::vector<uint8_t>& data)
{
    void* f = nullptr;
    try
    {
        const std::string tmp = path + ".tmp";
        f                     = pkgi_create(tmp);
        pkgi_write(f, data.data(), data.size());
        pkgi_close(f);
        f = nullptr;
        pkgi_rename(tmp, path);
        return true;
    }
    catch (...)
    {
        if (f)
            pkgi_close(f);
        return false;
    }
}

} // namespace

// ── ScreenshotFetcher ─────────────────────────────────────────────────────────

ScreenshotFetcher::ScreenshotFetcher(const Config* config, const DbItem* item)
    : _mutex("ss_fetcher_mutex")
    , _thread("ss_fetcher", [this] { do_work(); })
{
    _titleid  = item->titleid;
    _folder   = (config && !config->thumbnail_folder.empty())
                ? config->thumbnail_folder
                : "ux0:pkgj/cover";

    const auto country  = ss_get_country(item);
    const auto language = ss_get_language(item);
    _json_url = fmt::format(
            "https://store.playstation.com/store/api/chihiro/"
            "00_09_000/container/{}/{}/19/{}",
            country,
            language,
            item->content);
}

ScreenshotFetcher::~ScreenshotFetcher()
{
    {
        std::lock_guard<Mutex> lk(_mutex);
        _abort = true;
    }
    _thread.join();
    vita2d_wait_rendering_done();
    for (int i = 0; i < MAX_SCREENSHOTS; ++i)
        if (_slots[i].texture)
            vita2d_free_texture(_slots[i].texture);
}

void ScreenshotFetcher::do_work()
{
    // ── Phase 1: download container JSON and extract screenshot URLs ──────────
    {
        std::lock_guard<Mutex> lk(_mutex);
        if (_abort)
            return;
    }

    CurlHttp json_http;
    try
    {
        json_http.start(_json_url, 0);
    }
    catch (const std::exception& e)
    {
        LOGFW("[ScreenshotFetcher] JSON HTTP start failed for {}: {}",
              _titleid,
              e.what());
        std::lock_guard<Mutex> lk(_mutex);
        _json_done = true;
        return;
    }

    if (json_http.get_status() != 200)
    {
        LOGFW("[ScreenshotFetcher] JSON HTTP {} for {}", json_http.get_status(),
              _titleid);
        std::lock_guard<Mutex> lk(_mutex);
        _json_done = true;
        return;
    }

    std::vector<uint8_t> json_buf;
    read_http_body(json_http, json_buf, 512 * 1024);
    const std::string json(
            reinterpret_cast<const char*>(json_buf.data()), json_buf.size());

    const auto urls = extract_screenshot_urls(json, MAX_SCREENSHOTS);

    {
        std::lock_guard<Mutex> lk(_mutex);
        _count    = static_cast<int>(urls.size());
        _json_done = true;
        if (urls.empty())
        {
            LOGF("[ScreenshotFetcher] no screenshots found in JSON for {}",
                 _titleid);
            return;
        }
    }

    // ── Phase 2: download each screenshot image sequentially ─────────────────
    for (int i = 0; i < (int)urls.size() && i < MAX_SCREENSHOTS; ++i)
    {
        {
            std::lock_guard<Mutex> lk(_mutex);
            if (_abort)
                return;
            _slots[i].status = Status::Downloading;
        }

        const std::string path =
                fmt::format("{}/{}.ss{}.jpg", _folder, _titleid, i);

        // Serve from disk cache if available.
        if (pkgi_file_exists(path.c_str()))
        {
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].path           = path;
            _slots[i].upload_pending = true;
            continue;
        }

        // Download from the URL extracted from the JSON.
        CurlHttp img_http;
        try
        {
            img_http.start(urls[i], 0);
        }
        catch (const std::exception& e)
        {
            LOGFW("[ScreenshotFetcher] image {} HTTP start failed: {}",
                  i, e.what());
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].status = Status::Error;
            continue;
        }

        if (img_http.get_status() == 404)
        {
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].status = Status::Error;
            continue;
        }

        std::vector<uint8_t> img_buf;
        read_http_body(img_http, img_buf, MAX_SIZE_BYTES);

        if (img_buf.empty())
        {
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].status = Status::Error;
            continue;
        }

        if (!save_to_file(path, img_buf))
        {
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].status = Status::Error;
            continue;
        }

        {
            std::lock_guard<Mutex> lk(_mutex);
            _slots[i].path           = path;
            _slots[i].upload_pending = true;
        }
    }
}

ScreenshotFetcher::Status ScreenshotFetcher::get_status(int index)
{
    if (index < 0 || index >= MAX_SCREENSHOTS)
        return Status::Error;

    std::lock_guard<Mutex> lk(_mutex);

    // Once JSON parsing is done, slots beyond what was found are errors.
    if (_json_done && index >= _count &&
        _slots[index].status == Status::Pending)
        _slots[index].status = Status::Error;

    return _slots[index].status;
}

vita2d_texture* ScreenshotFetcher::get_texture(int index)
{
    if (index < 0 || index >= MAX_SCREENSHOTS)
        return nullptr;

    // Check (and consume) upload_pending under the mutex.
    std::string path_to_load;
    {
        std::lock_guard<Mutex> lk(_mutex);

        // Also drive the JSON-done → Error transition.
        if (_json_done && index >= _count &&
            _slots[index].status == Status::Pending)
            _slots[index].status = Status::Error;

        if (!_slots[index].upload_pending)
            return _slots[index].texture;

        _slots[index].upload_pending = false;
        path_to_load                 = _slots[index].path;
    }

    // Load the JPEG on the main thread (vita2d requirement).
    vita2d_texture* tex = vita2d_load_JPEG_file(path_to_load.c_str());
    if (!tex)
    {
        LOGFW("[ScreenshotFetcher] vita2d_load_JPEG_file failed for {}",
              path_to_load);
        pkgi_rm(path_to_load.c_str());
        std::lock_guard<Mutex> lk(_mutex);
        _slots[index].status = Status::Error;
        return nullptr;
    }

    std::lock_guard<Mutex> lk(_mutex);
    _slots[index].texture = tex;
    _slots[index].status  = Status::Ready;
    return tex;
}

