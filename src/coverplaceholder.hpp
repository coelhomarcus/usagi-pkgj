#pragma once

#include "db.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

// Shared "No image" / "Loading" skeleton art (assets/covers/*.png), used
// everywhere a cover is missing or still downloading: the grid and
// GameView's cover panel. The asset set is picked from `mode` to match the
// aspect ratio of that content type's real covers (PS Vita: vertical
// ~250x320, PSP: vertical 180x320, PSX: square 256x256) — every other mode
// falls back to the PS Vita art. Loaded once (embedded asset, see
// cross.cmake's add_assets) and cached for the process lifetime.
//
// Returns null in the simulator, which has no asset-embedding step —
// callers should fall back to a plain rect+text placeholder in that case.
vita2d_texture* pkgi_get_cover_placeholder(bool loading, Mode mode = ModeGames);
