#pragma once

#include <prism/render/draw_list.hpp>

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

// Pre-intersect all ClipPush rects so backends receive final clip regions.
// Must be called after layout_flatten, before handing the snapshot to any backend.
inline void resolve_clips(SceneSnapshot& snap) {
    std::vector<Rect> stack;
    for (uint16_t idx : snap.z_order) {
        for (auto& cmd : snap.draw_lists[idx].commands) {
            if (auto* cp = std::get_if<ClipPush>(&cmd)) {
                if (!stack.empty())
                    cp->rect = stack.back().intersect(cp->rect);
                stack.push_back(cp->rect);
            } else if (std::holds_alternative<ClipPop>(cmd)) {
                if (!stack.empty()) stack.pop_back();
            }
        }
    }
}

} // namespace prism
