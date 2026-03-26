#pragma once

#include <prism/core/draw_list.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace prism {

using WidgetId = uint64_t;

struct SceneSnapshot {
    uint64_t version = 0;
    std::vector<std::pair<WidgetId, Rect>> geometry;
    std::vector<DrawList> draw_lists;
    std::vector<uint8_t> z_order;
};

// Diff entry for incremental updates between full snapshots.
struct SceneDiff {
    enum class Property : uint8_t { Opacity, Transform, Visibility };

    WidgetId widget_id;
    Property property;
    float value;
};

} // namespace prism
