#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/layout.hpp>

TEST_CASE("leaf node measure uses draw list bounding box") {
    prism::LayoutNode leaf;
    leaf.kind = prism::LayoutNode::Kind::Leaf;
    leaf.draws.filled_rect({0, 0, 120, 40}, prism::Color::rgba(255, 0, 0));

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
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

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
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

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
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 400, 300});

    CHECK(row.allocated.x == 0);
    CHECK(row.allocated.w == 400);
    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 100);
    CHECK(row.children[0].allocated.h == 300);  // stretch cross-axis
    CHECK(row.children[1].allocated.x == 100);
    CHECK(row.children[1].allocated.w == 60);
    CHECK(row.children[1].allocated.h == 300);
}

TEST_CASE("arrange row with spacer distributes remaining space") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 500, 200});

    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 100);
    CHECK(row.children[1].allocated.x == 100);
    CHECK(row.children[1].allocated.w == 300);  // 500 - 100 - 100
    CHECK(row.children[2].allocated.x == 400);
    CHECK(row.children[2].allocated.w == 100);
}

TEST_CASE("arrange column distributes height to children") {
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 60}, prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);
    prism::layout_arrange(col, {10, 20, 300, 400});

    CHECK(col.allocated.x == 10);
    CHECK(col.allocated.y == 20);
    CHECK(col.children[0].allocated.x == 10);
    CHECK(col.children[0].allocated.y == 20);
    CHECK(col.children[0].allocated.w == 300);  // stretch cross-axis
    CHECK(col.children[0].allocated.h == 40);
    CHECK(col.children[1].allocated.x == 10);
    CHECK(col.children[1].allocated.y == 60);   // 20 + 40
    CHECK(col.children[1].allocated.h == 60);
}

TEST_CASE("arrange nested: column inside row") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode left;
    left.kind = prism::LayoutNode::Kind::Leaf;
    left.draws.filled_rect({0, 0, 200, 100}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode ca;
    ca.kind = prism::LayoutNode::Kind::Leaf;
    ca.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(0, 0, 255));

    prism::LayoutNode cb;
    cb.kind = prism::LayoutNode::Kind::Leaf;
    cb.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(ca));
    col.children.push_back(std::move(cb));

    row.children.push_back(std::move(left));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(col));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 800, 600});

    // left: 200px, spacer: 800-200-100=500, col: 100px
    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 200);
    CHECK(row.children[1].allocated.x == 200);
    CHECK(row.children[1].allocated.w == 500);
    CHECK(row.children[2].allocated.x == 700);
    CHECK(row.children[2].allocated.w == 100);
    // column children within the column
    CHECK(row.children[2].children[0].allocated.x == 700);
    CHECK(row.children[2].children[0].allocated.y == 0);
    CHECK(row.children[2].children[0].allocated.h == 40);
    CHECK(row.children[2].children[1].allocated.y == 40);
    CHECK(row.children[2].children[1].allocated.h == 40);
}
