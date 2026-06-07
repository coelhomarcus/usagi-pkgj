#pragma once

#include <string>
#include <vector>

#include "config.hpp"

// ConfigEditor — full-screen ImGui panel that lets the user view and edit
// every line of config.txt using the Vita native IME keyboard.
//
// Open it by setting config_editor = std::make_unique<ConfigEditor>(config)
// in pkgi.cpp (triggered by MenuResultOpenConfigEditor from the menu).
// Call render() once per frame.  Check is_closed() to know when to destroy it.
// If was_saved() is true after close, call pkgi_load_config() to reload.
class ConfigEditor
{
public:
    explicit ConfigEditor(Config& config);
    ~ConfigEditor() = default;

    void render();

    bool is_closed() const { return _closed; }
    bool was_saved() const { return _saved; }

    // Called from pkgi.cpp before input is zeroed:
    void save_and_close(); // Triangle — save then close
    void close();          // Circle  — discard

private:
    void load();
    void save();

    Config&                  _config;
    std::string              _path;
    std::vector<std::string> _lines;
    int                      _selected{0};
    bool                     _ime_active{false};
    bool                     _closed{false};
    bool                     _saved{false};
    char                     _ime_buf[512]{};
};
