#pragma once

#include "config.hpp"
#include "db.hpp"
#include "pkgi.hpp"

struct GridResult
{
    // True when OK was pressed on a valid item this frame. The caller
    // (pkgi.cpp) is responsible for actually opening GameView, since that
    // needs access to state (gameview, comppack DBs) that is private to
    // pkgi.cpp's translation unit.
    bool item_activated = false;

    // True when Triangle was pressed this frame. The caller (pkgi.cpp) is
    // responsible for actually starting the menu (pkgi_menu_start), since
    // that needs config_temp/search_active, private to pkgi.cpp's TU.
    bool open_menu_requested = false;
};

// Draws and drives input for the cover-art grid view: an alternative to
// pkgi_do_main's plain-text list. Supported for ModeGames, ModePspGames and
// ModePsxGames — mode picks both the HexFlow cover folder (ImageFetcher)
// and the placeholder art (pkgi_get_cover_placeholder), since PS Vita/PSP
// covers are vertical and PSX covers are square.
//
// first_item / selected_item are the SAME state pkgi_do_main uses, passed
// by reference, so toggling between list and grid mid-session keeps the
// user's position. In grid mode, first_item is always a multiple of the
// column count (index of the top-left visible cell).
//
// input == nullptr means input is inhibited this frame (a dialog or the
// options menu is open) — the grid still renders, just doesn't navigate.
GridResult pkgi_do_main_grid(
        TitleDatabase& db,
        const Config& config,
        pkgi_input* input,
        uint32_t& first_item,
        uint32_t& selected_item,
        int font_height,
        int avail_height,
        Mode mode);

// Retires every cached cover texture — actual destruction happens later,
// gradually, via pkgi_grid_tick (see GridImageCache's class comment in
// gridview.cpp for why: freeing a vita2d texture too soon after its last
// use has been observed to crash Vita3K). Call when leaving ModeGames or
// when grid view is toggled off, so a screen that's no longer shown
// doesn't keep holding its textures forever (sync() would eventually
// retire them too, but only on its next call, which may never come once
// the grid is inactive).
void pkgi_grid_deactivate();

// Call every frame, regardless of whether the grid is the active renderer:
// ages the retirement queue and destroys whatever has cooled down long
// enough, a little at a time.
void pkgi_grid_tick();
