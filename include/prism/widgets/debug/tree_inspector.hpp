#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/types.hpp>

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

} // namespace prism::ui

namespace prism::debug {

struct TreeInspectorModel {
    List<NodeRow> rows;
    Field<std::optional<WidgetId>> selected;
    std::function<void(size_t, const NodeRow&)> on_click;

    void view(WidgetTree::ViewBuilder& vb) {
        // Explicit template argument: deducing T from both `rows` (a
        // List<NodeRow>&) and this lambda (not itself a std::function)
        // fails template argument deduction, so T is pinned here and the
        // lambda converts to std::function normally.
        vb.list<NodeRow>(rows, [this](size_t index, const NodeRow& row) {
            if (on_click) on_click(index, row);
        });
    }
};

// Ties the main WidgetTree to a TreeInspectorModel: refresh() re-flattens the
// main tree into the debug model's rows and mirrors the main tree's hover
// state into the debug model's selection; on_row_clicked() drives the main
// tree's debug highlight and toggles expand/collapse for the clicked row.
//
// Lifetime: TreeInspectorController stores raw pointers to both the main
// WidgetTree and the TreeInspectorModel, and the constructor wires
// debug_model.on_click to call back into this. The controller must not
// outlive either referenced object; callers own both and the controller for
// the same scope (see tests/test_tree_inspector_controller.cpp).
class TreeInspectorController {
public:
    TreeInspectorController(WidgetTree& main_tree, TreeInspectorModel& debug_model)
        : main_tree_(&main_tree), debug_model_(&debug_model) {
        expanded_.insert(main_tree_->root().id);
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
        // `selected` now only records which id is hovered; NodeRow::hovered (populated by
        // flatten_tree above) is what actually drives the visible main -> debug highlight.
        // Auto-scrolling the debug list to bring that row into view is a deliberately
        // deferred follow-up — it needs TreeInspectorController to also hold the debug
        // window's own WidgetTree (currently only main_tree_ is held), which is a
        // constructor signature change touching this already-reviewed class, its
        // model_app.hpp call site, and its existing tests.
        if (hovered != 0)
            debug_model_->selected.set(hovered);
    }

    void on_row_clicked(size_t, const NodeRow& row) {
        main_tree_->set_debug_highlight(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
    }

private:
    WidgetTree* main_tree_;
    TreeInspectorModel* debug_model_;
    std::set<WidgetId> expanded_;
};

} // namespace prism::debug
