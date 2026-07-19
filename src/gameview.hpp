#pragma once

#include "comppackdb.hpp"
#include "config.hpp"
#include "db.hpp"
#include "descriptionfetcher.hpp"
#include "downloader.hpp"
#include "imagefetcher.hpp"
#include "install.hpp"
#include "patchinfofetcher.hpp"

#include <memory>

#include <optional>

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
    void refresh();

    // Called by pkgi.cpp instead of close() when the cancel button is pressed.
    // Walks up the focus hierarchy one level per press.
    // Returns true  → event consumed (don't close).
    // Returns false → already at top level (caller should call close()).
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

    // ── Focus hierarchy ──────────────────────────────────────────────────────
    // View  → D-pad picks left/right panel; X enters Panel; Circle closes view
    // Panel → ImGui nav inside panel; Circle returns to View
    // SubItem → ImGui nav inside desc/comment scroll; Circle returns to Panel
    enum class FocusLevel  { View, Panel, SubItem };
    enum class FocusPanel  { Left, Right };
    enum class SubItemTarget { Description };
    FocusLevel    _focus_level{FocusLevel::View};
    FocusPanel    _focused_panel{FocusPanel::Right};
    SubItemTarget _subitem_target{SubItemTarget::Description};
    bool          _request_focus{false}; // set true to seize ImGui focus next frame
    // ────────────────────────────────────────────────────────────────────────

    std::unique_ptr<PatchInfoFetcher>    _patch_info_fetcher;
    ImageFetcher                         _image_fetcher;
    std::unique_ptr<DescriptionFetcher>  _description_fetcher; // vita mode only

    std::string get_min_system_version();
    bool is_vita_mode() const;
    void printDiagnostic();
    void do_download(PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void start_download_package(
            PspInstallMode psp_install_mode = PspInstallMode::Auto);
    void cancel_download_package();
    void start_download_comppack(bool patch);
    void cancel_download_comppacks(bool patch);
};
