#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/types.hpp>

#include <fmt/format.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace prism::debug {
using namespace prism::core;
using namespace prism::ui;
using namespace prism::app;

struct NodeRow {
    WidgetId id = 0;
    std::string name;
    std::string layout_kind_name;
    int depth = 0;
    Rect rect;
    bool dirty = false;
    bool hovered = false;
    bool focused = false;
    bool pressed = false;
    bool has_children = false;
    bool expanded = false;

    bool operator==(const NodeRow&) const = default;
};

inline std::string_view layout_kind_name(LayoutKind k) {
    switch (k) {
        case LayoutKind::Default:     return "Default";
        case LayoutKind::Row:         return "Row";
        case LayoutKind::Column:      return "Column";
        case LayoutKind::Spacer:      return "Spacer";
        case LayoutKind::Canvas:      return "Canvas";
        case LayoutKind::Scroll:      return "Scroll";
        case LayoutKind::VirtualList: return "VirtualList";
        case LayoutKind::Table:       return "Table";
        case LayoutKind::Tabs:        return "Tabs";
    }
    return "?";
}

namespace detail {

inline void flatten_node(const WidgetNode& node, int depth, WidgetId hovered_id,
                         const std::set<WidgetId>& expanded, std::vector<NodeRow>& out) {
    NodeRow row;
    row.id = node.id;
    row.layout_kind_name = std::string(layout_kind_name(node.layout_kind));
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    row.name = node.debug_name.empty() ? row.layout_kind_name : node.debug_name;
#else
    row.name = row.layout_kind_name;
#endif
    row.depth = depth;
    row.rect = Rect{Point{X{0}, Y{0}}, node.canvas_bounds.extent};
    row.dirty = node.dirty;
    row.hovered = (node.id == hovered_id);
    row.focused = node.visual_state.focused;
    row.pressed = node.visual_state.pressed;
    row.has_children = !node.children.empty();
    row.expanded = expanded.contains(node.id);
    out.push_back(row);

    if (!node.children.empty() && expanded.contains(node.id)) {
        for (auto& child : node.children)
            flatten_node(child, depth + 1, hovered_id, expanded, out);
    }
}

} // namespace detail

inline std::vector<NodeRow> flatten_tree(const WidgetTree& tree, const std::set<WidgetId>& expanded) {
    std::vector<NodeRow> rows;
    detail::flatten_node(tree.root(), 0, tree.hovered_id(), expanded, rows);
    return rows;
}

} // namespace prism::debug

namespace prism::ui {

// Renders one flattened tree row: indent by depth, expand/collapse marker,
// name, and a dirty indicator. Selection is driven entirely by the
// containing .list() call's on_row_click, not by this leaf's own input
// handling — see TreeInspectorModel.
template <>
struct Widget<prism::debug::NodeRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height row_h{22.f};

    static void record(DrawList& dl, const Field<prism::debug::NodeRow>& field, WidgetNode& node) {
        auto& row = field.get();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        // vs.hovered: mouse is over this row in the debug window itself.
        // row.hovered: this row's widget is the one currently hovered in the
        // *main* window (see flatten_node) — the "main -> debug" highlight direction.
        bool highlight = vs.hovered || row.hovered;
        auto bg = highlight ? t.surface_hover : t.surface;
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, row_h), bg);

        DX indent{static_cast<float>(row.depth) * 16.f};
        std::string label = (row.has_children ? (row.expanded ? "v " : "> ") : "  ") + row.name;
        dl.text(label, detail::make_point(X{8.f} + indent, Y{4.f}), 13,
                row.dirty ? t.accent : t.text);
    }

    static void handle_input(Field<prism::debug::NodeRow>&, const InputEvent&, WidgetNode&) {
        // Selection is handled entirely via on_row_click on the containing .list() call —
        // this leaf has nothing of its own to mutate on click.
    }
};

// Read-only detail pane for the row currently selected in the debug window's list (see
// TreeInspectorController::detail_selected_). Shows a placeholder when nothing is selected,
// otherwise dumps every NodeRow field as its own text line.
template <>
struct Widget<std::optional<prism::debug::NodeRow>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height panel_h{200.f};

    static void record(DrawList& dl, const Field<std::optional<prism::debug::NodeRow>>& field,
                        WidgetNode& node) {
        auto& t = node_theme(node);
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, panel_h), t.surface);

        auto& value = field.get();
        if (!value) {
            dl.text("No selection", detail::make_point(X{8.f}, Y{8.f}), 13, t.text);
            return;
        }

        auto& row = *value;
        Y y{8.f};
        auto line = [&](const std::string& text) {
            dl.text(text, detail::make_point(X{8.f}, y), 13, t.text);
            y = y + DY{18.f};
        };
        line("name: " + row.name);
        line("layout: " + row.layout_kind_name);
        line(fmt::format("rect: {}, {}, {}, {}", row.rect.origin.x.raw(), row.rect.origin.y.raw(),
                          row.rect.extent.w.raw(), row.rect.extent.h.raw()));
        line(std::string("dirty: ") + (row.dirty ? "true" : "false"));
        line(std::string("hovered: ") + (row.hovered ? "true" : "false"));
        line(std::string("focused: ") + (row.focused ? "true" : "false"));
        line(std::string("pressed: ") + (row.pressed ? "true" : "false"));
    }

    static void handle_input(Field<std::optional<prism::debug::NodeRow>>&, const InputEvent&,
                              WidgetNode&) {
        // Read-only debug output — nothing to mutate on input.
    }
};

} // namespace prism::ui

namespace prism::debug {

struct TreeInspectorModel {
    List<NodeRow> rows;
    Field<std::optional<WidgetId>> selected;
    Field<std::optional<NodeRow>> detail;
    std::function<void(size_t, const NodeRow&)> on_click;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            // Explicit template argument: deducing T from both `rows` (a
            // List<NodeRow>&) and this lambda (not itself a std::function)
            // fails template argument deduction, so T is pinned here and the
            // lambda converts to std::function normally.
            vb.list<NodeRow>(rows, [this](size_t index, const NodeRow& row) {
                if (on_click) on_click(index, row);
            });
            vb.widget(detail);
        });
    }
};

// Ties the main WidgetTree to a TreeInspectorModel (rendered by the debug
// window's own WidgetTree): refresh() re-flattens the main tree into the
// debug model's rows, mirrors the main tree's hover state into the debug
// model's selection, and scrolls the debug tree's list to reveal the
// hovered row when present; on_row_clicked() drives the main tree's debug
// highlight and toggles expand/collapse for the clicked row.
//
// Lifetime: TreeInspectorController stores raw pointers to the main
// WidgetTree, the debug WidgetTree, and the TreeInspectorModel, and the
// constructor wires debug_model.on_click to call back into this. The
// controller must not outlive any referenced object; callers own all three
// and the controller for the same scope (see
// tests/test_tree_inspector_controller.cpp).
class TreeInspectorController {
public:
    TreeInspectorController(WidgetTree& main_tree, WidgetTree& debug_tree, TreeInspectorModel& debug_model)
        : main_tree_(&main_tree), debug_tree_(&debug_tree), debug_model_(&debug_model) {
        expanded_.insert(main_tree_->root().id);
        // TreeInspectorModel::view() is a single top-level vb.hstack(...) call, so
        // ViewBuilder::finalize()'s single-child Row hoist fires: debug_tree_->root() becomes
        // the hstack's Row node itself, with the list container and the detail pane spliced
        // directly onto it as its two children, in the order view() adds them. The list is
        // added first, so it's still children.front() regardless of the hoist.
        list_container_id_ = debug_tree_->root().children.front().id;
        debug_model_->on_click = [this](size_t index, const NodeRow& row) {
            on_row_clicked(index, row);
        };
    }

    // debug_model_->on_click above captures `this` — a copy or move would leave that
    // callback pointing at a stale (possibly destroyed) controller.
    TreeInspectorController(const TreeInspectorController&) = delete;
    TreeInspectorController& operator=(const TreeInspectorController&) = delete;
    TreeInspectorController(TreeInspectorController&&) = delete;
    TreeInspectorController& operator=(TreeInspectorController&&) = delete;

    void refresh() {
        auto rows = flatten_tree(*main_tree_, expanded_);
        while (!debug_model_->rows.empty())
            debug_model_->rows.erase(debug_model_->rows.size() - 1);
        for (auto& row : rows)
            debug_model_->rows.push_back(row);

        auto hovered = main_tree_->hovered_id();
        // `selected` records which id is hovered; NodeRow::hovered (populated by flatten_tree
        // above) is what actually drives the visible main -> debug highlight. If the hovered
        // row is present in this flatten, also scroll the debug window's list to reveal it.
        if (hovered != 0) {
            debug_model_->selected.set(hovered);
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].id == hovered) {
                    debug_tree_->scroll_row_into_view(list_container_id_, i, Widget<NodeRow>::row_h);
                    break;
                }
            }
        }

        if (detail_selected_) {
            bool found = false;
            for (auto& row : rows) {
                if (row.id == *detail_selected_) {
                    debug_model_->detail.set(row);
                    found = true;
                    break;
                }
            }
            if (!found) {
                detail_selected_.reset();
                debug_model_->detail.set(std::nullopt);
            }
        }
    }

    void on_row_clicked(size_t, const NodeRow& row) {
        main_tree_->set_debug_highlight(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
        detail_selected_ = row.id;
        debug_model_->detail.set(row);
    }

private:
    WidgetTree* main_tree_;
    WidgetTree* debug_tree_;
    TreeInspectorModel* debug_model_;
    WidgetId list_container_id_;
    std::set<WidgetId> expanded_;
    std::optional<WidgetId> detail_selected_;
};

} // namespace prism::debug
