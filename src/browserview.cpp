#include "browserview.hpp"

extern "C"
{
#include "style.h"
}
#include "config.hpp"
#include "pkgi.hpp"

#include <fmt/format.h>

#include <algorithm>

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
        { "PSV Games",  ModeGames,    {}, "", "" },
        { "PSV DLCs",   ModeDlcs,     {}, "", "" },
        { "PSV Themes", ModeThemes,   {}, "", "" },
        { "PSV Demos",  ModeDemos,    {}, "", "" },
        { "PSM Games",  ModePsmGames, {}, "", "" },
        { "PSP Games",  ModePspGames, {}, "", "" },
        { "PSP DLCs",   ModePspDlcs,  {}, "", "" },
        { "PS1 Games",  ModePsxGames, {}, "", "" },
    };

    int custom_number = 1;
    for (const auto& entry : config.custom_entries)
    {
        if (entry.name.empty() || entry.url.empty())
            continue;
        root.push_back({
                fmt::format("Custom {}", custom_number++),
                std::nullopt,
                {},
                "",
                entry.url,
        });
    }

    return root;
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

    if (input.active & PKGI_BUTTON_UP)
    {
        if (_selected > 0)
        {
            --_selected;
            if (_selected < _first)
                _first = _selected;
        }
        else
        {
            _selected = count - 1;
            _first    = count > max_vis ? count - max_vis : 0;
        }
    }

    if (input.active & PKGI_BUTTON_DOWN)
    {
        if (_selected + 1 < count)
        {
            ++_selected;
            if (_selected >= _first + max_vis)
                _first = _selected - max_vis + 1;
        }
        else
        {
            _selected = 0;
            _first    = 0;
        }
    }

    if (input.pressed & pkgi_ok_button())
    {
        const BrowseNode& node = nodes[_selected];
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
        const bool sel         = (i == _selected);
        const BrowseNode& node = nodes[i];

        if (sel)
            pkgi_draw_rect(
                    0, y, VITA_WIDTH,
                    font_h + PKGI_MAIN_ROW_PADDING - 1,
                    PKGI_COLOR_SELECTED_BACKGROUND);

        // ► prefix for nodes that can be entered (have children)
        const char* arrow = node.children.empty()
                          ? "   "
                          : "\xe2\x96\xba  "; // ►
        const std::string label = fmt::format("{}{}", arrow, node.label);

        pkgi_draw_text(
                PKGI_MAIN_COLUMN_PADDING, y,
                sel ? PKGI_COLOR_TEXT_SELECTED : PKGI_COLOR_TEXT,
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

    const HintSegment hint[] = {
        { pkgi_button_str(pkgi_ok_button()), pkgi_button_color(pkgi_ok_button()) },
        { " Select  ", PKGI_COLOR_TEXT_TAIL },
        { pkgi_button_str(pkgi_cancel_button()), pkgi_button_color(pkgi_cancel_button()) },
        { " Back", PKGI_COLOR_TEXT_TAIL },
    };
    draw_hint_segments_centered(
            list_bot + PKGI_MAIN_HLINE_HEIGHT,
            hint,
            PKGI_COUNTOF(hint));
}
