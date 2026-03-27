#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace prism {

enum class LayoutAxis { Horizontal, Vertical };

struct SizeHint {
    float preferred = 0;
    float min = 0;
    float max = std::numeric_limits<float>::max();
    float cross = 0;
    bool expand = false;
};

struct LayoutNode {
    WidgetId id = 0;
    SizeHint hint;
    Rect allocated{0, 0, 0, 0};
    DrawList draws;
    std::vector<LayoutNode> children;
    enum class Kind { Leaf, Row, Column, Spacer } kind = Kind::Leaf;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Leaf: {
        auto bb = node.draws.bounding_box();
        if (parent_axis == LayoutAxis::Horizontal) {
            node.hint.preferred = bb.w;
            node.hint.cross = bb.h;
        } else {
            node.hint.preferred = bb.h;
            node.hint.cross = bb.w;
        }
        return;
    }
    case LayoutNode::Kind::Row: {
        LayoutAxis child_axis = LayoutAxis::Horizontal;
        float sum = 0, max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, child_axis);
            sum += child.hint.preferred;
            max_cross = std::max(max_cross, child.hint.cross);
        }
        if (parent_axis == LayoutAxis::Horizontal) {
            node.hint.preferred = sum;
            node.hint.cross = max_cross;
        } else {
            node.hint.preferred = max_cross;
            node.hint.cross = sum;
        }
        return;
    }
    case LayoutNode::Kind::Column: {
        LayoutAxis child_axis = LayoutAxis::Vertical;
        float sum = 0, max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, child_axis);
            sum += child.hint.preferred;
            max_cross = std::max(max_cross, child.hint.cross);
        }
        if (parent_axis == LayoutAxis::Vertical) {
            node.hint.preferred = sum;
            node.hint.cross = max_cross;
        } else {
            node.hint.preferred = max_cross;
            node.hint.cross = sum;
        }
        return;
    }
    }
}

inline void layout_arrange(LayoutNode& node, Rect available) {
    node.allocated = available;

    if (node.children.empty()) return;

    bool horizontal = (node.kind == LayoutNode::Kind::Row);

    // Count expanders and sum preferred sizes
    float total_preferred = 0;
    int expand_count = 0;
    for (auto& child : node.children) {
        if (child.hint.expand)
            ++expand_count;
        else
            total_preferred += child.hint.preferred;
    }

    float remaining = (horizontal ? available.w : available.h) - total_preferred;
    float expand_share = (expand_count > 0) ? std::max(0.f, remaining) / expand_count : 0;

    float offset = 0;
    for (auto& child : node.children) {
        float main_size = child.hint.expand ? expand_share : child.hint.preferred;
        Rect child_rect;
        if (horizontal) {
            child_rect = {available.x + offset, available.y, main_size, available.h};
        } else {
            child_rect = {available.x, available.y + offset, available.w, main_size};
        }
        layout_arrange(child, child_rect);
        offset += main_size;
    }
}

namespace detail {

inline void translate_draw_list(DrawList& dl, float dx, float dy) {
    for (auto& cmd : dl.commands) {
        std::visit([dx, dy](auto& c) {
            if constexpr (requires { c.rect; }) {
                c.rect.x += dx;
                c.rect.y += dy;
            } else if constexpr (requires { c.origin; }) {
                c.origin.x += dx;
                c.origin.y += dy;
            }
        }, cmd);
    }
}

} // namespace detail

inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (!node.draws.empty()) {
        detail::translate_draw_list(node.draws, node.allocated.x, node.allocated.y);
        auto idx = static_cast<uint16_t>(snap.geometry.size());
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(node.draws));
        snap.z_order.push_back(idx);
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap);
    }
}

} // namespace prism
