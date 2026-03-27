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
