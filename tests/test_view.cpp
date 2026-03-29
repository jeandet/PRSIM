#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <string>

#include <prism/core/widget_tree.hpp>

TEST_CASE("WidgetNode has layout_kind defaulting to Default") {
    prism::WidgetNode node;
    CHECK(node.layout_kind == prism::LayoutKind::Default);
}

struct RowModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(a);
        vb.widget(b);
    }
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

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(x);
        vb.widget(y);
    }
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

TEST_CASE("WidgetNode LayoutKind::Canvas exists") {
    prism::WidgetNode node;
    node.layout_kind = prism::LayoutKind::Canvas;
    CHECK(node.layout_kind == prism::LayoutKind::Canvas);
}

struct CanvasModel {
    prism::Field<int> a{0};

    void canvas(prism::DrawList&, prism::Rect, const prism::WidgetNode&) {}

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(a);
        vb.canvas(*this);
    }
};

struct DrawingCanvasModel {
    prism::Field<int> value{42};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(255, 0, 0));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(value);
        vb.canvas(*this).depends_on(value);
    }
};

TEST_CASE("canvas record() receives allocated bounds from layout") {
    DrawingCanvasModel model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(400, 300, 1);
    REQUIRE(snap->geometry.size() == 2);

    auto& canvas_draws = snap->draw_lists[1];
    REQUIRE_FALSE(canvas_draws.empty());

    auto& cmd = canvas_draws.commands[0];
    auto* fr = std::get_if<prism::FilledRect>(&cmd);
    REQUIRE(fr != nullptr);
    CHECK(fr->rect.extent.w.raw() > 0);
    CHECK(fr->rect.extent.h.raw() > 0);
}

// ── Task 4: canvas dirty tracking via depends_on ─────────────────────────────

TEST_CASE("canvas depends_on triggers dirty on field change") {
    DrawingCanvasModel model;
    prism::WidgetTree tree(model);

    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    model.value.set(99);
    CHECK(tree.any_dirty());
}

TEST_CASE("canvas depends_on supports multiple fields") {
    struct MultiDepCanvas {
        prism::Field<int> x{0};
        prism::Field<int> y{0};

        void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
            dl.filled_rect(bounds, prism::Color::rgba(0, 0, 0));
        }

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.canvas(*this).depends_on(x).depends_on(y);
        }
    };

    MultiDepCanvas model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    tree.clear_dirty();
    model.x.set(1);
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    model.y.set(2);
    CHECK(tree.any_dirty());
}

// ── Task 5: canvas input handling ────────────────────────────────────────────

struct InteractiveCanvas {
    prism::Field<int> click_count{0};
    bool input_received = false;

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(30, 30, 40));
    }

    void handle_canvas_input(const prism::InputEvent& ev, prism::WidgetNode&, prism::Rect) {
        if (std::get_if<prism::MouseButton>(&ev))
            input_received = true;
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(click_count);
    }
};

TEST_CASE("canvas with handle_canvas_input is focusable") {
    InteractiveCanvas model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    CHECK(focus.size() == 1);
}

TEST_CASE("canvas dispatches input to handle_canvas_input") {
    InteractiveCanvas model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 1);

    prism::MouseButton click{prism::Point{prism::X{50}, prism::Y{50}}, 1, true};
    tree.dispatch(ids[0], click);

    CHECK(model.input_received);
}

TEST_CASE("canvas node expands to fill remaining space") {
    CanvasModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    auto& [id_a, r_a] = snap->geometry[0];
    auto& [id_canvas, r_canvas] = snap->geometry[1];
    CHECK(r_canvas.origin.y.raw() >= r_a.origin.y.raw() + r_a.extent.h.raw());
    CHECK(r_canvas.extent.h.raw() > 0);
    CHECK(r_canvas.extent.w.raw() == doctest::Approx(800));
}

struct CanvasInRow {
    prism::Field<int> a{0};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(0, 100, 0));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.canvas(*this);
        });
    }
};

TEST_CASE("canvas in row expands horizontally") {
    CanvasInRow model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap->geometry.size() == 2);

    auto& [id_a, r_a] = snap->geometry[0];
    auto& [id_canvas, r_canvas] = snap->geometry[1];

    // Same y (row), canvas starts after widget a
    CHECK(r_canvas.origin.y.raw() == r_a.origin.y.raw());
    CHECK(r_canvas.origin.x.raw() >= r_a.origin.x.raw() + r_a.extent.w.raw());
    // Canvas fills remaining width
    float expected_w = 800 - r_a.extent.w.raw();
    CHECK(r_canvas.extent.w.raw() == doctest::Approx(expected_w));
}

struct CanvasOnlyModel {
    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(0, 0, 100));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this);
    }
};

TEST_CASE("canvas-only model fills entire viewport") {
    CanvasOnlyModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap->geometry.size() == 1);

    auto& [id, r] = snap->geometry[0];
    CHECK(r.extent.w.raw() == doctest::Approx(800));
    CHECK(r.extent.h.raw() == doctest::Approx(600));
}
