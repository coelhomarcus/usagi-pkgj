#pragma once

#include "config.hpp"
#include "db.hpp"
#include "imagefetcher.hpp"
#include "pkgi.hpp"

#include <memory>
#include <vector>

// Optional, user-triggered bulk cover sync: walks a fixed list of games and
// fetches every cover not already cached locally, one at a time — reusing
// the exact same ImageFetcher/WorkerSlot pipeline the grid view uses for
// on-screen cells, just driven over the whole list instead of a small
// visible window. Meant for players who'd rather pay one longer up-front
// sync than have covers pop in while scrolling the grid.
//
// Already-cached covers resolve almost instantly (ImageFetcher's fast path
// checks disk before network), so re-running this after a partial/canceled
// run only fetches what's still missing.
class CoverSyncView
{
public:
    CoverSyncView(const Config* config, std::vector<DbItem*> items);

    // input: raw snapshot BEFORE pkgi.cpp zeros it.
    void render(const pkgi_input& input);

    bool is_closed() const { return _closed; }
    void close() { _closed = true; }

private:
    const Config*        _config;
    std::vector<DbItem*> _items;
    size_t                _index{0};
    size_t                _ok{0};
    size_t                _failed{0};
    std::unique_ptr<ImageFetcher> _current;
    bool                  _closed{false};
};
