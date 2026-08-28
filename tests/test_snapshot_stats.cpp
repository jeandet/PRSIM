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
    for (auto& dl : snap->draw_lists) expected += dl.size();
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
