#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>

TEST_CASE("WidgetNode has layout_kind defaulting to Default") {
    prism::WidgetNode node;
    CHECK(node.layout_kind == prism::WidgetNode::LayoutKind::Default);
}
