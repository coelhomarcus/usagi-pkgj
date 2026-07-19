#include "coverplaceholder.hpp"

#include "pkgi.hpp"

// Deliberately NOT in an anonymous namespace: the extern
// _binary_assets_covers_vita_*_png_start/_end symbols pkgi_load_png()
// declares must match the plain (non-mangled) global symbols add_assets
// emits from raw assembly. Inside an anonymous namespace they'd get
// internal C++ linkage instead and fail to link (same trap already hit
// once for pkgi.cpp's group-jump helpers and again for this function
// before it was pulled out of gridview.cpp's anonymous namespace).
vita2d_texture* pkgi_get_cover_placeholder(bool loading)
{
#ifndef PKGI_SIMULATOR
    static vita2d_texture* noimage_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_vita_noimage));
    static vita2d_texture* loading_tex = reinterpret_cast<vita2d_texture*>(
            pkgi_load_png(covers_vita_loading));
    return loading ? loading_tex : noimage_tex;
#else
    (void)loading;
    return nullptr;
#endif
}
