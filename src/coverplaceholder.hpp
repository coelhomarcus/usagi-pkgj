#pragma once

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

// Shared "No image" / "Loading" skeleton art (assets/covers/vita_*.png),
// used everywhere a cover is missing or still downloading: the grid and
// GameView's cover panel. Loaded once (embedded asset, see cross.cmake's
// add_assets) and cached for the process lifetime.
//
// Returns null in the simulator, which has no asset-embedding step —
// callers should fall back to a plain rect+text placeholder in that case.
vita2d_texture* pkgi_get_cover_placeholder(bool loading);
