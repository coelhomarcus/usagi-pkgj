#include "imagefetcher.hpp"

#include "db.hpp"
#include "file.hpp"
#include "pkgi.hpp"
#include "curlhttp.hpp"
#include "log.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
extern SDL_Renderer* g_sdl_renderer;
static vita2d_texture* sim_load_image_file(const char* path)
{
    SDL_Surface* s = IMG_Load(path);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
}
#define vita2d_load_JPEG_file(p)     sim_load_image_file(p)
#define vita2d_wait_rendering_done() ((void)0)
#define vita2d_free_texture(t)       SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t))
#endif

#include <chrono>
#include <cstring>
#include <fmt/format.h>
#include <limits>

namespace
{
// Default cover source: box art curated by the HexFlow project
// (https://github.com/Andiweli/HexFlow-Covers), served from this fork's own
// mirror (https://github.com/coelhomarcus/HexFlow-Covers). One folder per
// content type — PS Vita and PSP covers are vertical box art, PSX covers
// are square. Titles missing from their set fall back to the PlayStation
// Store below.
constexpr const char* kCoverBaseUrlVita =
        "https://raw.githubusercontent.com/coelhomarcus/HexFlow-Covers/main/Covers/PSVita";
constexpr const char* kCoverBaseUrlPsp =
        "https://raw.githubusercontent.com/coelhomarcus/HexFlow-Covers/main/Covers/PSP";
constexpr const char* kCoverBaseUrlPsx =
        "https://raw.githubusercontent.com/coelhomarcus/HexFlow-Covers/main/Covers/PS1";

const char* cover_base_url_for_mode(Mode mode)
{
    switch (mode)
    {
    case ModePspGames:
        return kCoverBaseUrlPsp;
    case ModePsxGames:
        return kCoverBaseUrlPsx;
    default:
        return kCoverBaseUrlVita;
    }
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

void ensure_image_folder(const Config* config)
{
    const std::string folder = config && !config->thumbnail_folder.empty()
            ? config->thumbnail_folder
            : "ux0:usagi-pkgj/cover";
    pkgi_mkdirs(folder.c_str());
}

// vita2d_load_PNG_file (a path-based loader) has no proven-working call
// site anywhere in this codebase — the only PNG load this app already
// relies on in production is vita2d_load_PNG_buffer, used for the bundled
// UI icon assets (see vita.cpp). Cover PNGs are decoded the same
// buffer-based way instead of trusting the untested file-based entry point.
vita2d_texture* decode_png_from_memory(const std::vector<uint8_t>& data)
{
#ifndef PKGI_SIMULATOR
    return vita2d_load_PNG_buffer(data.data());
#else
    SDL_RWops* rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
    SDL_Surface* s = rw ? IMG_Load_RW(rw, 1) : nullptr;
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_sdl_renderer, s);
    SDL_FreeSurface(s);
    return reinterpret_cast<vita2d_texture*>(t);
#endif
}

// Shared by get_texture() and take_decoded_cover(): the cache file's
// extension (set when _build_sources() picked the source) says which
// decoder to use.
vita2d_texture* decode_cover_from_path(const std::string& path)
{
    const bool is_png = path.size() >= 4 &&
            path.compare(path.size() - 4, 4, ".png") == 0;
    if (is_png)
    {
        try
        {
            return decode_png_from_memory(pkgi_load(path));
        }
        catch (const std::exception& e)
        {
            LOGFW("[ImageFetcher] failed to read {}: {}", path, e.what());
            return nullptr;
        }
    }
    return vita2d_load_JPEG_file(path.c_str());
}

#ifndef PKGI_SIMULATOR
bool cover_size_is_valid(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return false;

    const size_t max = std::numeric_limits<size_t>::max();
    return static_cast<size_t>(width) <= max / height / 4;
}

DecodedCover decode_png_cover_from_memory(const std::vector<uint8_t>& data)
{
    DecodedCover cover;

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, data.data(), data.size()))
        return cover;

    image.format = PNG_FORMAT_RGBA;
    if (!cover_size_is_valid(image.width, image.height))
    {
        png_image_free(&image);
        return cover;
    }

    cover.width  = image.width;
    cover.height = image.height;
    cover.pixels.resize(static_cast<size_t>(cover.width) * cover.height * 4);

    if (!png_image_finish_read(&image, nullptr, cover.pixels.data(), 0, nullptr))
    {
        cover = {};
        png_image_free(&image);
        return cover;
    }

    png_image_free(&image);
    return cover;
}

struct JpegErrorManager
{
    jpeg_error_mgr pub;
    jmp_buf        jump;
};

void jpeg_error_exit(j_common_ptr cinfo)
{
    auto* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    longjmp(err->jump, 1);
}

DecodedCover decode_jpeg_cover_from_memory(const std::vector<uint8_t>& data)
{
    DecodedCover cover;

    jpeg_decompress_struct cinfo;
    memset(&cinfo, 0, sizeof(cinfo));

    JpegErrorManager jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    bool created = false;
    if (setjmp(jerr.jump))
    {
        if (created)
            jpeg_destroy_decompress(&cinfo);
        return {};
    }

    jpeg_create_decompress(&cinfo);
    created = true;
    jpeg_mem_src(&cinfo, data.data(), data.size());
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    const uint32_t width  = cinfo.output_width;
    const uint32_t height = cinfo.output_height;
    if (!cover_size_is_valid(width, height) || cinfo.output_components == 0)
    {
        jpeg_destroy_decompress(&cinfo);
        return cover;
    }

    cover.width  = width;
    cover.height = height;
    cover.pixels.resize(static_cast<size_t>(width) * height * 4);

    const size_t component_count = cinfo.output_components;
    std::vector<uint8_t> row(static_cast<size_t>(width) * component_count);

    while (cinfo.output_scanline < cinfo.output_height)
    {
        const JDIMENSION y = cinfo.output_scanline;
        JSAMPROW rowp = row.data();
        jpeg_read_scanlines(&cinfo, &rowp, 1);

        uint8_t* dst = cover.pixels.data() + static_cast<size_t>(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint8_t* src = row.data() + static_cast<size_t>(x) * component_count;
            if (component_count == 1)
            {
                dst[x * 4 + 0] = src[0];
                dst[x * 4 + 1] = src[0];
                dst[x * 4 + 2] = src[0];
            }
            else
            {
                dst[x * 4 + 0] = src[0];
                dst[x * 4 + 1] = src[1];
                dst[x * 4 + 2] = src[2];
            }
            dst[x * 4 + 3] = 255;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return cover;
}
#else
// Decodes straight into an SDL_Surface — unlike get_texture()'s path via
// decode_png_from_memory()/vita2d_load_JPEG_file(), this never becomes an
// SDL_Texture at all, so there's no GPU-side handle to free (those helpers
// produce SDL_TEXTUREACCESS_STATIC textures, which SDL doesn't allow
// locking/reading back from anyway). SDL_image auto-detects PNG vs JPEG
// from the file's content, so no extension check is needed here.
DecodedCover decode_cover_surface(const std::string& path)
{
    DecodedCover cover;

    SDL_Surface* raw = IMG_Load(path.c_str());
    if (!raw)
        return cover;

    SDL_Surface* conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(raw);
    if (!conv)
        return cover;

    cover.width  = static_cast<uint32_t>(conv->w);
    cover.height = static_cast<uint32_t>(conv->h);
    cover.pixels.resize(static_cast<size_t>(conv->w) * conv->h * 4);

    SDL_LockSurface(conv);
    for (int y = 0; y < conv->h; ++y)
    {
        const uint8_t* srow =
                static_cast<const uint8_t*>(conv->pixels) +
                static_cast<size_t>(y) * conv->pitch;
        uint8_t* drow =
                cover.pixels.data() + static_cast<size_t>(y) * conv->w * 4;
        memcpy(drow, srow, static_cast<size_t>(conv->w) * 4);
    }
    SDL_UnlockSurface(conv);
    SDL_FreeSurface(conv);

    return cover;
}
#endif

DecodedCover decode_cover_pixels_from_path(const std::string& path)
{
#ifndef PKGI_SIMULATOR
    try
    {
        const std::vector<uint8_t> data = pkgi_load(path);
        const bool is_png = path.size() >= 4 &&
                path.compare(path.size() - 4, 4, ".png") == 0;
        return is_png ? decode_png_cover_from_memory(data)
                      : decode_jpeg_cover_from_memory(data);
    }
    catch (const std::exception& e)
    {
        LOGFW("[ImageFetcher] failed to read {}: {}", path, e.what());
        return {};
    }
#else
    return decode_cover_surface(path);
#endif
}
}

std::vector<ImageFetcher::Source> ImageFetcher::_build_sources(
        const Config* config, DbItem* item, Mode mode)
{
    const std::string folder = config && !config->thumbnail_folder.empty()
            ? config->thumbnail_folder
            : "ux0:usagi-pkgj/cover";

    return {
            // 1) HexFlow-Covers box art (PNG) — default source.
            {fmt::format("{}/{}.png", folder, item->titleid),
             fmt::format("{}/{}.png",
                         cover_base_url_for_mode(mode),
                         item->titleid)},
            // 2) PlayStation Store (JPEG) — fallback for titles HexFlow lacks.
            {fmt::format("{}/{}.cover.jpg", folder, item->titleid),
             get_store_image_url(item)},
    };
}

ImageFetcher::ImageFetcher(const Config* config, DbItem* item, Mode mode)
    : _sources(_build_sources(config, item, mode))
{
    ensure_image_folder(config);
    // Download is NOT started here.  get_status() / get_texture() drive
    // _try_submit() every frame until the WorkerPool accepts the task.
}

ImageFetcher::~ImageFetcher()
{
    // The worker thread is owned by the global WorkerPool singleton and
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
// Called every frame (via get_status) until the WorkerPool accepts the task.
void ImageFetcher::_try_submit()
{
    // ── Fast path: is any candidate already cached on disc? ───────────────
    // Checked in priority order so a higher-priority source that shows up
    // later (e.g. after a bulk sync) is preferred over a stale fallback.
    //
    // Only done ONCE (_disk_checked), not on every retry: the pool has a
    // small, fixed slot count, so when many grid cells want covers at once
    // (up to 8 now, 4 columns x 2 rows), most instances still spend several
    // frames retrying here while they wait for a free slot. Nothing else
    // writes to these cache paths mid-run, so a miss now will still be a
    // miss next frame — re-stat'ing (and re-logging, on Vita3K) both
    // candidates every single frame while queued was pure waste.
    if (!_disk_checked)
    {
        _disk_checked = true;
        for (const auto& src : _sources)
        {
            if (pkgi_file_exists(src.path.c_str()))
            {
                _pending_image_path = src.path;
                _upload_pending      = true;
                _submitted           = true;
                return; // status stays Pending until get_texture() builds the texture
            }
        }

        if (_sources.empty())
        {
            _status    = Status::Error;
            _submitted = true;
            return;
        }
    }

    // ── Slow path: submit download to the global WorkerPool ───────────────
    // All data captured by VALUE — the lambda must not reference 'this'.
    // If ImageFetcher is destroyed before the worker finishes, the
    // shared_ptr keeps ImageFetchResult alive until both sides drop it.
    auto result = std::make_shared<ImageFetchResult>();
    const std::vector<Source> sources = _sources;
    const std::string task_id = sources.front().path; // stable per-title id

    if (!WorkerPool::image_workers().try_submit(
                task_id,
                [result, sources]()
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

                    // Try each source in order; a 404/failure falls through
                    // to the next one instead of giving up immediately.
                    for (const auto& src : sources)
                    {
                        // ── Network ─────────────────────────────────────
                        // CurlHttp uses libcurl (TLS 1.2 + ECDHE ciphers).
                        // VitaHttp (sceHttp) lacks elliptic-curve support
                        // and fails on modern HTTPS servers — curl is used.
                        CurlHttp http;
                        try { http.start(src.url, 0); }
                        catch (const std::exception& e)
                        {
                            LOGFW("[ImageFetcher] HTTP start failed for {}: {}",
                                  src.url, e.what());
                            continue;
                        }

                        if (http.get_status() == 404)
                            continue;

                        std::vector<uint8_t> data;
                        data.reserve(32 * 1024);
                        size_t pos        = 0;
                        bool   too_large  = false;
                        bool   read_failed = false;

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
                                      src.url, e.what());
                                read_failed = true;
                                break;
                            }

                            if (n == 0) break;
                            pos += static_cast<size_t>(n);
                            if (pos > ImageFetcher::MAX_SIZE_BYTES)
                                { too_large = true; break; }
                        }

                        if (read_failed || too_large || pos == 0)
                            continue;

                        data.resize(pos);

                        // ── Save ────────────────────────────────────────
                        // Write to .tmp then rename so the file is never
                        // partial.
                        void* f = nullptr;
                        try
                        {
                            const std::string tmp = src.path + ".tmp";
                            f = pkgi_create(tmp);
                            pkgi_write(f, data.data(), data.size());
                            pkgi_close(f); f = nullptr;
                            pkgi_rename(tmp, src.path);
                            done_ok(src.path);
                            return;
                        }
                        catch (const std::exception& e)
                        {
                            if (f) pkgi_close(f);
                            LOGFW("[ImageFetcher] Failed to save {} : {}",
                                  src.path, e.what());
                            continue;
                        }
                    }

                    done_error(); // every source failed
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
            _pending_image_path = std::move(_result->path);
            _upload_pending      = true;
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
    // vita2d_load_*_file must run on the main (render) thread.
    _upload_pending = false;
    const std::string path = std::move(_pending_image_path);

    vita2d_texture* tex = decode_cover_from_path(path);
    if (!tex)
    {
        // Corrupt or unreadable cache file — delete it so the next open
        // triggers a fresh download instead of looping on the same error.
        LOGFW("[ImageFetcher] failed to decode {}, removing", path);
        pkgi_rm(path.c_str());
    }

    _texture = tex;
    _status  = tex ? Status::Ready : Status::Error;
    return tex;
}

// ── take_decoded_cover ──────────────────────────────────────────────────────
std::optional<DecodedCover> ImageFetcher::take_decoded_cover()
{
    get_status();

    if (_cover_taken || !_upload_pending)
        return std::nullopt;

    _upload_pending = false;
    _cover_taken    = true;
    const std::string path = std::move(_pending_image_path);

    DecodedCover cover = decode_cover_pixels_from_path(path);

    if (cover.width == 0 || cover.height == 0)
    {
        LOGFW("[ImageFetcher] failed to decode {}, removing", path);
        pkgi_rm(path.c_str());
        _status = Status::Error;
        return std::nullopt;
    }

    _status = Status::Ready;
    return cover;
}
