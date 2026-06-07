// bgdl_stub.cpp
// Stub implementation of bgdl.hpp for the pkgj Linux simulator.
// Background download (LiveArea queue) is not available on PC;
// calls are silently ignored.

#include "bgdl.hpp"

#include <cstdio>

void pkgi_start_bgdl(
        const int type,
        const std::string& title,
        const std::string& url,
        const std::vector<uint8_t>& rif)
{
    (void)type;
    (void)rif;
    fprintf(stderr,
            "[sim] pkgi_start_bgdl: bgdl not supported in simulator "
            "(title=%s, url=%s)\n",
            title.c_str(), url.c_str());
}
