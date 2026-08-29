#pragma once

#include <prism/app/widget_tree_layout.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

inline std::size_t count_leaves(const WidgetNode& node) {
    if (!node.is_container)
        return node.layout_kind == LayoutKind::Spacer ? 0 : 1;
    std::size_t n = 0;
    for (auto& c : node.children) n += count_leaves(c);
    return n;
}

inline bool check_dirty(const WidgetNode& node) {
    if (node.dirty) return true;
    for (auto& c : node.children)
        if (check_dirty(c)) return true;
    return false;
}

inline std::size_t count_dirty(const WidgetNode& node) {
    std::size_t n = node.dirty ? 1 : 0;
    for (auto& c : node.children) n += count_dirty(c);
    return n;
}

// VirtualList/Table viewport heights, keyed by widget id in a stable tree-order walk --
// used by build_snapshot() to detect whether a materialization pass actually needs
// redoing (see its comment), rather than re-running on any unrelated dirty flag.
inline void collect_viewport_heights(WidgetNode& node, std::vector<std::pair<WidgetId, Height>>& out) {
    if (node.layout_kind == LayoutKind::VirtualList) {
        if (auto* vls = get_vlist_state(node)) out.push_back({node.id, vls->viewport_h});
    } else if (node.layout_kind == LayoutKind::Table) {
        if (auto* ts = get_table_state(node)) out.push_back({node.id, ts->viewport_h});
    }
    for (auto& c : node.children) collect_viewport_heights(c, out);
}

inline void clear_dirty_impl(WidgetNode& node) {
    node.dirty = false;
    for (auto& c : node.children) clear_dirty_impl(c);
}

inline void close_overlays_impl(WidgetNode& node) {
    // Table/Tabs nodes use overlay_draws for their header, not for dropdown overlays
    if (!node.overlay_draws.empty()
        && node.layout_kind != LayoutKind::Table
        && node.layout_kind != LayoutKind::Tabs) {
        node.overlay_draws.clear();
        node.edit_state.reset();
        node.dirty = true;
    }
    for (auto& c : node.children) close_overlays_impl(c);
}

inline void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
    if (!node.is_container) {
        if (node.layout_kind != LayoutKind::Spacer)
            ids.push_back(node.id);
        return;
    }
    for (auto& c : node.children) collect_leaf_ids(c, ids);
}

inline void refresh_dirty(WidgetNode& node) {
    if (node.dirty && node.record)
        node.record(node);
    for (auto& c : node.children)
        refresh_dirty(c);
}

} // namespace prism::app::widget_detail
