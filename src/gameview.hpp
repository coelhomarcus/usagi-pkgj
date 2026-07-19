#pragma once

#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "downloader.hpp"
#include "imagefetcher.hpp"
#include "install.hpp"
#include "patchinfofetcher.hpp"

#include <memory>

#include <optional>

struct pkgi_input;

class GameView
{
public:
    GameView(
            Mode mode,
            const Config* config,
            Downloader* downloader,
            DbItem* item,
            std::optional<CompPackDatabase::Item> base_comppack,
            std::optional<CompPackDatabase::Item> patch_comppack);

    const DbItem* get_item() const
    {
        return _item;
    }

    void render();
    void update(const pkgi_input& input);
    void refresh();

    // Called by pkgi.cpp instead of close() when the cancel button is
    // pressed. GameView has no nested focus levels to walk back out of
    // anymore, so this always returns false (caller should call close()) —
    // kept as a method so pkgi.cpp's call site doesn't need special-casing.
    bool handle_cancel();

    bool is_closed() const
    {
        return _closed;
    }

    void close()
    {
        _closed = true;
    }

private:
    Mode _mode;
    const Config* _config;
    Downloader* _downloader;

    DbItem* _item;
    std::optional<CompPackDatabase::Item> _base_comppack;
    std::optional<CompPackDatabase::Item> _patch_comppack;

    bool _refood_present{false};
    bool _0syscall6_present{false};
    bool _nopspemudrm_present{false};
    std::string _game_version;
    CompPackVersion _comppack_versions;

    bool _closed{false};

    // Gives the details window focus on the first render. Actions are handled
    // directly by controller shortcuts, not by navigating ImGui buttons.
    bool _request_focus{true};

    std::unique_ptr<PatchInfoFetcher>    _patch_info_fetcher;
    ImageFetcher                         _image_fetcher;

    std::string get_min_system_version();
    bool is_vita_mode() const;
    void printDiagnostic();
    void do_download(PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void start_download_package(
            PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void cancel_download_package();
    void toggle_comppack(bool patch);
    void start_download_comppack(bool patch);
    void cancel_download_comppacks(bool patch);
};
