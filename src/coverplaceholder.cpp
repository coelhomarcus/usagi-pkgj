#include "coverplaceholder.hpp"

#include "pkgi.hpp"

// Deliberately NOT in an anonymous namespace: the extern
// _binary_assets_covers_vita_*_png_start/_end symbols pkgi_load_png()
// declares must match the plain (non-mangled) global symbols add_assets
// emits from raw assembly. Inside an anonymous namespace they'd get
// internal C++ linkage instead and fail to link (same trap already hit
// once for pkgi.cpp's group-jump helpers and again for this function
// before it was pulled out of gridview.cpp's anonymous namespace).
vita2d_texture* pkgi_get_cover_placeholder(bool loading, Mode mode)
{
#ifndef PKGI_SIMULATOR
    static vita2d_texture* vita_noimage = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_vita_noimage));
    static vita2d_texture* vita_loading = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_vita_loading));
    static vita2d_texture* psp_noimage = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_psp_noimage));
    static vita2d_texture* psp_loading = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_psp_loading));
    static vita2d_texture* ps1_noimage = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_ps1_noimage));
    static vita2d_texture* ps1_loading = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_ps1_loading));

    switch (mode)
    {
    case ModePspGames:
        return loading ? psp_loading : psp_noimage;
    case ModePsxGames:
        return loading ? ps1_loading : ps1_noimage;
    default:
        return loading ? vita_loading : vita_noimage;
    }
#else
    (void)loading;
    (void)mode;
    return nullptr;
#endif
}
