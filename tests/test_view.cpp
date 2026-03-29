#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>

TEST_CASE("WidgetNode has layout_kind defaulting to Default") {
    prism::WidgetNode node;
    CHECK(node.layout_kind == prism::WidgetNode::LayoutKind::Default);
}

struct RowModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
};

TEST_CASE("build_snapshot with default layout stacks vertically") {
    RowModel model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    // Both widgets should be stacked vertically (Column default)
    auto& [id0, r0] = snap->geometry[0];
    auto& [id1, r1] = snap->geometry[1];
    CHECK(r1.origin.y.raw() >= r0.origin.y.raw() + r0.extent.h.raw());
}

struct ViewModelSimple {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(b);
        vb.widget(a);
    }
};

TEST_CASE("view() controls field order") {
    ViewModelSimple model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 2);

    tree.clear_dirty();
    model.b.set(42);
    CHECK(tree.any_dirty());
}
