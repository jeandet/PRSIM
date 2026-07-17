#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>
#include <prism/app/test_backend.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct MainModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
}

TEST_CASE("TreeInspectorController refresh populates rows from the main tree") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    controller.refresh();
    CHECK(debug_model.rows.size() >= 2); // root + the int field's leaf
}

TEST_CASE("clicking a debug row sets the main tree's highlight") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    REQUIRE(!debug_model.rows.empty());
    controller.on_row_clicked(0, debug_model.rows[debug_model.rows.size() - 1]); // a leaf, not the root

    auto snap2 = main_tree.build_snapshot(400, 300, 2);
    bool found = false;
    for (auto& cmd : snap2->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found = true;
    CHECK(found);
}

TEST_CASE("hovering the main tree updates the debug model's selection on refresh") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto leaf_id = snap->geometry.back().first;
    main_tree.update_hover(leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    CHECK(debug_model.selected.get() == std::optional<prism::WidgetId>{leaf_id});
}

// New: auto-scroll. Needs the MAIN tree to have many direct sibling leaves — flatten_tree only
// descends into a container's children when that container's own id is in TreeInspectorController's
// expanded_ set, and the constructor only auto-expands the *root* — so the leaves must be direct
// children of the model's root wrapper node (not nested inside their own container) to all be
// visible without any prior expand-click.
namespace {
struct ManyLeavesModel {
    prism::Field<int> f0{0}, f1{0}, f2{0}, f3{0}, f4{0}, f5{0}, f6{0}, f7{0}, f8{0}, f9{0},
                       f10{0}, f11{0}, f12{0}, f13{0}, f14{0}, f15{0}, f16{0}, f17{0}, f18{0}, f19{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9,
                   f10, f11, f12, f13, f14, f15, f16, f17, f18, f19);
    }
};
}

namespace {
// Whether any rendered row in a debug-tree snapshot draws text containing `needle`. Used instead
// of WidgetTree::any_dirty() below: refresh() unconditionally erases and re-pushes every row on
// every call (pre-existing TreeInspectorController behavior, unchanged by this task), which alone
// dirties the list container's VirtualList node regardless of whether a scroll happened — verified
// empirically (a probe build with the scroll_row_into_view call stripped out still reported
// any_dirty() == true after refresh()). Checking which row text is actually materialized is a
// direct, scroll-specific signal instead.
bool debug_tree_shows_text(const prism::render::SceneSnapshot& snap, std::string_view needle) {
    for (auto& dl : snap.draw_lists)
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd))
                if (t->text.find(needle) != std::string_view::npos) return true;
    return false;
}
}

TEST_CASE("hovering an off-screen row in the main tree scrolls the debug tree to reveal it") {
    ManyLeavesModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    // Two refresh+snapshot cycles to let the debug window's own VirtualList state stabilize
    // (its viewport_h/content_h are only (re)computed during the debug tree's own
    // build_snapshot/materialize step — see tests/test_virtual_list.cpp's "VirtualList stabilizes
    // after two frames" for the same two-cycle convention) before asserting scroll behavior.
    controller.refresh();
    debug_tree.build_snapshot(200, 80, 1); // deliberately short — only a few debug rows fit
    debug_tree.clear_dirty();
    controller.refresh();
    auto before_snap = debug_tree.build_snapshot(200, 80, 2);
    debug_tree.clear_dirty();

    // Empirically confirmed (via a probe build, not assumed): with 21 rows (root + 20 leaves) at
    // row_h 22px in an 80px viewport, only the first ~3-4 rows are initially materialized, so
    // f19 (row index 20) starts genuinely off-screen and its text is absent here.
    REQUIRE(debug_model.rows.size() == 21);
    CHECK_FALSE(debug_tree_shows_text(*before_snap, "f19"));

    // Hover the LAST leaf (f19) — with 21 rows total (root + 20 leaves) in a viewport that only
    // fits a handful at row_h 22px (80/22 ≈ 3-4 rows), this leaf starts off-screen. Empirically
    // confirmed (via probe) that main_snap->geometry.back().first really is f19's id here (a flat
    // vstack of 20 direct siblings has no deeper nesting to make "last visited leaf" ambiguous).
    auto main_snap = main_tree.build_snapshot(400, 300, 3);
    auto last_leaf_id = main_snap->geometry.back().first;
    main_tree.update_hover(last_leaf_id);

    controller.refresh();
    auto after_snap = debug_tree.build_snapshot(200, 80, 3);
    CHECK(debug_tree_shows_text(*after_snap, "f19"));
}

// Guard-path test — the hovered node's row can legitimately be absent from the current
// flatten (its ancestor isn't in TreeInspectorController's expanded_ set), and refresh() must
// skip the scroll call rather than crash. A nested component starts collapsed by default (the
// constructor only auto-expands the root), so this needs no explicit collapse action to set up:
namespace {
struct NestedLeaf { prism::Field<int> inner{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(inner); } };
struct NestedMainModel {
    NestedLeaf nested;
    prism::Field<int> value{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.component(nested);
        vb.widget(value);
    }
};
}

TEST_CASE("hovering a node hidden by a collapsed ancestor does not crash and does not scroll") {
    NestedMainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto main_snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    // NestedLeaf::inner is the FIRST geometry leaf here, not the last: view() visits
    // vb.component(nested) (whose only leaf is `inner`) before vb.widget(value), so `inner`
    // is added to geometry first and `value` — a shallower but later sibling — comes after it.
    // Verified empirically with a probe dump of main_snap->geometry before writing this
    // assertion; geometry.back() is `value`'s id, which (unlike `inner`) DOES appear in the
    // flatten below and would defeat the point of this guard-path test.
    auto inner_leaf_id = main_snap->geometry.front().first;
    main_tree.update_hover(inner_leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    controller.refresh(); // must not crash
    // `inner` really is absent from the flatten (its ancestor "nested" isn't in expanded_): only
    // root + the "nested" container row + "value" are present, so the scroll_row_into_view call
    // in refresh() is structurally skipped (its id-matching loop never finds `inner_leaf_id`).
    // debug_tree.any_dirty() can't confirm this directly — refresh()'s unconditional
    // erase-then-repush of every row dirties the list container on every call regardless of
    // hover (see debug_tree_shows_text's comment above) — so this checks the structural
    // precondition and the pre-existing "still records selection even when not shown" behavior
    // instead.
    CHECK(debug_model.rows.size() == 3);
    CHECK(debug_model.selected.get() == std::optional<prism::WidgetId>{inner_leaf_id});
}
