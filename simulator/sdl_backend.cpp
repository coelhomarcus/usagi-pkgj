// sdl_backend.cpp
// SDL2-based platform backend for the pkgj graphical simulator on Linux.
// Replaces vita.cpp for the PKGI_SIMULATOR build.
//
// Provides:
//   - SDL2 window / renderer initialisation
//   - Keyboard → Vita button mapping
//   - SDL2_ttf text rendering
//   - SDL2_image PNG texture loading
//   - Clip-rect support
//   - Thread helpers via std::thread
//   - Stub implementations for battery, config folder, free-space, etc.

#include "pkgi.hpp"

extern "C"
{
#include "style.h"
}

#include "config.hpp"
#include "file.hpp"
#include "log.hpp"

#include <fmt/format.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <stdarg.h>
#include <stdexcept>
#include <string>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

// ──────────────────────────────────────────────────────────────────────────────
// Globals shared with imgui_sdl.cpp
// ──────────────────────────────────────────────────────────────────────────────
SDL_Renderer* g_sdl_renderer = nullptr;   // exposed for ImGui SDL backend
SDL_Window*   g_sdl_window   = nullptr;

// ──────────────────────────────────────────────────────────────────────────────
// Private statics
// ──────────────────────────────────────────────────────────────────────────────
static TTF_Font* g_font       = nullptr;
static bool      g_clipping   = false;
static SDL_Rect  g_clip_rect  = {0, 0, VITA_WIDTH, VITA_HEIGHT};

static uint32_t g_button_frame_count = 0;
static uint64_t g_time_ms            = 0;
static uint32_t g_current_down       = 0; // buttons currently held

// Dialog / IME state
static std::mutex  g_dialog_mutex;
static bool        g_ime_active    = false;
static bool        g_ime_confirmed = false; // set when user presses Enter
static std::string g_ime_title;
static std::string g_ime_input; // text typed so far

// OK / Cancel button assignment (X = ok, O = cancel, matching western style)
static int g_ok_button     = PKGI_BUTTON_X;
static int g_cancel_button = PKGI_BUTTON_O;

// ──────────────────────────────────────────────────────────────────────────────
// Color helper
// ──────────────────────────────────────────────────────────────────────────────
// pkgj color format is 0xBBGGRR (R in lowest byte).
static void sdl_set_color(SDL_Renderer* r, uint32_t c)
{
    SDL_SetRenderDrawColor(r,
        c & 0xFF,
        (c >> 8) & 0xFF,
        (c >> 16) & 0xFF,
        255);
}

// ──────────────────────────────────────────────────────────────────────────────
// Keyboard → Vita button mapping
// ──────────────────────────────────────────────────────────────────────────────
struct KeyMap { SDL_Keycode key; uint32_t button; };

static constexpr KeyMap KEY_TABLE[] = {
    // D-Pad
    { SDLK_UP,        PKGI_BUTTON_UP    },
    { SDLK_DOWN,      PKGI_BUTTON_DOWN  },
    { SDLK_LEFT,      PKGI_BUTTON_LEFT  },
    { SDLK_RIGHT,     PKGI_BUTTON_RIGHT },
    // WASD (alternative D-Pad)
    { SDLK_w,         PKGI_BUTTON_UP    },
    { SDLK_s,         PKGI_BUTTON_DOWN  },
    { SDLK_a,         PKGI_BUTTON_LEFT  },
    { SDLK_d,         PKGI_BUTTON_RIGHT },
    // Face buttons
    { SDLK_RETURN,    PKGI_BUTTON_X     }, // Enter  = Cross  (confirm)
    { SDLK_KP_ENTER,  PKGI_BUTTON_X     },
    { SDLK_ESCAPE,    PKGI_BUTTON_O     }, // Esc    = Circle (cancel)
    { SDLK_BACKSPACE, PKGI_BUTTON_O     }, // Bksp   = Circle (cancel)
    { SDLK_t,         PKGI_BUTTON_T     }, // T      = Triangle
    { SDLK_F1,        PKGI_BUTTON_T     }, // F1     = Triangle (alt)
    { SDLK_SPACE,     PKGI_BUTTON_S     }, // Space  = Square
    { SDLK_F2,        PKGI_BUTTON_S     },
    // Shoulders
    { SDLK_q,         PKGI_BUTTON_LT    }, // Q  = L-trigger
    { SDLK_e,         PKGI_BUTTON_RT    }, // E  = R-trigger
    { SDLK_PAGEUP,    PKGI_BUTTON_LT    },
    { SDLK_PAGEDOWN,  PKGI_BUTTON_RT    },
    // Select / Start
    { SDLK_TAB,       PKGI_BUTTON_SELECT },
    { SDLK_F10,       PKGI_BUTTON_START  },
};

static uint32_t sdl_key_to_button(SDL_Keycode k)
{
    for (const auto& km : KEY_TABLE)
        if (km.key == k) return km.button;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Font helpers
// ──────────────────────────────────────────────────────────────────────────────
// Preferred font search order (first that exists is used).
// Include Japanese-capable fonts first so the simulator can render JPN text.
static const char* FONT_CANDIDATES[] = {
    "/usr/share/fonts/truetype/noto/NotoSansCJKjp-Regular.otf",
    "/usr/share/fonts/truetype/noto/NotoSansCJKjp-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
    "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.ttf",
    "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
    "/usr/share/fonts/truetype/fonts-japanese-mincho.ttf",
    "/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    nullptr
};

static bool load_font_file(const char* path, int ptsize, TTF_Font** out)
{
    if (access(path, R_OK) != 0)
        return false;
    TTF_Font* font = TTF_OpenFont(path, ptsize);
    if (!font)
        return false;

    printf("[sim] Loaded font: %s\n", path);
    *out = font;
    return true;
}

static bool load_font_via_fc_match(int ptsize, TTF_Font** out)
{
    FILE* pipe = popen("fc-match -s -f '%{file}\\n' :lang=ja", "r");
    if (!pipe)
        return false;

    char path[1024];
    while (fgets(path, sizeof(path), pipe))
    {
        size_t len = strlen(path);
        if (len == 0)
            continue;
        if (path[len - 1] == '\n')
            path[len - 1] = '\0';

        if (load_font_file(path, ptsize, out))
        {
            pclose(pipe);
            return true;
        }
    }

    pclose(pipe);
    return false;
}

static TTF_Font* load_best_font(int ptsize)
{
    TTF_Font* font = nullptr;
    if (load_font_via_fc_match(ptsize, &font))
        return font;

    for (int i = 0; FONT_CANDIDATES[i]; ++i)
    {
        if (load_font_file(FONT_CANDIDATES[i], ptsize, &font))
            return font;
    }

    // fall back to whatever TTF_OpenFont can find
    fprintf(stderr, "[sim] WARNING: no preferred font found; text will be blank\n");
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// pkgi_start / pkgi_end
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_start(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        throw std::runtime_error(
                fmt::format("SDL_Init failed: {}", SDL_GetError()));

    if (TTF_Init() != 0)
        throw std::runtime_error(
                fmt::format("TTF_Init failed: {}", TTF_GetError()));

    int img_flags = IMG_INIT_PNG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags)
        fprintf(stderr, "[sim] WARNING: IMG_Init PNG failed: %s\n",
                IMG_GetError());

    g_sdl_window = SDL_CreateWindow(
            "PKGj Simulator",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            VITA_WIDTH, VITA_HEIGHT,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_sdl_window)
        throw std::runtime_error(
                fmt::format("SDL_CreateWindow failed: {}", SDL_GetError()));

    g_sdl_renderer = SDL_CreateRenderer(
            g_sdl_window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_sdl_renderer)
        throw std::runtime_error(
                fmt::format("SDL_CreateRenderer failed: {}", SDL_GetError()));

    // Scale renderer output to always match 960×544 logical size
    SDL_RenderSetLogicalSize(g_sdl_renderer, VITA_WIDTH, VITA_HEIGHT);

    // Must happen once, before any thread touches libcurl (WorkerPool's
    // cover-fetch threads, VitaHttp's package-download thread) — curl_easy_init()
    // auto-inits lazily otherwise, and libcurl explicitly documents that
    // implicit init as not thread-safe. Mirrors src/vita.cpp's real-Vita init.
    curl_global_init(CURL_GLOBAL_ALL);

    g_font = load_best_font(17);

    g_time_ms = SDL_GetTicks();
}

void pkgi_end(void)
{
    if (g_font)        { TTF_CloseFont(g_font);                g_font = nullptr; }
    if (g_sdl_renderer){ SDL_DestroyRenderer(g_sdl_renderer); g_sdl_renderer = nullptr; }
    if (g_sdl_window)  { SDL_DestroyWindow(g_sdl_window);     g_sdl_window = nullptr; }
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

// ──────────────────────────────────────────────────────────────────────────────
// pkgi_update  — polls SDL events, translates keys, prepares next frame
// ──────────────────────────────────────────────────────────────────────────────
int pkgi_update(pkgi_input* input)
{
    uint32_t just_pressed = 0;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            return 0;

        case SDL_KEYDOWN:
            // IME is active: handle only text-input keys, don't pass to
            // the Vita button mapper so we don't accidentally trigger
            // Cross / cancel etc. while the user is typing.
            if (g_ime_active)
            {
                if (event.key.keysym.sym == SDLK_RETURN ||
                    event.key.keysym.sym == SDLK_KP_ENTER)
                {
                    g_ime_confirmed = true;
                    g_ime_active    = false;
                    SDL_StopTextInput();
                    SDL_SetWindowTitle(g_sdl_window, "PKGj Simulator");
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    g_ime_active = false;
                    g_ime_confirmed = false;
                    g_ime_input.clear();
                    SDL_StopTextInput();
                    SDL_SetWindowTitle(g_sdl_window, "PKGj Simulator");
                }
                else if (event.key.keysym.sym == SDLK_BACKSPACE &&
                         !g_ime_input.empty())
                {
                    // Remove last UTF-8 character
                    g_ime_input.pop_back();
                    while (!g_ime_input.empty() &&
                           (g_ime_input.back() & 0xC0) == 0x80)
                        g_ime_input.pop_back();
                }
                break; // do NOT fall through to button mapper
            }
            if (!event.key.repeat)
            {
                uint32_t btn = sdl_key_to_button(event.key.keysym.sym);
                if (btn)
                {
                    just_pressed  |= btn;
                    g_current_down |= btn;
                }
            }
            break;

        case SDL_KEYUP:
        {
            uint32_t btn = sdl_key_to_button(event.key.keysym.sym);
            if (btn) g_current_down &= ~btn;
            break;
        }

        case SDL_TEXTINPUT:
            if (g_ime_active)
                g_ime_input += event.text.text;
            break;

        default:
            break;
        }
    }

    uint32_t previous = input->down;
    input->down    = g_current_down;
    input->pressed = just_pressed;
    input->active  = just_pressed;

    if (input->down == previous)
    {
        if (g_button_frame_count >= 10)
            input->active = input->down;
        g_button_frame_count++;
    }
    else
    {
        g_button_frame_count = 0;
    }

    // Clear screen at start of frame
    SDL_SetRenderDrawColor(g_sdl_renderer, 30, 30, 30, 255);
    SDL_RenderClear(g_sdl_renderer);
    // Reset clip so full-screen draws work
    SDL_RenderSetClipRect(g_sdl_renderer, nullptr);

    uint64_t now = SDL_GetTicks();
    input->delta = (now - g_time_ms) * 1000; // microseconds
    g_time_ms    = now;

    return 1;
}

// ──────────────────────────────────────────────────────────────────────────────
// pkgi_swap — present the rendered frame
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_swap(void)
{
    SDL_RenderPresent(g_sdl_renderer);
}

// ──────────────────────────────────────────────────────────────────────────────
// Drawing primitives
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_clip_set(int x, int y, int w, int h)
{
    g_clipping = true;
    g_clip_rect = {x, y, w, h};
    SDL_RenderSetClipRect(g_sdl_renderer, &g_clip_rect);
}

void pkgi_clip_remove(void)
{
    g_clipping = false;
    SDL_RenderSetClipRect(g_sdl_renderer, nullptr);
}

void pkgi_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    sdl_set_color(g_sdl_renderer, color);
    const SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(g_sdl_renderer, &r);
}

void pkgi_draw_text(int x, int y, uint32_t color, const char* text)
{
    pkgi_draw_text_scale(x, y, color, text, 1.f);
}

void pkgi_draw_text_scale(int x, int y, uint32_t color, const char* text, float scale)
{
    if (!g_font || !text || !*text)
        return;

    SDL_Color c{
        static_cast<Uint8>(color & 0xFF),
        static_cast<Uint8>((color >> 8) & 0xFF),
        static_cast<Uint8>((color >> 16) & 0xFF),
        static_cast<Uint8>(color >> 24)};
    if (c.a == 0)
        c.a = 255;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(g_font, text, c);
    if (!surf) return;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_sdl_renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);

    // vita.cpp draws at y+20 (font ascent offset) — replicate here
    SDL_Rect dst{
            x,
            y + 2,
            static_cast<int>(tw * scale),
            static_cast<int>(th * scale)};
    SDL_RenderCopy(g_sdl_renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

int pkgi_text_width(const char* text)
{
    if (!g_font || !text || !*text) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(g_font, text, &w, &h);
    return w;
}

int pkgi_text_height(const char* text)
{
    (void)text;
    if (!g_font) return 20;
    return TTF_FontHeight(g_font);
}

// ──────────────────────────────────────────────────────────────────────────────
// Texture / PNG support
// ──────────────────────────────────────────────────────────────────────────────
pkgi_texture pkgi_load_png_raw(const void* data, uint32_t size)
{
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw) return nullptr;
    SDL_Surface* surf = IMG_LoadPNG_RW(rw);
    SDL_RWclose(rw);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_sdl_renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void pkgi_draw_texture(pkgi_texture texture, int x, int y)
{
    if (!texture) return;
    SDL_Texture* tex = static_cast<SDL_Texture*>(texture);
    int w = 0, h = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    const SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(g_sdl_renderer, tex, nullptr, &dst);
}

void pkgi_draw_texture_scaled(pkgi_texture texture, int x, int y, int w, int h)
{
    if (!texture) return;
    SDL_Texture* tex = static_cast<SDL_Texture*>(texture);
    const SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(g_sdl_renderer, tex, nullptr, &dst);
}

// ──────────────────────────────────────────────────────────────────────────────
// ImGui font texture creation (called from sim_main.cpp)
// ──────────────────────────────────────────────────────────────────────────────
SDL_Texture* sim_create_font_texture(const uint32_t* pixels, int w, int h)
{
    SDL_Texture* tex = SDL_CreateTexture(
            g_sdl_renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STATIC,
            w, h);
    if (!tex)
    {
        fprintf(stderr, "[sim] SDL_CreateTexture (font) failed: %s\n",
                SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(tex, nullptr, pixels,
                      static_cast<int>(w * sizeof(uint32_t)));
    return tex;
}

// ──────────────────────────────────────────────────────────────────────────────
// Button assignment
// ──────────────────────────────────────────────────────────────────────────────
int pkgi_ok_button(void)     { return g_ok_button;     }
int pkgi_cancel_button(void) { return g_cancel_button; }

// ──────────────────────────────────────────────────────────────────────────────
// Dialog mutex (coarse lock used by dialog.cpp)
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_dialog_lock(void)   { g_dialog_mutex.lock();   }
void pkgi_dialog_unlock(void) { g_dialog_mutex.unlock(); }

// ──────────────────────────────────────────────────────────────────────────────
// IME / text-input dialog (maps to SDL text input on Linux)
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_dialog_input_text(const char* title, const char* text)
{
    g_ime_title  = title  ? title  : "";
    g_ime_input  = text   ? text   : "";
    g_ime_active = true;
    SDL_StartTextInput();
    // Show title in window title bar as a hint
    std::string wt =
            fmt::format("PKGj Simulator — {} (type, Enter to confirm, Esc to cancel)",
                        g_ime_title);
    SDL_SetWindowTitle(g_sdl_window, wt.c_str());
}

int pkgi_dialog_input_update(void)
{
    // Returns 1 exactly once after the user confirms input with Enter.
    // g_ime_confirmed is set inside pkgi_update's SDL_KEYDOWN handler.
    if (g_ime_confirmed)
    {
        g_ime_confirmed = false;
        return 1;
    }
    return 0;
}

// Called after pkgi_dialog_input_update returns 1 (see SDL KEYDOWN handler).
// We expose a separate function that pkgi_update can call when detecting
// SDLK_RETURN while g_ime_active; here we just provide the query function.
void pkgi_dialog_input_get_text(char* text, uint32_t size)
{
    strncpy(text, g_ime_input.c_str(), size - 1);
    text[size - 1] = '\0';
    SDL_SetWindowTitle(g_sdl_window, "PKGj Simulator");
}

// ──────────────────────────────────────────────────────────────────────────────
// Battery stubs (no battery on PC)
// ──────────────────────────────────────────────────────────────────────────────
int pkgi_battery_present(void)      { return 0; }
int pkgi_bettery_get_level(void)    { return 100; }
int pkgi_battery_is_low(void)       { return 0; }
int pkgi_battery_is_charging(void)  { return 0; }

// ──────────────────────────────────────────────────────────────────────────────
// Filesystem helpers
// ──────────────────────────────────────────────────────────────────────────────
uint64_t pkgi_get_free_space(const char* path)
{
    struct statvfs sv;
    if (statvfs(path[0] ? path : ".", &sv) != 0)
        return 0;
    return (uint64_t)sv.f_bfree * sv.f_bsize;
}

const char* pkgi_get_config_folder(void)
{
    static const char* folder = "usagi-pkgj";
    // Ensure the folder exists
    mkdir(folder, 0755);
    return folder;
}

int pkgi_is_incomplete(const char* partition, const char* contentid)
{
    (void)partition;
    return pkgi_file_exists(
            fmt::format("usagi-pkgj/{}.resume", contentid).c_str());
}

// ──────────────────────────────────────────────────────────────────────────────
// Process lock stubs (no shell on PC)
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_lock_process(void)   {}
void pkgi_unlock_process(void) {}

// ──────────────────────────────────────────────────────────────────────────────
// Threads (std::thread wrappers)
// ──────────────────────────────────────────────────────────────────────────────
void pkgi_start_thread(const char* name, pkgi_thread_entry* start)
{
    (void)name;
    std::thread(start).detach();
}

void pkgi_sleep(uint32_t msec)
{
    SDL_Delay(msec);
}

// ──────────────────────────────────────────────────────────────────────────────
// System version stub
// ──────────────────────────────────────────────────────────────────────────────
std::string pkgi_get_system_version()
{
    return "Linux Simulator";
}

// ──────────────────────────────────────────────────────────────────────────────
// Module presence stub (always "not present" on PC)
// ──────────────────────────────────────────────────────────────────────────────
bool pkgi_is_module_present(const char*)
{
    return false;
}
