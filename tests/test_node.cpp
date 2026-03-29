#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/core/widget_tree.hpp>

TEST_CASE("node_leaf creates a leaf Node from Field<int>") {
    prism::Field<int> count{0};
    prism::WidgetId next_id = 1;
    auto node = prism::node_leaf(count, next_id);
    CHECK(node.is_leaf);
    CHECK(node.id == 1);
    CHECK(next_id == 2);
    CHECK(node.build_widget != nullptr);
    CHECK(node.on_change != nullptr);
}

TEST_CASE("node_leaf on_change fires when field changes") {
    prism::Field<int> count{0};
    prism::WidgetId next_id = 1;
    auto node = prism::node_leaf(count, next_id);
    bool fired = false;
    auto conn = node.on_change([&fired]() { fired = true; });
    count.set(42);
    CHECK(fired);
}

struct NodeTestTwoFields {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(a, b);
    }
};

TEST_CASE("Node tree through WidgetTree produces same leaf count") {
    NodeTestTwoFields model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

struct NodeTestSingleField {
    prism::Field<int> val{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(val); }
};

TEST_CASE("Node tree dirty tracking works through WidgetTree") {
    NodeTestSingleField model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    model.val.set(99);
    CHECK(tree.any_dirty());
}

struct NodeTestRowLayout {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] { vb.widget(a); vb.widget(b); });
    }
};

TEST_CASE("Node tree with Row layout through WidgetTree") {
    NodeTestRowLayout model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);
    auto& [id0, r0] = snap->geometry[0];
    auto& [id1, r1] = snap->geometry[1];
    CHECK(r0.origin.y.raw() == r1.origin.y.raw());
    CHECK(r1.origin.x.raw() > r0.origin.x.raw());
}

struct NodeTestInner {
    prism::Field<int> x{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(x); }
};

struct NodeTestOuter {
    NodeTestInner inner;
    prism::Field<int> y{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(inner, y);
    }
};

TEST_CASE("Node tree with nested component through WidgetTree") {
    NodeTestOuter model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

struct NodeTestCanvasMultiDep {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(255, 0, 0));
    }
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(a).depends_on(b);
    }
};

TEST_CASE("Canvas node depends_on tracks multiple fields") {
    NodeTestCanvasMultiDep model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    model.a.set(1);
    CHECK(tree.any_dirty());
    tree.clear_dirty();
    model.b.set(1);
    CHECK(tree.any_dirty());
}
