// imgui_sdl.cpp
// SDL2-based ImGui render backend for the pkgj Linux simulator.
// Replaces the vita2d/GXM imgui.cpp that is used in Vita builds.
//
// Uses SDL_RenderGeometry (SDL 2.0.18+) to draw ImGui triangles via the
// existing SDL_Renderer managed by sdl_backend.cpp.

#include "imgui.hpp"

#include <imgui.h>
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

// Provided by sdl_backend.cpp
extern SDL_Renderer* g_sdl_renderer;

// ──────────────────────────────────────────────────────────────────────────────
// init_imgui
// ──────────────────────────────────────────────────────────────────────────────
// No-op for the SDL path: ImGui context creation and font-texture upload are
// handled entirely inside sim_main.cpp (analogous to pkgi.cpp's main()).
void init_imgui()
{
    // Nothing to do — ImGui is set up in sim_main.cpp before the render loop.
}

// ──────────────────────────────────────────────────────────────────────────────
// pkgi_imgui_render
// ──────────────────────────────────────────────────────────────────────────────
// Renders an ImGui draw-list using SDL_RenderGeometry.
//
// SDL_RenderGeometry requires SDL 2.0.18 or newer.  On older distributions
// you can downgrade this path to fill each triangle with plain
// SDL_RenderFillRect calls (lossy but functional).
void pkgi_imgui_render(ImDrawData* draw_data)
{
    if (!draw_data || draw_data->CmdListsCount == 0)
        return;

    SDL_Renderer* renderer = g_sdl_renderer;

    // Save SDL renderer state
    SDL_Rect saved_clip;
    bool had_clip = (SDL_RenderIsClipEnabled(renderer) == SDL_TRUE);
    SDL_RenderGetClipRect(renderer, &saved_clip);

    // ImGui display sizing
    const float fb_x = draw_data->DisplayPos.x;
    const float fb_y = draw_data->DisplayPos.y;

    for (int list_idx = 0; list_idx < draw_data->CmdListsCount; ++list_idx)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[list_idx];

        // Build vertex and index buffers in SDL format
        // SDL_Vertex: position (float x,y), color (SDL_Color), tex_coord (float u,v)
        const int vert_count = cmd_list->VtxBuffer.Size;
        const int idx_count  = cmd_list->IdxBuffer.Size;

        // Allocate temporary buffers on the stack if small enough, else heap
        SDL_Vertex* sdl_verts   = new SDL_Vertex[vert_count];
        int*        sdl_indices = new int[idx_count];

        for (int i = 0; i < vert_count; ++i)
        {
            const ImDrawVert& v = cmd_list->VtxBuffer[i];
            sdl_verts[i].position.x = v.pos.x;
            sdl_verts[i].position.y = v.pos.y;
            sdl_verts[i].color.r    = static_cast<Uint8>((v.col >>  0) & 0xFF);
            sdl_verts[i].color.g    = static_cast<Uint8>((v.col >>  8) & 0xFF);
            sdl_verts[i].color.b    = static_cast<Uint8>((v.col >> 16) & 0xFF);
            sdl_verts[i].color.a    = static_cast<Uint8>((v.col >> 24) & 0xFF);
            sdl_verts[i].tex_coord.x = v.uv.x;
            sdl_verts[i].tex_coord.y = v.uv.y;
        }

        for (int i = 0; i < idx_count; ++i)
            sdl_indices[i] = static_cast<int>(cmd_list->IdxBuffer[i]);

        // Execute draw commands
        int idx_offset = 0;
        for (int cmd_idx = 0; cmd_idx < cmd_list->CmdBuffer.Size; ++cmd_idx)
        {
            const ImDrawCmd& pcmd = cmd_list->CmdBuffer[cmd_idx];

            if (pcmd.UserCallback)
            {
                pcmd.UserCallback(cmd_list, &pcmd);
                idx_offset += static_cast<int>(pcmd.ElemCount);
                continue;
            }

            // Scissor / clip rect
            const ImVec4 clip = pcmd.ClipRect;
            SDL_Rect clip_rect{
                static_cast<int>(clip.x - fb_x),
                static_cast<int>(clip.y - fb_y),
                static_cast<int>(clip.z - clip.x),
                static_cast<int>(clip.w - clip.y)
            };
            if (clip_rect.w <= 0 || clip_rect.h <= 0)
            {
                idx_offset += static_cast<int>(pcmd.ElemCount);
                continue;
            }
            SDL_RenderSetClipRect(renderer, &clip_rect);

            SDL_Texture* tex = static_cast<SDL_Texture*>(
                    reinterpret_cast<void*>(pcmd.TextureId));

            SDL_RenderGeometry(
                    renderer,
                    tex,
                    sdl_verts,
                    vert_count,
                    sdl_indices + idx_offset,
                    static_cast<int>(pcmd.ElemCount));

            idx_offset += static_cast<int>(pcmd.ElemCount);
        }

        delete[] sdl_verts;
        delete[] sdl_indices;
    }

    // Restore SDL clip state
    if (had_clip)
        SDL_RenderSetClipRect(renderer, &saved_clip);
    else
        SDL_RenderSetClipRect(renderer, nullptr);
}
