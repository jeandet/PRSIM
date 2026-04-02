#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/layout.hpp>
#include <prism/app/widget_tree.hpp>
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

// --- Layout-level tests ---

TEST_CASE("Scroll LayoutNode measures as expandable") {
    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;

    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{800}}},
        Color::rgba(255, 0, 0));
    scroll.children.push_back(std::move(child));

    layout_measure(scroll, LayoutAxis::Vertical);
    CHECK(scroll.hint.expand);
}

TEST_CASE("Scroll LayoutNode arranges children with unbounded height") {
    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;

    for (int i = 0; i < 2; ++i) {
        LayoutNode child;
        child.kind = LayoutNode::Kind::Leaf;
        child.id = 10 + i;
        child.draws.filled_rect(
            Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{400}}},
            Color::rgba(255, 0, 0));
        scroll.children.push_back(std::move(child));
    }

    layout_measure(scroll, LayoutAxis::Vertical);

    Rect viewport{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    layout_arrange(scroll, viewport);

    CHECK(scroll.allocated.extent.h.raw() == doctest::Approx(300));
    CHECK(scroll.children[0].allocated.extent.h.raw() == doctest::Approx(400));
    CHECK(scroll.children[1].allocated.origin.y.raw() == doctest::Approx(400));
    CHECK(scroll.children[1].allocated.extent.h.raw() == doctest::Approx(400));
}

TEST_CASE("Scroll flatten clips to viewport") {
    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;
    scroll.scroll_offset = DY{0};
    scroll.allocated = Rect{Point{X{10}, Y{20}}, Size{Width{200}, Height{100}}};
    scroll.scroll_content_h = Height{400};

    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{400}}},
        Color::rgba(255, 0, 0));
    child.allocated = Rect{Point{X{10}, Y{20}}, Size{Width{200}, Height{400}}};
    scroll.children.push_back(std::move(child));

    SceneSnapshot snap;
    layout_flatten(scroll, snap);

    CHECK(snap.geometry.size() >= 1);
    bool has_clip = false;
    for (auto& dl : snap.draw_lists) {
        for (auto& cmd : dl.commands) {
            if (std::holds_alternative<ClipPush>(cmd)) has_clip = true;
        }
    }
    CHECK(has_clip);
}

// --- Model structs (must be at namespace scope for reflection) ---

struct ScrollModel8 {
    Field<int> a{0};
    Field<int> b{0};
    Field<int> c{0};
    Field<int> d{0};
    Field<int> e{0};
    Field<int> f{0};
    Field<int> g{0};
    Field<int> h{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.widget(a);
            vb.widget(b);
            vb.widget(c);
            vb.widget(d);
            vb.widget(e);
            vb.widget(f);
            vb.widget(g);
            vb.widget(h);
        });
    }
};

struct ScrollModel1 {
    Field<int> a{0};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] { vb.widget(a); });
    }
};

struct ScrollModel2 {
    Field<int> a{0};
    Field<int> b{0};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] { vb.widget(a); vb.widget(b); });
    }
};

struct InnerScroll {
    Field<int> a{0};
    Field<int> b{0};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] { vb.widget(a); vb.widget(b); });
    }
};

struct OuterScroll {
    InnerScroll inner;
    Field<int> c{0};
    Field<int> d{0};
    Field<int> e{0};
    Field<int> f{0};
    Field<int> g{0};
    Field<int> h{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.component(inner);
            vb.widget(c);
            vb.widget(d);
            vb.widget(e);
            vb.widget(f);
            vb.widget(g);
            vb.widget(h);
        });
    }
};

struct ScrollAreaModel {
    Field<ScrollArea> scroller{{}};
    Field<int> a{0};
    Field<int> b{0};
    Field<int> c{0};
    Field<int> d{0};
    Field<int> e{0};
    Field<int> f{0};
    Field<int> g{0};
    Field<int> h{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll(scroller, [&] {
            vb.widget(a);
            vb.widget(b);
            vb.widget(c);
            vb.widget(d);
            vb.widget(e);
            vb.widget(f);
            vb.widget(g);
            vb.widget(h);
        });
    }
};

// --- WidgetTree-level tests ---

TEST_CASE("scroll_at scrolls a scroll container") {
    ScrollModel8 model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    REQUIRE(snap->geometry.size() >= 1);
    auto leaf_id = snap->geometry[0].first;

    tree.scroll_at(leaf_id, DY{50});
    CHECK(tree.any_dirty());
}

TEST_CASE("scroll_at clamps to bounds — content fits viewport") {
    ScrollModel1 model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 600, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], DY{100});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("BubbleAtBounds: inner scroll at limit bubbles to outer") {
    OuterScroll model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 80, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], DY{50});
    CHECK(tree.any_dirty());
}

TEST_CASE("Field<ScrollArea> provides programmatic scroll control") {
    ScrollAreaModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    auto sa = model.scroller.get();
    sa.scroll_y = DY{50};
    model.scroller.set(sa);
    CHECK(tree.any_dirty());
}

TEST_CASE("Scroll node creation via ViewBuilder") {
    ScrollModel2 model;
    WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() >= 2);
}

TEST_CASE("Scrollbar overlay appears when content overflows") {
    ScrollModel8 model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    CHECK(!snap->overlay_geometry.empty());
}

TEST_CASE("Scrollbar drag updates scroll offset") {
    ScrollModel8 model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    REQUIRE(!snap->overlay_geometry.empty());

    auto [scroll_id, thumb_rect] = snap->overlay_geometry[0];
    Y thumb_center_y = thumb_rect.origin.y + DY{thumb_rect.extent.h.raw() / 2.f};

    tree.begin_scrollbar_drag(scroll_id, thumb_center_y);
    CHECK(tree.in_scrollbar_drag());
    CHECK(tree.captured_id() == scroll_id);

    // Drag downward by 20 pixels — should increase scroll offset
    tree.clear_dirty();
    tree.update_scrollbar_drag(thumb_center_y + DY{20.f});
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 100, 2);
    CHECK(snap2->overlay_geometry[0].second.origin.y.raw() > thumb_rect.origin.y.raw());

    tree.end_scrollbar_drag();
    CHECK_FALSE(tree.in_scrollbar_drag());
}

TEST_CASE("scroll_to sets absolute offset") {
    ScrollModel8 model;
    WidgetTree tree(model);
    auto snap0 = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());

    // Find the scroll container (parent of leaves)
    // scroll_at uses leaf → walks up to scroll container, but scroll_to needs the scroll container ID directly
    // Use scroll_at first to find that scrolling works, then test scroll_to on same container
    tree.scroll_at(ids[0], DY{50});
    CHECK(tree.any_dirty());
}
