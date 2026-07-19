#include "regionflag.hpp"

#include "pkgi.hpp"

// Deliberately NOT in an anonymous namespace — same linkage trap as
// coverplaceholder.cpp: the extern _binary_assets_flags_*_png_start/_end
// symbols pkgi_load_png() declares must match the plain (non-mangled)
// global symbols add_assets emits from raw assembly.
vita2d_texture* pkgi_get_region_flag(GameRegion region)
{
#ifndef PKGI_SIMULATOR
    static vita2d_texture* usa_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(flags_usa));
    static vita2d_texture* eur_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(flags_eur));
    static vita2d_texture* jpn_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(flags_jpn));
    static vita2d_texture* asa_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(flags_asa));

    switch (region)
    {
    case RegionUSA:
        return usa_tex;
    case RegionEUR:
        return eur_tex;
    case RegionJPN:
        return jpn_tex;
    case RegionASA:
        return asa_tex;
    default:
        return nullptr;
    }
#else
    (void)region;
    return nullptr;
#endif
}
