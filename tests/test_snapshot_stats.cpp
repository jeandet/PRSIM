#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

struct SimpleModel {
    prism::Field<int> count{0};
    prism::Field<std::string> name{"hi"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, name);
    }
};

struct ScrollLeavesModel {
    prism::Field<int> a{0}, b{0}, c{0}, d{0}, e{0}, f{0}, g{0}, h{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.widget(a); vb.widget(b); vb.widget(c); vb.widget(d);
            vb.widget(e); vb.widget(f); vb.widget(g); vb.widget(h);
        });
    }
};

TEST_CASE("build_snapshot reports a non-negative build time") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap->build_time_ms >= 0.0);
}

TEST_CASE("build_snapshot draw_command_count matches the snapshot's actual command totals") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);

    size_t expected = snap->overlay.size();
    for (auto& dl : snap->draw_lists) expected += dl->size();
    CHECK(snap->draw_command_count == expected);
    CHECK(snap->draw_command_count > 0);
}

TEST_CASE("build_snapshot approx_bytes is positive when the scene has content") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap->approx_bytes > 0);
}

TEST_CASE("build_snapshot dirty_widget_count reflects changes since the last publish") {
    SimpleModel model;
    prism::WidgetTree tree(model);

    // Construction records initial draws directly, not through the dirty-tracking
    // path (see WidgetNode::dirty default) -- nothing has "changed" yet.
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK(snap1->dirty_widget_count == 0);
    tree.clear_dirty();

    model.count.set(42);
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2->dirty_widget_count == 1);
    tree.clear_dirty();

    auto snap3 = tree.build_snapshot(800, 600, 3);
    CHECK(snap3->dirty_widget_count == 0);
}

TEST_CASE("build_snapshot reuses a widget's DrawList when neither its content nor its position changed") {
    SimpleModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();
    auto snap2 = tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    REQUIRE(snap1->draw_lists.size() == snap2->draw_lists.size());
    for (size_t i = 0; i < snap1->draw_lists.size(); ++i)
        CHECK(snap1->draw_lists[i].get() == snap2->draw_lists[i].get());
}

TEST_CASE("build_snapshot recomputes only the widget whose content actually changed") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    model.count.set(42);
    auto snap2 = tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    REQUIRE(snap1->draw_lists.size() == snap2->draw_lists.size());
    // `count` is the first field in the vstack -- its draw list is regenerated...
    CHECK(snap1->draw_lists[0].get() != snap2->draw_lists[0].get());
    // ...while `name`, untouched, is reused rather than recopied.
    CHECK(snap1->draw_lists[1].get() == snap2->draw_lists[1].get());
}

TEST_CASE("build_snapshot recomputes a scrolled widget's DrawList even though its content never changed") {
    ScrollLeavesModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 100, 1); // 240px of content, 100px viewport
    tree.clear_dirty();

    REQUIRE(!snap1->geometry.empty());
    auto scroll_id = snap1->geometry[0].first; // clip-push entry -- the scroll container itself
    tree.scroll_at(scroll_id, prism::DY{10});
    // scroll_at only marks the scroll container dirty, never the children it scrolls
    // (see WidgetTree::scroll_at) -- this is exactly the case the rect-based cache
    // invalidation exists for, since a plain dirty check would miss it.
    REQUIRE(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 100, 2);
    tree.clear_dirty();

    REQUIRE(snap1->draw_lists.size() == snap2->draw_lists.size());
    // Leaf `a` (index 1: index 0 is the scroll container's own clip-push entry) stays
    // visible in both frames -- unlike its siblings further down, which scroll out of
    // view -- but its screen position shifts with the scroll offset.
    CHECK(snap1->draw_lists[1].get() != snap2->draw_lists[1].get());
}
