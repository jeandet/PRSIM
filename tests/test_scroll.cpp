#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/layout.hpp>
#include <prism/core/widget_tree.hpp>

using namespace prism;

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
