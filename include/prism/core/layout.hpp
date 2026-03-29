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
    Rect allocated{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    DrawList draws;
    DrawList overlay_draws;  // after `DrawList draws;`
    std::vector<LayoutNode> children;
    enum class Kind { Leaf, Row, Column, Spacer, Canvas } kind = Kind::Leaf;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Canvas:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Leaf: {
        auto bb = node.draws.bounding_box();
        if (parent_axis == LayoutAxis::Horizontal) {
            node.hint.preferred = bb.extent.w.raw();
            node.hint.cross = bb.extent.h.raw();
        } else {
            node.hint.preferred = bb.extent.h.raw();
            node.hint.cross = bb.extent.w.raw();
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

    float remaining = (horizontal ? available.extent.w.raw() : available.extent.h.raw()) - total_preferred;
    float expand_share = (expand_count > 0) ? std::max(0.f, remaining) / expand_count : 0;

    float offset = 0;
    for (auto& child : node.children) {
        float main_size = child.hint.expand ? expand_share : child.hint.preferred;
        Rect child_rect;
        if (horizontal) {
            child_rect = {Point{X{available.origin.x.raw() + offset}, available.origin.y},
                          Size{Width{main_size}, available.extent.h}};
        } else {
            child_rect = {Point{available.origin.x, Y{available.origin.y.raw() + offset}},
                          Size{available.extent.w, Height{main_size}}};
        }
        layout_arrange(child, child_rect);
        offset += main_size;
    }
}

namespace detail {

inline void translate_draw_list(DrawList& dl, DX dx, DY dy) {
    for (auto& cmd : dl.commands) {
        std::visit([dx, dy](auto& c) {
            if constexpr (requires { c.rect; }) {
                c.rect.origin.x = X{c.rect.origin.x.raw() + dx.raw()};
                c.rect.origin.y = Y{c.rect.origin.y.raw() + dy.raw()};
            } else if constexpr (requires { c.origin; }) {
                c.origin.x = X{c.origin.x.raw() + dx.raw()};
                c.origin.y = Y{c.origin.y.raw() + dy.raw()};
            }
        }, cmd);
    }
}

} // namespace detail

inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (!node.draws.empty() || node.kind == LayoutNode::Kind::Canvas) {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        detail::translate_draw_list(node.draws, dx, dy);
        auto idx = static_cast<uint16_t>(snap.geometry.size());
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(node.draws));
        snap.z_order.push_back(idx);
    }

    if (!node.overlay_draws.empty()) {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        detail::translate_draw_list(node.overlay_draws, dx, dy);
        snap.overlay_geometry.push_back({node.id, node.overlay_draws.bounding_box()});
        for (auto& cmd : node.overlay_draws.commands)
            snap.overlay.commands.push_back(std::move(cmd));
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap);
    }
}

} // namespace prism
