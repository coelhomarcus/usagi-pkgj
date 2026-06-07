// install_stub.cpp
// Stub implementations of install.hpp for the pkgj Linux simulator.
// Installation and content-presence detection are not available on PC;
// all queries return "not installed" and install operations are no-ops.

#include "install.hpp"

#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> pkgi_get_installed_games()
{
    return {};
}

std::vector<std::string> pkgi_get_installed_themes()
{
    return {};
}

std::string pkgi_get_game_version(const std::string& titleid)
{
    (void)titleid;
    return "";
}

CompPackVersion pkgi_get_comppack_versions(const std::string& titleid)
{
    (void)titleid;
    return {false, "", ""};
}

bool pkgi_dlc_is_installed(const char* content)
{
    (void)content;
    return false;
}

bool pkgi_psm_is_installed(const char* titleid)
{
    (void)titleid;
    return false;
}

bool pkgi_psp_is_installed(const char* psppartition, const char* content)
{
    (void)psppartition;
    (void)content;
    return false;
}

bool pkgi_psx_is_installed(const char* psppartition, const char* content)
{
    (void)psppartition;
    (void)content;
    return false;
}

void pkgi_install(const char* contentid)
{
    fprintf(stderr, "[sim] pkgi_install(%s): not supported in simulator\n",
            contentid);
}

void pkgi_install_update(const std::string& titleid)
{
    fprintf(stderr, "[sim] pkgi_install_update(%s): not supported\n",
            titleid.c_str());
}

void pkgi_install_comppack(
        const std::string& titleid, bool patch, const std::string& version)
{
    fprintf(stderr,
            "[sim] pkgi_install_comppack(%s, patch=%d, ver=%s): not supported\n",
            titleid.c_str(), patch, version.c_str());
}

void pkgi_install_psmgame(const char* contentid)
{
    fprintf(stderr, "[sim] pkgi_install_psmgame(%s): not supported\n",
            contentid);
}

void pkgi_install_pspgame(const char* partition, const char* contentid)
{
    fprintf(stderr, "[sim] pkgi_install_pspgame(%s, %s): not supported\n",
            partition, contentid);
}

void pkgi_install_pspgame_as_iso(const char* partition, const char* contentid)
{
    fprintf(stderr,
            "[sim] pkgi_install_pspgame_as_iso(%s, %s): not supported\n",
            partition, contentid);
}

void pkgi_install_pspdlc(const char* partition, const char* contentid)
{
    fprintf(stderr, "[sim] pkgi_install_pspdlc(%s, %s): not supported\n",
            partition, contentid);
}
