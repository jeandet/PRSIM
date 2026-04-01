#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/context.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/node.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace prism {

struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    FocusPolicy focus_policy = FocusPolicy::none;
    WidgetVisualState visual_state;
    EditState edit_state;
    DrawList draws;
    DrawList overlay_draws;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    // connections must be declared after on_input so they are destroyed first,
    // disconnecting from on_input before it is destroyed
    std::vector<Connection> connections;
    std::function<void(WidgetNode&)> wire;
    std::function<void(WidgetNode&)> record;
    LayoutKind layout_kind = LayoutKind::Default;
    Rect canvas_bounds{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    bool expand = false;
    ExpandAxis expand_axis = ExpandAxis::None;
    const Theme* theme = nullptr;
    bool table_input_wired = false;
    std::shared_ptr<std::vector<std::string>> tab_names;
    Height viewport_height{0};
    Y absolute_y{0};
};

struct VirtualListState {
    ItemCount item_count{0};
    Height item_height{0};
    DY scroll_offset{0};
    Height viewport_h{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};
    ScrollBarPolicy scrollbar{ScrollBarPolicy::Auto};
    uint8_t show_ticks = 0;

    std::vector<WidgetNode> pool;
    std::function<void(WidgetNode&, size_t index)> bind_row;
    std::function<void(WidgetNode&)> unbind_row;
};

struct TabsState {
    std::shared_ptr<std::vector<std::string>> tab_names;
    std::vector<std::function<void(Node&)>> tab_node_builders;
    std::function<size_t()> get_selected;
    size_t active_tab = std::numeric_limits<size_t>::max();
};

inline std::pair<ItemIndex, ItemIndex> compute_visible_range(
    ItemCount item_count, Height item_height, DY scroll_offset,
    Height viewport_h, ItemCount overscan)
{
    if (item_count.raw() == 0 || item_height.raw() <= 0.f)
        return {ItemIndex{0}, ItemIndex{0}};

    float h = item_height.raw();
    size_t first_visible = static_cast<size_t>(std::max(0.f, scroll_offset.raw() / h));
    size_t last_visible = static_cast<size_t>(
        std::ceil((scroll_offset.raw() + viewport_h.raw()) / h));

    size_t start = (first_visible >= overscan.raw())
        ? first_visible - overscan.raw() : 0;
    size_t end = std::min(last_visible + overscan.raw(), item_count.raw());

    return {ItemIndex{start}, ItemIndex{end}};
}

inline const WidgetVisualState& node_vs(const WidgetNode& n) { return n.visual_state; }
inline Size node_allocated(const WidgetNode& n) { return n.canvas_bounds.extent; }

// --- Node factory functions (need complete WidgetNode) ---

template <typename T>
Node node_leaf(Field<T>& field, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;

    n.build_widget = [&field](WidgetNode& wn) {
        wn.focus_policy = Delegate<T>::focus_policy;
        if constexpr (requires { Delegate<T>::expand; })
            wn.expand = Delegate<T>::expand;
        if constexpr (requires { Delegate<T>::expand_axis; })
            wn.expand_axis = Delegate<T>::expand_axis;
        wn.record = [&field](WidgetNode& node) {
            node.draws.clear();
            node.overlay_draws.clear();
            Delegate<T>::record(node.draws, field, node);
        };
        wn.record(wn);
        wn.wire = [&field](WidgetNode& node) {
            node.connections.push_back(
                node.on_input.connect([&field, &node](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev, node);
                })
            );
        };
    };

    n.on_change = [&field](std::function<void()> cb) -> Connection {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}

template <typename T>
    requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
        t.canvas(dl, r, n);
    }
Node node_canvas(T& model, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;

    n.build_widget = [&model](WidgetNode& wn) {
        wn.record = [&model](WidgetNode& node) {
            node.draws.clear();
            model.canvas(node.draws, node.canvas_bounds, node);
        };
        wn.record(wn);

        if constexpr (requires(T& t, const InputEvent& ev, WidgetNode& nd, Rect r) {
                           t.handle_canvas_input(ev, nd, r);
                       }) {
            wn.focus_policy = FocusPolicy::tab_and_click;
            wn.wire = [&model](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&model, &node](const InputEvent& ev) {
                        model.handle_canvas_input(ev, node, node.canvas_bounds);
                    })
                );
            };
        }
    };

    return n;
}

} // namespace prism
