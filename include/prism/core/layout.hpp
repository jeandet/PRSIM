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

} // namespace prism
