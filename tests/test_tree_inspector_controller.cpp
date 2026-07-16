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
    prism::debug::TreeInspectorController controller(main_tree, debug_model);

    controller.refresh();
    CHECK(debug_model.rows.size() >= 2); // root + the int field's leaf
}

TEST_CASE("clicking a debug row sets the main tree's highlight") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::debug::TreeInspectorController controller(main_tree, debug_model);
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
    auto leaf_rect = snap->geometry.back().second;
    main_tree.dispatch(leaf_id, prism::MouseMove{leaf_rect.origin}); // won't actually hover without
                                                                       // going through hit_test/route_mouse_move
    // Use the real routing path instead of raw dispatch, matching how other tests in this codebase
    // drive hover state (search tests/test_widget_tree.cpp for update_hover usage and mirror it):
    // main_tree.update_hover(leaf_id) is the direct, already-tested primitive to use here.
    main_tree.update_hover(leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::debug::TreeInspectorController controller(main_tree, debug_model);
    controller.refresh();

    CHECK(debug_model.selected.get() == std::optional<prism::WidgetId>{leaf_id});
}
