#pragma once

#include <prism/core/draw_list.hpp>

#include <cstdint>
#include <vector>

namespace prism {

using WidgetId = uint64_t;

// Complete, immutable description of what should be on screen.
// All vectors are parallel — indexed by the same position.
// z_order contains indices into geometry/draw_lists, back-to-front.
struct SceneSnapshot {
    uint64_t version = 0;
    std::vector<std::pair<WidgetId, Rect>> geometry;
    std::vector<DrawList> draw_lists;
    std::vector<uint16_t> z_order;
    DrawList overlay;  // rendered last, on top of everything, no clip
    std::vector<std::pair<WidgetId, Rect>> overlay_geometry;  // hit-test regions for overlays
};

} // namespace prism
