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
};

// Draws and drives input for the ModeGames cover-art grid view: an
// alternative to pkgi_do_main's plain-text list. Only ever called for
// ModeGames.
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
        int avail_height);

// Drops every cached cover texture. Call when leaving ModeGames or when
// grid view is toggled off, so textures aren't held in VRAM by a screen
// that is no longer shown (sync() would eventually do this too, but only
// on the next call, which may never come once the screen is inactive).
void pkgi_grid_deactivate();
