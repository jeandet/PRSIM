#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/widget_tree.hpp>


TEST_CASE("TabBar default constructs with selected=0") {
    prism::TabBar tb;
    CHECK(tb.selected == 0);
}

TEST_CASE("TabBar equality") {
    prism::TabBar a{.selected = 0};
    prism::TabBar b{.selected = 1};
    CHECK(a == a);
    CHECK(a != b);
}

TEST_CASE("TabBarEditState default constructs") {
    prism::TabBarEditState es;
    CHECK(!es.hovered_tab.has_value());
    CHECK(es.header_x_ranges.empty());
}

TEST_CASE("LayoutKind::Tabs exists") {
    auto kind = prism::LayoutKind::Tabs;
    CHECK(kind != prism::LayoutKind::Default);
}
