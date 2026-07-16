#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/types.hpp>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace prism::debug {
using namespace prism::core;
using namespace prism::ui;
using namespace prism::app;

struct NodeRow {
    WidgetId id = 0;
    std::string name;
    std::string layout_kind_name;
    int depth = 0;
    Rect rect;
    bool dirty = false;
    bool hovered = false;
    bool focused = false;
    bool pressed = false;
    bool has_children = false;
    bool expanded = false;
};

inline std::string_view layout_kind_name(LayoutKind k) {
    switch (k) {
        case LayoutKind::Default:     return "Default";
        case LayoutKind::Row:         return "Row";
        case LayoutKind::Column:      return "Column";
        case LayoutKind::Spacer:      return "Spacer";
        case LayoutKind::Canvas:      return "Canvas";
        case LayoutKind::Scroll:      return "Scroll";
        case LayoutKind::VirtualList: return "VirtualList";
        case LayoutKind::Table:       return "Table";
        case LayoutKind::Tabs:        return "Tabs";
    }
    return "?";
}

namespace detail {

inline void flatten_node(const WidgetNode& node, int depth, WidgetId hovered_id,
                         const std::set<WidgetId>& expanded, std::vector<NodeRow>& out) {
    NodeRow row;
    row.id = node.id;
    row.layout_kind_name = std::string(layout_kind_name(node.layout_kind));
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    row.name = node.debug_name.empty() ? row.layout_kind_name : node.debug_name;
#else
    row.name = row.layout_kind_name;
#endif
    row.depth = depth;
    row.rect = Rect{Point{X{0}, Y{0}}, node.canvas_bounds.extent};
    row.dirty = node.dirty;
    row.hovered = (node.id == hovered_id);
    row.focused = node.visual_state.focused;
    row.pressed = node.visual_state.pressed;
    row.has_children = !node.children.empty();
    row.expanded = expanded.contains(node.id);
    out.push_back(row);

    if (!node.children.empty() && expanded.contains(node.id)) {
        for (auto& child : node.children)
            flatten_node(child, depth + 1, hovered_id, expanded, out);
    }
}

} // namespace detail

inline std::vector<NodeRow> flatten_tree(const WidgetTree& tree, const std::set<WidgetId>& expanded) {
    std::vector<NodeRow> rows;
    detail::flatten_node(tree.root(), 0, tree.hovered_id(), expanded, rows);
    return rows;
}

} // namespace prism::debug
