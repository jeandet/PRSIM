#pragma once

#include <prism/render/draw_list.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace prism::render {

using WidgetId = uint64_t;

// Complete, immutable description of what should be on screen.
// All vectors are parallel — indexed by the same position.
// z_order contains indices into geometry/draw_lists, back-to-front.
struct SceneSnapshot {
    uint64_t version = 0;
    std::vector<std::pair<WidgetId, Rect>> geometry;
    // Per-widget entries may be shared with a previous snapshot (see layout_flatten's
    // per-widget DrawList cache) -- const so nothing downstream can mutate a shared entry
    // out from under another snapshot still holding it.
    std::vector<std::shared_ptr<const DrawList>> draw_lists;
    std::vector<uint32_t> z_order; // uint32_t: a uint16_t silently wraps past 65535 entries
    DrawList overlay;  // rendered last, on top of everything, no clip
    std::vector<std::pair<WidgetId, Rect>> overlay_geometry;  // hit-test regions for overlays

    // Instrumentation, populated by WidgetTree::build_snapshot(). Not part of the
    // rendered scene -- for the debug tree inspector / perf overlay.
    double build_time_ms = 0.0;
    std::size_t dirty_widget_count = 0;
    std::size_t draw_command_count = 0;
    std::size_t approx_bytes = 0;
    std::size_t layout_pass_count = 0;  // 1 normally, 2 when a VirtualList/Table viewport changed
};

// Pre-intersect all ClipPush rects so backends receive final clip regions.
// Must be called after layout_flatten, before handing the snapshot to any backend.
inline void resolve_clips(SceneSnapshot& snap) {
    std::vector<Rect> stack;
    for (uint32_t idx : snap.z_order) {
        // Entries below are only ever mutated while a clip is already active on `stack`.
        // layout_flatten() only shares/caches a widget's DrawList across snapshots when it
        // has no clipping ancestor (see its clipped_by_ancestor parameter), so any entry
        // reached here with a non-empty stack was freshly built this call and isn't aliased
        // by any other snapshot -- the const_pointer_cast reflects that invariant, not
        // general license to mutate a published snapshot.
        auto& dl = *std::const_pointer_cast<DrawList>(snap.draw_lists[idx]);
        for (auto& cmd : dl.commands) {
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

} // namespace prism::render
