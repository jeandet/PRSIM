#pragma once

#include <prism/core/scene_snapshot.hpp>

#include <optional>

namespace prism {

[[nodiscard]] inline std::optional<WidgetId> hit_test(
    const SceneSnapshot& snap, Point pos)
{
    // Walk z_order back-to-front, return first hit
    for (auto it = snap.z_order.rbegin(); it != snap.z_order.rend(); ++it) {
        auto& [id, rect] = snap.geometry[*it];
        if (rect.contains(pos))
            return id;
    }
    return std::nullopt;
}

} // namespace prism
