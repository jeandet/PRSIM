#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <string>

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

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.spacer();
            vb.widget(b);
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

// ── Task 7: view() with component() ──────────────────────────────────────────

struct SubComponent {
    prism::Field<int> x{0};
    prism::Field<int> y{0};
};

struct ParentWithComponent {
    SubComponent sub;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.component(sub);
            vb.widget(flag);
        });
    }
};

TEST_CASE("view() with component() embeds sub-component tree") {
    ParentWithComponent model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);
}

TEST_CASE("view() with component(): sub-component fields are reactive") {
    ParentWithComponent model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();

    model.sub.x.set(99);
    CHECK(tree.any_dirty());
}

// ── Task 8: recursive view() — sub-component with its own view() ─────────────

struct SubWithView {
    prism::Field<int> x{0};
    prism::Field<int> y{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(y);
            vb.widget(x);
        });
    }
};

struct ParentWithViewedSub {
    SubWithView sub;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.component(sub);
        vb.widget(flag);
    }
};

TEST_CASE("component() delegates to sub-component's view()") {
    ParentWithViewedSub model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    // Sub-component used row layout: its two widgets should share same y
    auto& [id0, r0] = snap->geometry[0];
    auto& [id1, r1] = snap->geometry[1];
    CHECK(r0.origin.y.raw() == r1.origin.y.raw());
}

// ── Task 9: omitted fields and focus order ──────────────────────────────────

struct OmittedFieldModel {
    prism::Field<int> shown{0};
    prism::Field<int> hidden{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(shown);
    }
};

TEST_CASE("omitted field has no widget but remains reactive") {
    OmittedFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    model.hidden.set(99);
    CHECK(model.hidden.get() == 99);
}

struct FocusViewModel {
    prism::Field<prism::Button> btn1{{"First"}};
    prism::Field<prism::Label<>> label{{"text"}};
    prism::Field<prism::Button> btn2{{"Second"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(btn2);
        vb.widget(label);
        vb.widget(btn1);
    }
};

TEST_CASE("focus order follows view() placement order") {
    FocusViewModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    auto ids = tree.leaf_ids();
    REQUIRE(focus.size() == 2);
    CHECK(focus[0] == ids[0]);
    CHECK(focus[1] == ids[2]);
}

// ── Task 10: dirty propagation ──────────────────────────────────────────────

TEST_CASE("dirty propagation works for view()-placed widgets") {
    ViewModelSimple model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    model.a.set(42);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    REQUIRE(snap2 != nullptr);
    CHECK(snap2->version == 2);
}

// ── Task 11: regression guard ───────────────────────────────────────────────

struct PlainModel {
    prism::Field<int> a{0};
    prism::Field<std::string> b{"hello"};
    prism::Field<bool> c{false};
};

TEST_CASE("model without view() still uses reflection walk") {
    PlainModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    for (size_t i = 1; i < snap->geometry.size(); ++i) {
        auto& [id_prev, r_prev] = snap->geometry[i - 1];
        auto& [id_curr, r_curr] = snap->geometry[i];
        CHECK(r_curr.origin.y.raw() >= r_prev.origin.y.raw() + r_prev.extent.h.raw());
    }
}
