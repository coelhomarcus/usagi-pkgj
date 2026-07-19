#pragma once

#include "db.hpp"

#ifndef PKGI_SIMULATOR
#include <vita2d.h>
#else
struct vita2d_texture;
#endif

// Small flag badges (assets/flags/*.png) used by the list view and the grid
// view to show a title's region at a glance instead of a 3-letter text code.
// Sourced from flagcdn.com (flagpedia.net, free to use) and cropped to fill
// a common 3:2 canvas (flags whose native ratio isn't already 3:2, like the
// US flag's ~1.9:1, lose a sliver off the sides rather than being
// letterboxed) so no per-flag aspect-ratio math is needed at draw time.
// RegionASA uses the Hong Kong flag, since SCE Hong Kong published most
// ASA-region Vita releases. Loaded once (embedded asset, see cross.cmake's
// add_assets) and cached for the process lifetime.
//
// RegionINT/RegionUnknown get a generic "unknown" (assets/flags/unk.png)
// badge instead, since no single flag represents them.
//
// Returns null in the simulator, which has no asset-embedding step —
// callers should fall back to a plain rect+text placeholder in that case.
vita2d_texture* pkgi_get_region_flag(GameRegion region);
