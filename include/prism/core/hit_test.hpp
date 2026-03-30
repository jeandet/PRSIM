#pragma once

#include <prism/core/scene_snapshot.hpp>

#include <optional>

namespace prism {

[[nodiscard]] inline std::optional<WidgetId> hit_test(
    const SceneSnapshot& snap, Point pos)
{
    // Overlays render on top — check them first
    for (auto it = snap.overlay_geometry.rbegin(); it != snap.overlay_geometry.rend(); ++it) {
        auto& [id, rect] = *it;
        if (rect.contains(pos))
            return id;
    }
    // Then normal widget geometry, back-to-front
    for (auto it = snap.z_order.rbegin(); it != snap.z_order.rend(); ++it) {
        auto& [id, rect] = snap.geometry[*it];
        if (rect.contains(pos))
            return id;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<Rect> find_widget_rect(
    const SceneSnapshot& snap, WidgetId id)
{
    for (auto& [wid, rect] : snap.geometry) {
        if (wid == id) return rect;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<WidgetId> hit_test_overlay(
    const SceneSnapshot& snap, Point pos)
{
    for (auto it = snap.overlay_geometry.rbegin(); it != snap.overlay_geometry.rend(); ++it) {
        if (it->second.contains(pos))
            return it->first;
    }
    return std::nullopt;
}

} // namespace prism
