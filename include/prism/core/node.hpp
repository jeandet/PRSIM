#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace prism {

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
};

} // namespace prism
