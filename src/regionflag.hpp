#pragma once

#include "db.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

// Small flag badges (assets/flags/*.png) used by the list view and the grid
// view to show a title's region at a glance instead of a 3-letter text code.
// Loaded once (embedded asset, see cross.cmake's add_assets) and cached for
// the process lifetime.
//
// Returns null for RegionINT/RegionUnknown (no single flag represents them —
// callers should keep the "INT"/"???" text fallback) and null in the
// simulator, which has no asset-embedding step.
vita2d_texture* pkgi_get_region_flag(GameRegion region);
