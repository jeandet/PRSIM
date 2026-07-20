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
