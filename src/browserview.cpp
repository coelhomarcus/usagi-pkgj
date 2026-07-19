#include "browserview.hpp"

extern "C"
{
#include "style.h"
}
#include "config.hpp"
#include "pkgi.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <utility>

// ---------------------------------------------------------------------------
// Build the static category tree.
//
// ── How to add the future "group by initial" layer ──────────────────────────
// Replace any leaf node (mode set, children empty) with an internal node
// (mode absent, children non-empty) whose children are leaves that each carry
// both `mode` and a non-empty `group_filter`.  The onModeSelected callback
// will receive the group_filter and can apply it as a prefix search to the
// loaded game list.
//
// Example — expanding "PlayStation Vita > Games" into lettered groups:
//
//   { "PlayStation Vita", std::nullopt, {
//       { "Games", std::nullopt, {              // <── now internal
//           { "#",   ModeGames, {}, "0123456789" },
//           { "A-D", ModeGames, {}, "ABCD" },
//           { "E-H", ModeGames, {}, "EFGH" },
//           { "I-L", ModeGames, {}, "IJKL" },
//           { "M-P", ModeGames, {}, "MNOP" },
//           { "Q-T", ModeGames, {}, "QRST" },
//           { "U-Z", ModeGames, {}, "UVWXYZ" },
//       }, "" },
//       ...
//   }, "" },
// ---------------------------------------------------------------------------
static std::vector<BrowseNode> build_tree(const Config& config)
{
    auto root = std::vector<BrowseNode>{
        { "PlayStation Vita",   std::nullopt, {}, "", "" },
        { "  Games",            ModeGames,    {}, "", "" },
        { "  DLCs",             ModeDlcs,     {}, "", "" },
        { "  Themes",           ModeThemes,   {}, "", "" },
        { "  Demos",            ModeDemos,    {}, "", "" },
        { "PlayStation Portable", std::nullopt, {}, "", "" },
        { "  Games",            ModePspGames, {}, "", "" },
        { "  DLCs",             ModePspDlcs,  {}, "", "" },
        { "PlayStation 1",      std::nullopt, {}, "", "" },
        { "  Games",            ModePsxGames, {}, "", "" },
        { "PlayStation Mobile", std::nullopt, {}, "", "" },
        { "  Games",            ModePsmGames, {}, "", "" },
    };

    bool custom_heading_added = false;
    for (const auto& entry : config.custom_entries)
    {
        if (entry.name.empty() || entry.url.empty())
            continue;
        if (!custom_heading_added)
        {
            root.push_back({ "Custom Lists", std::nullopt, {}, "", "" });
            custom_heading_added = true;
        }
        root.push_back({
                "  " + entry.name,
                std::nullopt,
                {},
                "",
                entry.url,
        });
    }

    return root;
}

static bool is_selectable_node(const BrowseNode& node)
{
    return node.mode.has_value() || !node.children.empty() ||
            !node.custom_tsv_url.empty();
}

static size_t first_selectable_index(const std::vector<BrowseNode>& nodes)
{
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (is_selectable_node(nodes[i]))
            return i;
    }
    return nodes.size();
}

static size_t next_selectable_index(
        const std::vector<BrowseNode>& nodes,
        size_t current)
{
    for (size_t offset = 1; offset <= nodes.size(); ++offset)
    {
        const size_t index = (current + offset) % nodes.size();
        if (is_selectable_node(nodes[index]))
            return index;
    }
    return nodes.size();
}

static size_t previous_selectable_index(
        const std::vector<BrowseNode>& nodes,
        size_t current)
{
    for (size_t offset = 1; offset <= nodes.size(); ++offset)
    {
        const size_t index = (current + nodes.size() - offset) % nodes.size();
        if (is_selectable_node(nodes[index]))
            return index;
    }
    return nodes.size();
}

struct HintSegment
{
    const char* text;
    uint32_t color;
};

static int hint_width(const HintSegment* segments, size_t count)
{
    int width = 0;
    for (size_t i = 0; i < count; ++i)
        width += pkgi_text_width(segments[i].text);
    return width;
}

static void draw_hint_segments_centered(
        int y,
        const HintSegment* segments,
        size_t count)
{
    int x = (VITA_WIDTH - hint_width(segments, count)) / 2;
    for (size_t i = 0; i < count; ++i)
    {
        pkgi_draw_text(x, y, segments[i].color, segments[i].text);
        x += pkgi_text_width(segments[i].text);
    }
}

// ---------------------------------------------------------------------------

BrowseView::BrowseView(
        const Config& config,
        std::function<void(const BrowseNode&)> onNodeSelected)
    : _root(build_tree(config))
    , _onNodeSelected(std::move(onNodeSelected))
{
    _selected = first_selectable_index(_root);
    if (_selected >= _root.size())
        _selected = 0;
}

const std::vector<BrowseNode>& BrowseView::current_nodes() const
{
    const std::vector<BrowseNode>* nodes = &_root;
    for (const auto& lvl : _stack)
        nodes = &(*nodes)[lvl.selected].children;
    return *nodes;
}

void BrowseView::enter_child()
{
    _stack.push_back({ _selected, _first });
    _selected = 0;
    _first    = 0;

    const auto& nodes = current_nodes();
    _selected = first_selectable_index(nodes);
    if (_selected >= nodes.size())
        _selected = 0;
}

bool BrowseView::go_back()
{
    if (_stack.empty())
        return false;
    const auto prev = _stack.back();
    _stack.pop_back();
    _selected = prev.selected;
    _first    = prev.first;
    return true;
}

bool BrowseView::update(const pkgi_input& input)
{
    const auto& nodes  = current_nodes();
    const size_t count = nodes.size();

    const int font_h = pkgi_text_height("M");
    const size_t max_vis = static_cast<size_t>(std::max(
            1,
            (VITA_HEIGHT - 2 * (font_h + PKGI_MAIN_HLINE_EXTRA)) /
                    (font_h + PKGI_MAIN_ROW_PADDING)));

    if (count == 0)
        return true;

    if (_selected >= count || !is_selectable_node(nodes[_selected]))
    {
        _selected = first_selectable_index(nodes);
        if (_selected >= count)
            return true;
    }

    if (input.active & PKGI_BUTTON_UP)
    {
        _selected = previous_selectable_index(nodes, _selected);
        if (_selected >= count)
            return true;
        if (_selected < _first)
            _first = _selected;
        else if (_selected >= _first + max_vis)
            _first = _selected >= max_vis ? _selected - max_vis + 1 : 0;
    }

    if (input.active & PKGI_BUTTON_DOWN)
    {
        _selected = next_selectable_index(nodes, _selected);
        if (_selected >= count)
            return true;
        if (_selected < _first)
            _first = _selected == first_selectable_index(nodes) ? 0 : _selected;
        else if (_selected >= _first + max_vis)
            _first = _selected - max_vis + 1;
    }

    if (input.pressed & pkgi_ok_button())
    {
        const BrowseNode& node = nodes[_selected];
        if (!is_selectable_node(node))
            return true;
        if (!node.children.empty())
            enter_child();
        else
            _onNodeSelected(node);
        return true;
    }

    if (input.pressed & pkgi_cancel_button())
        return go_back();  // false at root → caller transitions away

    return true;
}

void BrowseView::render() const
{
    const auto& nodes  = current_nodes();
    const size_t count = nodes.size();
    const int font_h   = pkgi_text_height("M");

    // ── Breadcrumb header ────────────────────────────────────────────────────
    std::string breadcrumb = "Home";
    {
        const std::vector<BrowseNode>* cur = &_root;
        bool is_first = true;
        for (const auto& lvl : _stack)
        {
            if (is_first) { breadcrumb.clear(); is_first = false; }
            else          breadcrumb += " > ";
            breadcrumb += (*cur)[lvl.selected].label;
            cur = &(*cur)[lvl.selected].children;
        }
    }

    const std::string header = fmt::format("Browse: {}", breadcrumb);
    pkgi_draw_text(
            (VITA_WIDTH - pkgi_text_width(header.c_str())) / 2,
            0,
            PKGI_COLOR_TEXT_HEAD,
            header.c_str());

    pkgi_draw_rect(
            0, font_h, VITA_WIDTH, PKGI_MAIN_HLINE_HEIGHT, PKGI_COLOR_HLINE);

    // ── Compute list bounds (leave room for footer hint) ─────────────────────
    const int hint_area = font_h + PKGI_MAIN_HLINE_EXTRA;
    const int list_top  = font_h + PKGI_MAIN_HLINE_EXTRA;
    const int list_bot  = VITA_HEIGHT - hint_area;
    const size_t max_vis = static_cast<size_t>(std::max(
            1, (list_bot - list_top) / (font_h + PKGI_MAIN_ROW_PADDING)));

    // ── Item list ─────────────────────────────────────────────────────────────
    int y = list_top;
    for (size_t i = _first; i < count && i < _first + max_vis; ++i)
    {
        const bool selectable  = is_selectable_node(nodes[i]);
        const bool sel         = selectable && (i == _selected);
        const BrowseNode& node = nodes[i];

        if (sel)
            pkgi_draw_rect(
                    0, y, VITA_WIDTH,
                    font_h + PKGI_MAIN_ROW_PADDING - 1,
                    PKGI_COLOR_SELECTED_BACKGROUND);

        std::string label = node.label;
        if (!node.children.empty())
            label = fmt::format("\xe2\x96\xba  {}", node.label); // ►

        const uint32_t color =
                selectable
                        ? (sel ? PKGI_COLOR_TEXT_SELECTED : PKGI_COLOR_TEXT)
                        : PKGI_COLOR_TEXT_SECTION;

        pkgi_draw_text(
                PKGI_MAIN_COLUMN_PADDING, y,
                color,
                label.c_str());

        y += font_h + PKGI_MAIN_ROW_PADDING;
    }

    // ── Scroll bar ───────────────────────────────────────────────────────────
    if (count > max_vis)
    {
        const int avail = list_bot - list_top;
        const int bar_h = std::max(
                PKGI_MAIN_SCROLL_MIN_HEIGHT,
                static_cast<int>(max_vis * avail / count));
        const int bar_y = static_cast<int>(
                _first * static_cast<size_t>(avail - bar_h) /
                (count - max_vis));

        pkgi_draw_rect(
                VITA_WIDTH - PKGI_MAIN_SCROLL_WIDTH - 1,
                list_top + bar_y,
                PKGI_MAIN_SCROLL_WIDTH,
                bar_h,
                PKGI_COLOR_SCROLL_BAR);
    }

    // ── Footer hint ──────────────────────────────────────────────────────────
    pkgi_draw_rect(
            0, list_bot, VITA_WIDTH, PKGI_MAIN_HLINE_HEIGHT, PKGI_COLOR_HLINE);

    const HintSegment root_hint[] = {
        { pkgi_button_str(pkgi_ok_button()), pkgi_button_color(pkgi_ok_button()) },
        { " Select", PKGI_COLOR_TEXT_TAIL },
    };
    const HintSegment child_hint[] = {
        { pkgi_button_str(pkgi_ok_button()), pkgi_button_color(pkgi_ok_button()) },
        { " Select  ", PKGI_COLOR_TEXT_TAIL },
        { pkgi_button_str(pkgi_cancel_button()), pkgi_button_color(pkgi_cancel_button()) },
        { " Back", PKGI_COLOR_TEXT_TAIL },
    };

    const HintSegment* hint = _stack.empty() ? root_hint : child_hint;
    const size_t hint_count =
            _stack.empty() ? PKGI_COUNTOF(root_hint) : PKGI_COUNTOF(child_hint);
    draw_hint_segments_centered(list_bot + PKGI_MAIN_HLINE_HEIGHT, hint, hint_count);
}
