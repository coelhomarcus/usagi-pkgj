#pragma once

#include "pkgi.hpp"

class LogViewer
{
public:
    // input: raw snapshot BEFORE pkgi.cpp zeros it.
    void render(const pkgi_input& input);

    bool is_closed() const { return _closed; }
    void close() { _closed = true; }

private:
    int  _selected{0};
    bool _closed{false};
};