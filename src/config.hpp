#pragma once

#include "db.hpp"

#include <array>
#include <string>

struct CustomConfigEntry
{
    std::string name;
    std::string url;
};

typedef struct Config
{
    DbSort sort;
    DbSortOrder order;
    uint32_t filter;
    int no_version_check;
    std::string install_psp_psx_location;

    std::string games_url;
    std::string dlcs_url;
    std::string demos_url;
    std::string themes_url;
    std::string psm_games_url;
    std::string psx_games_url;
    std::string psp_games_url;
    std::string psp_dlcs_url;

    std::string comppack_url;

    // Image panel settings
    // thumbnail_url    : optional base URL for custom images fetched as
    //                    {thumbnail_url}/{titleid}.jpg. Overrides the
    //                    default source entirely (no PSN fallback).
    //                    Leave empty to use the default: vertical box art
    //                    from the HexFlow-Covers project, falling back to
    //                    the PS Store cover for titles it doesn't have.
    // thumbnail_folder : local directory where images are stored/cached.
    //                    Leave empty to use the default: ux0:pkgj/cover
    // thumbnail_size   : panel size preset — 0=off, 1=small, 2=medium (default), 3=large
    std::string thumbnail_url;
    std::string thumbnail_folder;
    int thumbnail_size{2};

    // Display settings
    // grid_view : when true, ModeGames renders as a cover-art grid instead
    //             of the plain-text list. Other modes always use the list.
    bool grid_view{false};

    std::array<CustomConfigEntry, 5> custom_entries;
} Config;

Config pkgi_load_config();
void pkgi_save_config(const Config& config);
