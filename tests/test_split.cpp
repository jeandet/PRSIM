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
