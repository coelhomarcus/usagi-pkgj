#pragma once

#include "db.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

typedef struct Config Config;
struct pkgi_input;

// ---------------------------------------------------------------------------
// BrowseNode — one entry in the category hierarchy
//
// Intended tree layout (layers):
//   Layer 0  Family      PlayStation Vita, PSP, PS1, PSM
//   Layer 1  Content     Games, DLCs, Demos, Themes …
//   Layer 2  (future)    Groups by initial — Numbers, A-D, E-G, H-K …
//   Layer 3  (future)    game list via pkgi_set_mode + group_filter applied
//
// A node is a LEAF when `mode` has a value and `children` is empty.
// An internal node has `children` non-empty and no `mode`.
//
// To add the future "group by initial" layer, replace a leaf node that
// currently has `mode` set with an internal node that has child leaves.
// Each child leaf carries both `mode` and a non-empty `group_filter` that
// the onModeSelected callback will use to filter the game list.
// ---------------------------------------------------------------------------
struct BrowseNode
{
    std::string             label;
    std::optional<Mode>     mode;       // leaf: the Mode this entry activates
    std::vector<BrowseNode> children;   // non-empty → internal node

    // Future "group by initial" use: when this leaf node is confirmed, this
    // value is forwarded to onModeSelected so the game-list layer can apply
    // it as a prefix filter (e.g. "A-D" to show titles starting A through D).
    // Empty means no additional filter.
    std::string group_filter;

    // Custom lists come from config.txt and point directly to a TSV file.
    // They do not use a built-in Mode and therefore need a custom handler.
    std::string custom_tsv_url;
};

// ---------------------------------------------------------------------------
// BrowseView — renders the current tree level and handles controller input.
//
// Usage (each frame):
//   if (!browse_view->update(input)) { /* user backed out at root */ }
//   browse_view->render();
//
// onModeSelected receives the chosen Mode and an optional group_filter string.
// ---------------------------------------------------------------------------
class BrowseView
{
public:
    explicit BrowseView(
            const Config& config,
            std::function<void(const BrowseNode&)> onNodeSelected);

    // Process one frame of input.
    // Returns false when the user presses Back at the root level.
    bool update(const pkgi_input& input);

    void render() const;

private:
    std::vector<BrowseNode> _root;

    // Stack tracking the path taken so far (enables back-navigation +
    // breadcrumb rendering).  One entry per level entered.
    struct LevelState { size_t selected; size_t first; };
    std::vector<LevelState> _stack;

    size_t _selected = 0;
    size_t _first    = 0;

    std::function<void(const BrowseNode&)> _onNodeSelected;

    const std::vector<BrowseNode>& current_nodes() const;
    void enter_child();
    bool go_back();  // returns false if already at root
};
