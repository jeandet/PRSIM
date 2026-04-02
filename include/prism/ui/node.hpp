#pragma once

#include <prism/core/connection.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace prism::ui {
using namespace prism::core;
using namespace prism::render;


struct WidgetNode;

struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    // Type-erased WidgetNode builder (leaf nodes only)
    std::function<void(WidgetNode&)> build_widget;

    // Type-erased change subscription (leaf nodes only)
    std::function<Connection(std::function<void()>)> on_change;

    // Multiple dependencies for canvas nodes with depends_on()
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;

    // Scroll container metadata (only meaningful when layout_kind == Scroll)
    ScrollBarPolicy scroll_bar_policy = ScrollBarPolicy::Auto;
    ScrollEventPolicy scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;

    // Virtual list metadata (only meaningful when layout_kind == VirtualList)
    std::function<void(WidgetNode&, size_t index)> vlist_bind_row;
    std::function<void(WidgetNode&)> vlist_unbind_row;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_insert;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_remove;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_update;
    size_t vlist_item_count = 0;

    // Table metadata (only meaningful when layout_kind == Table)
    std::shared_ptr<TableState> table_state;

    // Tabs metadata (only meaningful when layout_kind == Tabs)
    std::shared_ptr<TabsState> tabs_state;
};

} // namespace prism::ui
