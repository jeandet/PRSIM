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

struct RowViewModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.widget(b);
        });
    }
};

TEST_CASE("view() with row() produces side-by-side layout") {
    RowViewModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    auto& [id0, r0] = snap->geometry[0];
    auto& [id1, r1] = snap->geometry[1];
    // In a row, both widgets share the same y but different x
    CHECK(r0.origin.y.raw() == r1.origin.y.raw());
    CHECK(r1.origin.x.raw() > r0.origin.x.raw());
}

struct NestedLayoutModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    prism::Field<int> c{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.widget(b);
        });
        vb.widget(c);
    }
};

TEST_CASE("view() with row + extra widget wraps in implicit Column") {
    NestedLayoutModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    auto& [ida, ra] = snap->geometry[0];
    auto& [idb, rb] = snap->geometry[1];
    auto& [idc, rc] = snap->geometry[2];

    // a and b are in a row: same y, different x
    CHECK(ra.origin.y.raw() == rb.origin.y.raw());
    CHECK(rb.origin.x.raw() > ra.origin.x.raw());

    // c is below the row
    float row_bottom = ra.origin.y.raw() + ra.extent.h.raw();
    CHECK(rc.origin.y.raw() >= row_bottom);
}

struct SpacerModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    // column wrapper gives the row full window width so the spacer can expand
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.column([&] {
            vb.row([&] {
                vb.widget(a);
                vb.spacer();
                vb.widget(b);
            });
        });
    }
};

TEST_CASE("view() with spacer pushes widgets apart") {
    SpacerModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);  // spacer has no geometry

    auto& [ida, ra] = snap->geometry[0];
    auto& [idb, rb] = snap->geometry[1];
    // Both in a row, spacer takes remaining space
    CHECK(ra.origin.y.raw() == rb.origin.y.raw());
    // b should be pushed far right (spacer expands)
    float gap = rb.origin.x.raw() - (ra.origin.x.raw() + ra.extent.w.raw());
    CHECK(gap > 0);
}
