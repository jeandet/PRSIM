#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
// root(1) -> {child(2), child(3)}
struct FixtureTree {
    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(prism::TreeNodeId id) const { return id == 1 ? 2 : 0; }
    prism::TreeNodeId child_at(prism::TreeNodeId, size_t i) const { return i == 0 ? 2 : 3; }
    std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(prism::TreeNodeId id) const { return id == 1; }
};

struct TreeModel {
    prism::TreeController ctrl;
    TreeModel() : ctrl(prism::wrap_tree_storage(fixture_)) {}
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
private:
    FixtureTree fixture_;
};
}

TEST_CASE("vb.tree() builds without crashing and produces geometry") {
    TreeModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap->geometry.size() > 0);
}

TEST_CASE("the tree container is focusable via focus_order") {
    TreeModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(400, 300, 1);
    // Exactly one focusable target: the tree container itself (rows are focus_policy::none).
    CHECK(tree.focus_order().size() == 1);
}

TEST_CASE("clicking the root row expands it and updates the row count") {
    TreeModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!model.ctrl.rows.empty());
    // The root row is the sole entry before any click.
    CHECK(model.ctrl.rows.size() == 1);

    // Find the root row's on-screen WidgetId via the snapshot geometry produced by the
    // VirtualList's own row-binding (mirrors the pattern already established in
    // tests/test_tree_inspector_controller.cpp and test_virtual_list.cpp). Verified empirically
    // (via a throwaway id/rect dump) rather than assumed: geometry[0] is the VirtualList
    // container's own clip-viewport entry (id = container's WidgetId, rect = its full allocated
    // extent -- dumped as id=3, 200x300 for this 400x300 snapshot split evenly by the hstack with
    // the detail panel); geometry[1] is the first visible row, whose rect height (22px) matches
    // Widget<TreeRow>::row_h. See .superpowers/sdd/task-5-report.md for the full dump.
    REQUIRE(snap->geometry.size() > 1);
    auto row_id = snap->geometry[1].first;

    tree.dispatch(row_id, prism::MouseButton{prism::Point{prism::X{5}, prism::Y{5}}, 1, true});

    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
    CHECK(model.ctrl.rows.size() == 3); // root + 2 children now visible
}

TEST_CASE("Down arrow on the focused tree container moves selection") {
    TreeModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(tree.focus_order().size() == 1);
    auto container_id = tree.focus_order()[0];
    tree.set_focused(container_id);

    model.ctrl.selected.set(prism::TreeNodeId{1});
    model.ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand root -> rows = [1, 2, 3]
    (void)tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();

    tree.dispatch(container_id, prism::KeyPress{prism::keys::down, 0});
    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("keyboard navigation scrolls the newly selected row into view") {
    // A tree with enough rows that the last one starts off-screen in a short viewport --
    // same shape of test as test_tree_inspector_controller.cpp's auto-scroll case.
    struct WideFixture {
        size_t root_count() const { return 1; }
        prism::TreeNodeId root_at(size_t) const { return 100; }
        size_t child_count(prism::TreeNodeId id) const { return id == 100 ? 20 : 0; }
        prism::TreeNodeId child_at(prism::TreeNodeId, size_t i) const { return 200 + i; }
        std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
        bool has_children(prism::TreeNodeId id) const { return id == 100; }
    };
    struct WideModel {
        prism::TreeController ctrl;
        WideModel() : ctrl(prism::wrap_tree_storage(fixture_)) {}
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
    private:
        WideFixture fixture_;
    };

    WideModel model;
    prism::WidgetTree tree(model);
    model.ctrl.selected.set(prism::TreeNodeId{100});
    model.ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand -> 21 rows
    (void)tree.build_snapshot(300, 80, 1); // short viewport, only a handful of rows fit
    tree.clear_dirty();

    REQUIRE(tree.focus_order().size() == 1);
    auto container_id = tree.focus_order()[0];
    tree.set_focused(container_id);

    // Move down 20 times to reach the last child (n219), which should start off-screen.
    // Verified empirically (debug dump of selected.get() after each press): the root row
    // (id 100) occupies index 0, so it takes 20 presses -- not 19 -- to walk down to the
    // last child at index 20 (n219); 19 presses lands one row short, at n218.
    for (int i = 0; i < 20; ++i)
        tree.dispatch(container_id, prism::KeyPress{prism::keys::down, 0});

    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{219});

    auto snap = tree.build_snapshot(300, 80, 2);
    bool found = false;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd))
                if (t->text.find("n219") != std::string_view::npos) found = true;
    CHECK(found); // scroll_row_into_view should have brought it into the materialized range
}
