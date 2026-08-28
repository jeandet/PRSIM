#pragma once

#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/delegate.hpp>

#include <any>
#include <memory>
#include <optional>

namespace prism {
namespace app {
namespace detail {
using namespace prism::core;
using namespace prism::ui;

inline ScrollState& ensure_scroll_state(WidgetNode& node) {
    return node.get_or_create<ScrollState>();
}

inline TableState* get_table_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TableState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline VirtualListState* get_vlist_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<VirtualListState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline TabsState* get_tabs_state_ptr(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TabsState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

struct ScrollView {
    DY& offset;
    Height viewport_h;
    Height content_h;
    uint8_t& show_ticks;
    ScrollEventPolicy event_policy;
};

inline std::optional<ScrollView> get_scroll_view(WidgetNode& node) {
    if (auto* ss = std::any_cast<ScrollState>(&node.edit_state))
        return ScrollView{ss->offset_y, ss->viewport_h, ss->content_h, ss->show_ticks, ss->event_policy};
    if (auto* vls = get_vlist_state(node)) {
        Height ch{static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
        return ScrollView{vls->scroll_offset, vls->viewport_h, ch, vls->show_ticks, vls->event_policy};
    }
    if (auto* ts = get_table_state(node)) {
        Height ch{static_cast<float>(ts->row_count()) * ts->row_height.raw()};
        return ScrollView{ts->scroll_y, ts->viewport_h, ch, ts->show_ticks, ts->event_policy};
    }
    return std::nullopt;
}

inline void build_layout(WidgetNode& node, LayoutNode& parent) {
    using LK = LayoutKind;

    if (!node.is_container) {
        if (node.layout_kind == LK::Spacer) {
            LayoutNode spacer;
            spacer.kind = LayoutNode::Kind::Spacer;
            spacer.id = node.id;
            spacer.theme = node.theme;
            parent.children.push_back(std::move(spacer));
        } else if (node.layout_kind == LK::Canvas) {
            LayoutNode canvas;
            canvas.kind = LayoutNode::Kind::Canvas;
            canvas.id = node.id;
            canvas.theme = node.theme;
            canvas.draws = node.draws;
            canvas.overlay_draws = node.overlay_draws;
            canvas.canvas_min_width = node.canvas_min_width;
            canvas.canvas_min_height = node.canvas_min_height;
            canvas.widget = &node;
            parent.children.push_back(std::move(canvas));
        } else if (node.layout_kind == LK::Handle) {
            LayoutNode handle;
            handle.kind = LayoutNode::Kind::Handle;
            handle.id = node.id;
            handle.theme = node.theme;
            handle.draws = node.draws;
            handle.overlay_draws = node.overlay_draws;
            handle.widget = &node;
            parent.children.push_back(std::move(handle));
        } else {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = node.id;
            leaf.theme = node.theme;
            leaf.draws = node.draws;
            leaf.overlay_draws = node.overlay_draws;
            leaf.hint.expand = node.expand;
            leaf.hint.expand_axis = node.expand_axis;
            leaf.widget = &node;
            parent.children.push_back(std::move(leaf));
        }
    } else if (node.layout_kind == LK::Scroll) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Scroll;
        container.id = node.id;
        container.theme = node.theme;
        if (auto* ss = std::any_cast<ScrollState>(&node.edit_state))
            container.scroll_offset = ss->offset_y;
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::VirtualList) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::VirtualList;
        container.id = node.id;
        container.theme = node.theme;
        if (auto* vls = get_vlist_state(node)) {
            container.scroll_offset = vls->scroll_offset;
            container.scroll_content_h = Height{
                static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
            container.vlist_visible_start = vls->visible_start.raw();
            container.vlist_item_height = vls->item_height;
        }
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Table) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Table;
        container.id = node.id;
        container.theme = node.theme;
        if (auto* ts = get_table_state(node)) {
            container.scroll_offset = ts->scroll_y;
            container.scroll_content_h = Height{
                static_cast<float>(ts->row_count()) * ts->row_height.raw()};
            container.vlist_visible_start = ts->visible_start.raw();
            container.vlist_item_height = ts->row_height;
            container.table_column_count = ts->column_count;
            container.table_header_h = ts->row_height;
            container.table_scroll_x = ts->scroll_x;
        }
        container.overlay_draws = node.overlay_draws;
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Tabs) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Tabs;
        container.id = node.id;
        container.theme = node.theme;
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
        LayoutNode container;
        container.kind = (node.layout_kind == LK::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        container.id = node.id;
        container.theme = node.theme;
        if (auto* ss = std::any_cast<SplitState>(&node.edit_state); ss && ss->engaged)
            container.split_sizes = ss->pane_sizes;
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else {
        for (auto& c : node.children)
            build_layout(c, parent);
    }
}

} // namespace detail
} // namespace app
} // namespace prism
