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

struct TwoPaneRowModel {
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

struct TwoPaneColumnModel {
    Field<int> a{0};
    Field<int> b{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

struct BareHandleModel {
    Field<int> a{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(a);
            vb.handle();
        });
    }
};

TEST_CASE("desired_cursor is Default when nothing is hovered or captured") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    CHECK(tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("Hovering a Handle in an hstack reports ResizeEW") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);
}

TEST_CASE("Hovering a Handle in a vstack reports ResizeNS") {
    TwoPaneColumnModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(200, 66, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::ResizeNS);
}

TEST_CASE("A bare, unwired handle() outside a split container has no cursor override") {
    BareHandleModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(300, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("Capturing a Handle keeps its resize cursor even after the mouse leaves its rect") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto handle_center = handle_rect.center();

    MouseButton press{handle_center, 1, true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);
    REQUIRE(tree.captured_id() == handle_id);
    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);

    MouseMove move{Point{X{handle_center.x.raw() + 60.f}, handle_center.y}};
    prism::app::detail::route_mouse_move(tree, *snap, move);
    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);
}

TEST_CASE("Releasing a captured Handle off its rect falls back to whatever is now hovered") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto handle_center = handle_rect.center();

    MouseButton press{handle_center, 1, true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);

    Point release_pos{pane0_rect.center()};
    MouseMove move{release_pos};
    prism::app::detail::route_mouse_move(tree, *snap, move);
    MouseButton release{release_pos, 1, false};
    InputEvent release_ev{release};
    prism::app::detail::route_mouse_button(tree, *snap, release_ev, release);

    REQUIRE(tree.captured_id() == 0);
    CHECK(tree.desired_cursor() == CursorShape::Default);
}
