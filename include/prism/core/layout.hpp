#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

#include <vector>

namespace prism {

namespace scrollbar {
    inline constexpr Width track_width{6.f};
    inline constexpr Width track_inset{8.f};
    inline constexpr Height min_thumb_h{20.f};

    inline Height thumb_height(Height viewport_h, Height content_h) {
        return Height{std::max(min_thumb_h.raw(), viewport_h.raw() * (viewport_h.raw() / content_h.raw()))};
    }
}

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
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table } kind = Kind::Leaf;
    DY scroll_offset{0};        // only for Kind::Scroll and VirtualList
    Height scroll_content_h{0}; // total content height, for scrollbar rendering
    size_t vlist_visible_start = 0;  // first materialized item index (VirtualList only)
    float vlist_item_height = 0;     // uniform item height (VirtualList only)
    DX table_scroll_x{0};
    size_t table_column_count = 0;
    float table_header_h = 0;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis);

namespace detail {

inline void measure_linear(LayoutNode& node, LayoutAxis own_axis, LayoutAxis parent_axis) {
    float sum = 0, max_cross = 0;
    bool has_expander = false;
    for (auto& child : node.children) {
        layout_measure(child, own_axis);
        sum += child.hint.preferred;
        max_cross = std::max(max_cross, child.hint.cross);
        if (child.hint.expand) has_expander = true;
    }
    bool aligned = (own_axis == parent_axis);
    node.hint.preferred = aligned ? sum : max_cross;
    node.hint.cross = aligned ? max_cross : sum;
    if (has_expander) node.hint.expand = true;
}

inline void measure_scrollable(LayoutNode& node) {
    float max_cross = 0;
    for (auto& child : node.children) {
        layout_measure(child, LayoutAxis::Vertical);
        max_cross = std::max(max_cross, child.hint.cross);
    }
    node.hint.expand = true;
    node.hint.preferred = 0;
    node.hint.cross = max_cross;
}

} // namespace detail

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
    case LayoutNode::Kind::Canvas:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Leaf: {
        auto bb = node.draws.bounding_box();
        bool horiz = (parent_axis == LayoutAxis::Horizontal);
        node.hint.preferred = horiz ? bb.extent.w.raw() : bb.extent.h.raw();
        node.hint.cross     = horiz ? bb.extent.h.raw() : bb.extent.w.raw();
        return;
    }
    case LayoutNode::Kind::Row:
        detail::measure_linear(node, LayoutAxis::Horizontal, parent_axis);
        return;
    case LayoutNode::Kind::Column:
        detail::measure_linear(node, LayoutAxis::Vertical, parent_axis);
        return;
    case LayoutNode::Kind::Scroll:
    case LayoutNode::Kind::VirtualList:
    case LayoutNode::Kind::Table:
        detail::measure_scrollable(node);
        return;
    }
}

inline void layout_arrange(LayoutNode& node, Rect available) {
    node.allocated = available;

    if (node.kind == LayoutNode::Kind::Table) {
        float item_h = node.vlist_item_height;
        if (item_h <= 0) item_h = 24.f;
        float header_h = node.table_header_h > 0 ? node.table_header_h : item_h;
        float body_top = available.origin.y.raw() + header_h;
        float start_y = body_top + static_cast<float>(node.vlist_visible_start) * item_h;
        for (auto& child : node.children) {
            layout_arrange(child, {
                Point{available.origin.x, Y{start_y}},
                Size{available.extent.w, Height{item_h}}
            });
            start_y += item_h;
        }
        return;
    }

    if (node.kind == LayoutNode::Kind::VirtualList) {
        float item_h = node.vlist_item_height;
        if (item_h <= 0 && !node.children.empty())
            item_h = node.children[0].hint.preferred;
        float start_y = available.origin.y.raw()
            + static_cast<float>(node.vlist_visible_start) * item_h;
        for (auto& child : node.children) {
            layout_arrange(child, {
                Point{available.origin.x, Y{start_y}},
                Size{available.extent.w, Height{item_h}}
            });
            start_y += item_h;
        }
        return;
    }

    if (node.kind == LayoutNode::Kind::Scroll) {
        float viewport_h = available.extent.h.raw();
        float offset = 0;
        for (auto& child : node.children) {
            float main_size = child.hint.expand
                ? std::max(child.hint.preferred, viewport_h) : child.hint.preferred;
            layout_arrange(child, {
                Point{available.origin.x, Y{available.origin.y.raw() + offset}},
                Size{available.extent.w, Height{main_size}}
            });
            offset += main_size;
        }
        node.scroll_content_h = Height{offset};
        return;
    }

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

inline void offset_subtree_y(LayoutNode& node, DY dy) {
    node.allocated.origin.y = Y{node.allocated.origin.y.raw() + dy.raw()};
    for (auto& child : node.children)
        offset_subtree_y(child, dy);
}

} // namespace detail

inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (node.kind == LayoutNode::Kind::Table) {
        auto vp = node.allocated;
        float item_h = node.vlist_item_height > 0 ? node.vlist_item_height : 24.f;
        float header_h = node.table_header_h > 0 ? node.table_header_h : item_h;

        snap.geometry.push_back({node.id, vp});

        // Header background (fixed at top, doesn't scroll vertically)
        DrawList header_dl;
        header_dl.clip_push(vp.origin, Size{vp.extent.w, Height{header_h}});
        header_dl.filled_rect(
            Rect{vp.origin, Size{vp.extent.w, Height{header_h}}},
            Color::rgba(42, 42, 74));
        header_dl.filled_rect(
            Rect{Point{vp.origin.x, vp.origin.y + DY{header_h - 2.f}},
                 Size{vp.extent.w, Height{2.f}}},
            Color::rgba(74, 74, 106));
        // Header cell text
        DX hdr_dx{vp.origin.x.raw()};
        DY hdr_dy{vp.origin.y.raw()};
        for (auto& cmd : node.overlay_draws.commands) {
            auto translated = cmd;
            std::visit([hdr_dx, hdr_dy](auto& c) {
                if constexpr (requires { c.rect; }) {
                    c.rect.origin.x = X{c.rect.origin.x.raw() + hdr_dx.raw()};
                    c.rect.origin.y = Y{c.rect.origin.y.raw() + hdr_dy.raw()};
                } else if constexpr (requires { c.origin; }) {
                    c.origin.x = X{c.origin.x.raw() + hdr_dx.raw()};
                    c.origin.y = Y{c.origin.y.raw() + hdr_dy.raw()};
                }
            }, translated);
            header_dl.commands.push_back(std::move(translated));
        }
        header_dl.clip_pop();
        snap.draw_lists.push_back(std::move(header_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Body region (clipped, scrollable)
        Rect body_rect{
            Point{vp.origin.x, vp.origin.y + DY{header_h}},
            Size{vp.extent.w, Height{vp.extent.h.raw() - header_h}}};

        DrawList body_clip;
        body_clip.clip_push(body_rect.origin, body_rect.extent);
        snap.geometry.push_back({0, body_rect});
        snap.draw_lists.push_back(std::move(body_clip));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Flatten visible children with scroll offset + header offset
        DY scroll_dy = node.scroll_offset;
        for (auto& child : node.children) {
            DY total_offset{-scroll_dy.raw() + header_h};
            detail::offset_subtree_y(child, total_offset);
            layout_flatten(child, snap);
            detail::offset_subtree_y(child, DY{-total_offset.raw()});
        }

        DrawList body_clip_pop;
        body_clip_pop.clip_pop();
        snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});
        snap.draw_lists.push_back(std::move(body_clip_pop));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Vertical scrollbar
        if (node.scroll_content_h.raw() > body_rect.extent.h.raw()) {
            Height thumb_h = scrollbar::thumb_height(body_rect.extent.h, node.scroll_content_h);
            float max_scroll = node.scroll_content_h.raw() - body_rect.extent.h.raw();
            float thumb_y = max_scroll > 0
                ? scroll_dy.raw() * (body_rect.extent.h.raw() - thumb_h.raw()) / max_scroll
                : 0.f;
            snap.overlay.filled_rect(
                Rect{Point{body_rect.origin.x + DX{body_rect.extent.w.raw() - scrollbar::track_inset.raw()},
                           body_rect.origin.y + DY{thumb_y}},
                     Size{Width{scrollbar::track_width}, thumb_h}},
                Color::rgba(120, 120, 130, 160));
            snap.overlay_geometry.push_back({node.id, vp});
        }

        return;
    }

    if (node.kind == LayoutNode::Kind::Scroll || node.kind == LayoutNode::Kind::VirtualList) {
        // ClipPush for the viewport
        DrawList clip_dl;
        clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(clip_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Flatten visible children with scroll offset applied to entire subtree
        DY scroll_dy = node.scroll_offset;
        DY neg_scroll{-scroll_dy.raw()};
        for (auto& child : node.children) {
            float child_top = child.allocated.origin.y.raw() - scroll_dy.raw();
            float child_bottom = child_top + child.allocated.extent.h.raw();
            float vp_top = node.allocated.origin.y.raw();
            float vp_bottom = vp_top + node.allocated.extent.h.raw();

            if (child_bottom <= vp_top || child_top >= vp_bottom)
                continue;

            detail::offset_subtree_y(child, neg_scroll);
            layout_flatten(child, snap);
            DY restore{scroll_dy.raw()};
            detail::offset_subtree_y(child, restore);
        }

        // ClipPop
        DrawList clip_pop_dl;
        clip_pop_dl.clip_pop();
        snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});
        snap.draw_lists.push_back(std::move(clip_pop_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Scrollbar overlay (if content exceeds viewport)
        if (node.scroll_content_h.raw() > node.allocated.extent.h.raw()) {
            auto vp = node.allocated;
            Height viewport_h = vp.extent.h;
            Height content_h = node.scroll_content_h;
            Height thumb_h = scrollbar::thumb_height(viewport_h, content_h);
            float max_scroll = content_h.raw() - viewport_h.raw();
            float thumb_y = (max_scroll > 0)
                ? node.scroll_offset.raw() * (viewport_h.raw() - thumb_h.raw()) / max_scroll
                : 0.f;

            Rect thumb_rect{
                Point{vp.origin.x + DX{vp.extent.w.raw() - scrollbar::track_inset.raw()},
                      vp.origin.y + DY{thumb_y}},
                Size{Width{scrollbar::track_width}, thumb_h}};
            snap.overlay.filled_rect(thumb_rect, Color::rgba(120, 120, 130, 160));
            snap.overlay_geometry.push_back({node.id, thumb_rect});
        }

        return;
    }

    if (!node.draws.empty() || node.kind == LayoutNode::Kind::Canvas) {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        detail::translate_draw_list(node.draws, dx, dy);
        // Canvas nodes fill their allocation; leaf widgets use drawn content bounds
        auto hit_rect = (node.kind == LayoutNode::Kind::Canvas)
            ? node.allocated : node.draws.bounding_box();
        auto idx = static_cast<uint16_t>(snap.geometry.size());
        snap.geometry.push_back({node.id, hit_rect});
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
