#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/layout.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/app/event_routing.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;

struct TwoPaneModel {
    Field<int> a{0};
    Field<int> b{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("A Handle between two panes renders as a fixed-width bar") {
    TwoPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);

    REQUIRE(snap->geometry.size() == 3);
    auto& [pane0_id, pane0_rect] = snap->geometry[0];
    auto& [handle_id, handle_rect] = snap->geometry[1];
    auto& [pane1_id, pane1_rect] = snap->geometry[2];

    CHECK(handle_rect.extent.w.raw() == doctest::Approx(splitter::thickness_px));
    CHECK(handle_rect.origin.x.raw() == doctest::Approx(pane0_rect.origin.x.raw() + pane0_rect.extent.w.raw()));
    CHECK(pane1_rect.origin.x.raw() == doctest::Approx(handle_rect.origin.x.raw() + handle_rect.extent.w.raw()));
}

struct TwoPaneModelWithId {
    Field<int> a{0};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("First drag engages split mode and resizes exactly the two adjacent panes") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(model.container_id != 0);
    REQUIRE(snap->geometry.size() == 3);

    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto [pane1_id, pane1_rect] = snap->geometry[2];
    float pane0_w0 = pane0_rect.extent.w.raw();
    float pane1_w0 = pane1_rect.extent.w.raw();

    float anchor = handle_rect.origin.x.raw() + handle_rect.extent.w.raw() / 2.f;
    tree.begin_split_drag(model.container_id, 0, anchor);
    CHECK(tree.in_split_drag());

    tree.update_split_drag(anchor + 20.f);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    [[maybe_unused]] auto [handle_id2, handle_rect2] = snap2->geometry[1];
    auto [pane1_id2, pane1_rect2] = snap2->geometry[2];

    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0 + 20.f));
    CHECK(pane1_rect2.extent.w.raw() == doctest::Approx(pane1_w0 - 20.f));

    tree.end_split_drag();
    CHECK_FALSE(tree.in_split_drag());
}

struct ContainerPaneModel {
    Field<int> a{0};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.vstack([&] { vb.widget(a); });  // container-kind pane (nested vstack), not a leaf
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("First drag captures a container-kind pane's real size, not just leaf panes") {
    // The nested vstack's own arranged width is never exposed via canvas_bounds
    // (only Leaf/Canvas/Handle get that) -- this specifically proves
    // arranged_extent reaches container-kind panes too, which begin_split_drag
    // depends on. geometry[0] is the leaf INSIDE the nested vstack; since a
    // Column arranges its child at the container's full allocated width, this
    // leaf's rect width equals the container pane's own width.
    ContainerPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(snap->geometry.size() == 3);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    float pane0_w0 = pane0_rect.extent.w.raw();

    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    tree.update_split_drag(anchor + 20.f);

    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    // If arranged_extent were NOT populated for a container-kind pane (stayed
    // at its default {0,0}), the captured pre-drag size would be 0 and this
    // would read exactly 20.f instead of pane0_w0 + 20.f.
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0 + 20.f));
}

TEST_CASE("Dragging clamps at the minimum pane size") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    [[maybe_unused]] auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];

    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    // Drag far past pane0's minimum.
    tree.update_split_drag(anchor - 10000.f);

    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(splitter::min_pane_size_px));
}

struct ThreePaneModel {
    Field<int> a{0}, b{0}, c{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
            vb.handle();
            vb.widget(c);
        });
    }
};

TEST_CASE("Dragging one handle in a 3-pane row leaves the far pane untouched") {
    ThreePaneModel model;
    WidgetTree tree(model);
    // Window sized to exactly fit 3 fixed-width (200px) panes + 2 handles
    // (6px each) = 612 -- an exact fit, like the other fixed-pane tests in
    // this file, so the Task 6 resize-rescale in update_split_state doesn't
    // spuriously fire from a pre-existing content/container mismatch that
    // predates any drag.
    auto snap = tree.build_snapshot(612, 100, 1);
    REQUIRE(snap->geometry.size() == 5);

    [[maybe_unused]] auto [id0, r0] = snap->geometry[0];
    auto [hid0, hr0] = snap->geometry[1];
    [[maybe_unused]] auto [id1, r1] = snap->geometry[2];
    [[maybe_unused]] auto [hid1, hr1] = snap->geometry[3];
    auto [id2, r2] = snap->geometry[4];
    float w2_before = r2.extent.w.raw();

    float anchor = hr0.origin.x.raw();
    tree.begin_split_drag(model.container_id, /*handle_index=*/0, anchor);
    tree.update_split_drag(anchor + 15.f);

    auto snap2 = tree.build_snapshot(612, 100, 2);
    auto [id2b, r2b] = snap2->geometry[4];
    CHECK(r2b.extent.w.raw() == doctest::Approx(w2_before));
}

struct ExpandingPaneModel {
    Field<Slider<double>> s{{.value = 0.5}};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(s);   // Widget<Slider<>>::expand_axis == Horizontal -- expand=true here
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("Pressing and dragging the handle through the real input pipeline resizes panes") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(snap->geometry.size() == 3);

    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto [pane1_id, pane1_rect] = snap->geometry[2];
    float pane0_w0 = pane0_rect.extent.w.raw();
    float pane1_w0 = pane1_rect.extent.w.raw();

    Point press_pos{X{handle_rect.origin.x.raw() + 2.f}, Y{handle_rect.origin.y.raw() + 5.f}};
    auto hit = prism::hit_test(*snap, press_pos);
    REQUIRE(hit.has_value());
    CHECK(*hit == handle_id);

    MouseButton press{press_pos, /*button=*/1, /*pressed=*/true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);
    CHECK(tree.captured_id() == handle_id);

    // Two separate MouseMove events, each a real 10px rightward mouse movement.
    // This specifically guards the absolute-position fix described in this
    // task: if the handle's own (moving) rect were used to localize
    // subsequent events instead of reconstructing absolute position, the
    // second move would not add another clean 10px -- it would desync.
    MouseMove move1{Point{X{press_pos.x.raw() + 10.f}, press_pos.y}};
    prism::app::detail::route_mouse_move(tree, *snap, move1);
    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0 + 10.f));

    MouseMove move2{Point{X{press_pos.x.raw() + 20.f}, press_pos.y}};
    prism::app::detail::route_mouse_move(tree, *snap2, move2);
    auto snap3 = tree.build_snapshot(406, 100, 3);
    auto [pane0_id3, pane0_rect3] = snap3->geometry[0];
    CHECK(pane0_rect3.extent.w.raw() == doctest::Approx(pane0_w0 + 20.f));

    MouseButton release{Point{X{press_pos.x.raw() + 20.f}, press_pos.y}, 1, false};
    InputEvent release_ev{release};
    prism::app::detail::route_mouse_button(tree, *snap3, release_ev, release);
    CHECK(tree.captured_id() == 0);

    auto snap4 = tree.build_snapshot(406, 100, 4);
    auto [pane1_id4, pane1_rect4] = snap4->geometry[2];
    CHECK(pane1_rect4.extent.w.raw() == doctest::Approx(pane1_w0 - 20.f));
}

TEST_CASE("First drag captures an expanding pane's real allocated size, not a fallback") {
    ExpandingPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(500, 100, 1);
    REQUIRE(snap->geometry.size() == 3);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    float pane0_w0 = pane0_rect.extent.w.raw();
    // The slider pane expands to fill the row minus the fixed-width int pane
    // and the handle -- confirm it is NOT the 200px default_widget_w fallback.
    CHECK(pane0_w0 > 200.f);

    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    tree.update_split_drag(anchor); // no movement -- just engage

    auto snap2 = tree.build_snapshot(500, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    // Engaging with zero drag delta must reproduce the exact pre-drag width --
    // proves the captured size came from the real expand-filled allocation,
    // not some smaller content-bbox fallback that would visibly snap the pane
    // narrower the instant a drag starts.
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0));
}

TEST_CASE("Resizing the window after a drag proportionally rescales pane sizes on the next frame") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    [[maybe_unused]] auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];

    // Engage split mode with an uneven split: pane0 grows by 100px.
    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    tree.update_split_drag(anchor + 100.f);
    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    auto [pane1_id2, pane1_rect2] = snap2->geometry[2];
    float ratio_before = pane0_rect2.extent.w.raw()
        / (pane0_rect2.extent.w.raw() + pane1_rect2.extent.w.raw());

    // Double the window width. The rescale is documented as taking effect on
    // the frame *after* the mismatch is observed (same "detect now, correct
    // next frame" pattern already used for table/vlist viewport sizing).
    auto snap3 = tree.build_snapshot(812, 100, 3);
    auto snap4 = tree.build_snapshot(812, 100, 4);
    auto [pane0_id4, pane0_rect4] = snap4->geometry[0];
    auto [pane1_id4, pane1_rect4] = snap4->geometry[2];
    float ratio_after = pane0_rect4.extent.w.raw()
        / (pane0_rect4.extent.w.raw() + pane1_rect4.extent.w.raw());

    CHECK(ratio_after == doctest::Approx(ratio_before).epsilon(0.02));
    CHECK(pane0_rect4.extent.w.raw() + pane1_rect4.extent.w.raw()
          == doctest::Approx(812.f - splitter::thickness_px).epsilon(0.02));
}
