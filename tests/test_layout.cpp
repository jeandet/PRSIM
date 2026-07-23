#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/layout.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

TEST_CASE("leaf node measure uses draw list bounding box") {
    prism::LayoutNode leaf;
    leaf.kind = prism::LayoutNode::Kind::Leaf;
    leaf.draws.filled_rect(R(0, 0, 120, 40), prism::Color::rgba(255, 0, 0));

    prism::layout_measure(leaf, prism::LayoutAxis::Horizontal);

    CHECK(leaf.hint.preferred == 120);
    CHECK(leaf.hint.cross == 40);
}

TEST_CASE("spacer has expand=true and zero preferred") {
    prism::LayoutNode spacer;
    spacer.kind = prism::LayoutNode::Kind::Spacer;

    prism::layout_measure(spacer, prism::LayoutAxis::Horizontal);

    CHECK(spacer.hint.preferred == 0);
    CHECK(spacer.hint.expand == true);
}

TEST_CASE("row measure sums children preferred widths") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect(R(0, 0, 60, 80), prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);

    CHECK(row.hint.preferred == 160);  // 100 + 60
    CHECK(row.hint.cross == 80);       // max(50, 80)
}

TEST_CASE("column measure sums children preferred heights") {
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect(R(0, 0, 60, 80), prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);

    CHECK(col.hint.preferred == 130);  // 50 + 80
    CHECK(col.hint.cross == 100);      // max(100, 60)
}

TEST_CASE("arrange row distributes width to children") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect(R(0, 0, 60, 80), prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(0, 0, 400, 300));

    CHECK(row.allocated.origin.x.raw() == 0);
    CHECK(row.allocated.extent.w.raw() == 400);
    CHECK(row.children[0].allocated.origin.x.raw() == 0);
    CHECK(row.children[0].allocated.extent.w.raw() == 100);
    CHECK(row.children[0].allocated.extent.h.raw() == 300);  // stretch cross-axis
    CHECK(row.children[1].allocated.origin.x.raw() == 100);
    CHECK(row.children[1].allocated.extent.w.raw() == 60);
    CHECK(row.children[1].allocated.extent.h.raw() == 300);
}

TEST_CASE("arrange row with spacer distributes remaining space") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(0, 0, 500, 200));

    CHECK(row.children[0].allocated.origin.x.raw() == 0);
    CHECK(row.children[0].allocated.extent.w.raw() == 100);
    CHECK(row.children[1].allocated.origin.x.raw() == 100);
    CHECK(row.children[1].allocated.extent.w.raw() == 300);  // 500 - 100 - 100
    CHECK(row.children[2].allocated.origin.x.raw() == 400);
    CHECK(row.children[2].allocated.extent.w.raw() == 100);
}

TEST_CASE("arrange column distributes height to children") {
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect(R(0, 0, 100, 40), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect(R(0, 0, 60, 60), prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);
    prism::layout_arrange(col, R(10, 20, 300, 400));

    CHECK(col.allocated.origin.x.raw() == 10);
    CHECK(col.allocated.origin.y.raw() == 20);
    CHECK(col.children[0].allocated.origin.x.raw() == 10);
    CHECK(col.children[0].allocated.origin.y.raw() == 20);
    CHECK(col.children[0].allocated.extent.w.raw() == 300);  // stretch cross-axis
    CHECK(col.children[0].allocated.extent.h.raw() == 40);
    CHECK(col.children[1].allocated.origin.x.raw() == 10);
    CHECK(col.children[1].allocated.origin.y.raw() == 60);   // 20 + 40
    CHECK(col.children[1].allocated.extent.h.raw() == 60);
}

TEST_CASE("arrange nested: column inside row") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode left;
    left.kind = prism::LayoutNode::Kind::Leaf;
    left.draws.filled_rect(R(0, 0, 200, 100), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode ca;
    ca.kind = prism::LayoutNode::Kind::Leaf;
    ca.draws.filled_rect(R(0, 0, 100, 40), prism::Color::rgba(0, 0, 255));

    prism::LayoutNode cb;
    cb.kind = prism::LayoutNode::Kind::Leaf;
    cb.draws.filled_rect(R(0, 0, 100, 40), prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(ca));
    col.children.push_back(std::move(cb));

    row.children.push_back(std::move(left));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(col));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(0, 0, 800, 600));

    // left: 200px, spacer: 800-200-100=500, col: 100px
    CHECK(row.children[0].allocated.origin.x.raw() == 0);
    CHECK(row.children[0].allocated.extent.w.raw() == 200);
    CHECK(row.children[1].allocated.origin.x.raw() == 200);
    CHECK(row.children[1].allocated.extent.w.raw() == 500);
    CHECK(row.children[2].allocated.origin.x.raw() == 700);
    CHECK(row.children[2].allocated.extent.w.raw() == 100);
    // column children within the column
    CHECK(row.children[2].children[0].allocated.origin.x.raw() == 700);
    CHECK(row.children[2].children[0].allocated.origin.y.raw() == 0);
    CHECK(row.children[2].children[0].allocated.extent.h.raw() == 40);
    CHECK(row.children[2].children[1].allocated.origin.y.raw() == 40);
    CHECK(row.children[2].children[1].allocated.extent.h.raw() == 40);
}

TEST_CASE("flatten produces per-widget geometry and draw lists") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.id = 2;
    b.draws.filled_rect(R(0, 0, 60, 80), prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(0, 0, 400, 300));

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    // Two leaf nodes with draw commands
    CHECK(snap.geometry.size() == 2);
    CHECK(snap.draw_lists.size() == 2);
    CHECK(snap.z_order.size() == 2);

    CHECK(snap.geometry[0].first == 1);
    CHECK(snap.geometry[0].second.origin.x.raw() == 0);
    CHECK(snap.geometry[0].second.extent.w.raw() == 100);

    CHECK(snap.geometry[1].first == 2);
    CHECK(snap.geometry[1].second.origin.x.raw() == 100);
    CHECK(snap.geometry[1].second.extent.w.raw() == 60);
}

TEST_CASE("flatten translates draw commands to absolute coordinates") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    row.children.push_back(std::move(a));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(30, 40, 400, 300));

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    CHECK(snap.geometry.size() == 1);
    CHECK(snap.geometry[0].second.origin.x.raw() == 30);
    CHECK(snap.geometry[0].second.origin.y.raw() == 40);

    // The draw commands are: ClipPush (allocation clip), FilledRect, ClipPop
    auto& dl = snap.draw_lists[0];
    CHECK(dl.commands.size() == 3);
    auto* fr = std::get_if<prism::FilledRect>(&dl.commands[1]);
    REQUIRE(fr != nullptr);
    CHECK(fr->rect.origin.x.raw() == 30);
    CHECK(fr->rect.origin.y.raw() == 40);
}

TEST_CASE("flatten translates Line/Polyline/Circle draw commands to absolute coordinates") {
    // Regression test: translate_draw_list's if-constexpr chain only matched commands with
    // a .rect or .origin member, silently leaving Line/Polyline/Circle at local coordinates
    // when a widget sits below another sibling in a Column (nonzero dy). Found via a real
    // system-monitor app where the 2nd/3rd of three stacked Plot canvases rendered correct
    // (translated) axis-label text but an invisible (untranslated, clipped-away) polyline.
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect(R(0, 0, 100, 40), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.id = 2;
    b.draws.line(prism::Point{prism::X{5}, prism::Y{5}}, prism::Point{prism::X{15}, prism::Y{15}},
                 prism::Color::rgba(0, 255, 0), 1.f);
    b.draws.polyline({prism::Point{prism::X{1}, prism::Y{1}}, prism::Point{prism::X{2}, prism::Y{2}}},
                     prism::Color::rgba(0, 0, 255), 1.f);
    b.draws.circle(prism::Point{prism::X{3}, prism::Y{3}}, 4.f, prism::Color::rgba(255, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);
    prism::layout_arrange(col, R(0, 0, 300, 400));

    // b is stacked below a (height 40), so its allocated origin has a nonzero dy -- the
    // exact condition that exposes the bug (dy=0 for the first child hides it entirely).
    REQUIRE(col.children[1].allocated.origin.y.raw() == 40);

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(col, snap);

    REQUIRE(snap.draw_lists.size() == 2);
    auto& dl = snap.draw_lists[1]; // b's: ClipPush, Line, Polyline, Circle, ClipPop
    REQUIRE(dl.commands.size() == 5);

    auto* line = std::get_if<prism::Line>(&dl.commands[1]);
    REQUIRE(line != nullptr);
    CHECK(line->from.y.raw() == 45);  // 5 + dy(40)
    CHECK(line->to.y.raw() == 55);    // 15 + dy(40)

    auto* poly = std::get_if<prism::Polyline>(&dl.commands[2]);
    REQUIRE(poly != nullptr);
    REQUIRE(poly->points.size() == 2);
    CHECK(poly->points[0].y.raw() == 41);  // 1 + dy(40)
    CHECK(poly->points[1].y.raw() == 42);  // 2 + dy(40)

    auto* circ = std::get_if<prism::Circle>(&dl.commands[3]);
    REQUIRE(circ != nullptr);
    CHECK(circ->center.y.raw() == 43);  // 3 + dy(40)
}

TEST_CASE("flatten skips spacers and empty containers") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect(R(0, 0, 100, 50), prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;
    sp.id = 2;

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(sp));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, R(0, 0, 400, 300));

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    // Only the leaf with draw commands, not the spacer
    CHECK(snap.geometry.size() == 1);
    CHECK(snap.geometry[0].first == 1);
}

TEST_CASE("Handle LayoutNode measures to fixed thickness regardless of content") {
    prism::LayoutNode handle;
    handle.kind = prism::LayoutNode::Kind::Handle;
    handle.id = 1;
    // No draws at all — a Handle's size must not depend on bounding_box().

    prism::layout_measure(handle, prism::LayoutAxis::Horizontal);

    CHECK(handle.hint.preferred == doctest::Approx(prism::splitter::thickness_px));
    CHECK_FALSE(handle.hint.expand);
}

TEST_CASE("Row measure ignores split_sizes when empty (regression pin)") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 1;

    prism::LayoutNode leaf;
    leaf.kind = prism::LayoutNode::Kind::Leaf;
    leaf.id = 2;
    leaf.draws.filled_rect(R(0, 0, 80, 20), prism::Color::rgba(255, 0, 0));
    row.children.push_back(std::move(leaf));

    prism::LayoutNode handle;
    handle.kind = prism::LayoutNode::Kind::Handle;
    handle.id = 3;
    row.children.push_back(std::move(handle));

    prism::LayoutNode leaf2;
    leaf2.kind = prism::LayoutNode::Kind::Leaf;
    leaf2.id = 4;
    leaf2.draws.filled_rect(R(0, 0, 120, 20), prism::Color::rgba(0, 255, 0));
    row.children.push_back(std::move(leaf2));

    prism::layout_measure(row, prism::LayoutAxis::Vertical);

    CHECK(row.children[0].hint.preferred == doctest::Approx(80.f));
    CHECK(row.children[1].hint.preferred == doctest::Approx(prism::splitter::thickness_px));
    CHECK(row.children[2].hint.preferred == doctest::Approx(120.f));
}

TEST_CASE("Row measure uses split_sizes for panes when engaged, ignoring content") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 1;
    row.split_sizes = {150.f, 250.f};

    prism::LayoutNode leaf;
    leaf.kind = prism::LayoutNode::Kind::Leaf;
    leaf.id = 2;
    leaf.draws.filled_rect(R(0, 0, 80, 20), prism::Color::rgba(255, 0, 0));
    row.children.push_back(std::move(leaf));

    prism::LayoutNode handle;
    handle.kind = prism::LayoutNode::Kind::Handle;
    handle.id = 3;
    row.children.push_back(std::move(handle));

    prism::LayoutNode leaf2;
    leaf2.kind = prism::LayoutNode::Kind::Leaf;
    leaf2.id = 4;
    leaf2.draws.filled_rect(R(0, 0, 120, 20), prism::Color::rgba(0, 255, 0));
    row.children.push_back(std::move(leaf2));

    prism::layout_measure(row, prism::LayoutAxis::Vertical);

    CHECK(row.children[0].hint.preferred == doctest::Approx(150.f));
    CHECK(row.children[1].hint.preferred == doctest::Approx(prism::splitter::thickness_px));
    CHECK(row.children[2].hint.preferred == doctest::Approx(250.f));
    CHECK_FALSE(row.children[0].hint.expand);
    CHECK_FALSE(row.children[2].hint.expand);
}
