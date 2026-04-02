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
