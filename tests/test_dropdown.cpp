#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

enum class Color { Red, Green, Blue };
enum class Size { Small, Medium, Large, XLarge };

namespace {
[[maybe_unused]]
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("ScopedEnum concept matches scoped enums") {
    static_assert(prism::ScopedEnum<Color>);
    static_assert(prism::ScopedEnum<Size>);
    static_assert(!prism::ScopedEnum<int>);
    static_assert(!prism::ScopedEnum<std::string>);
}

TEST_CASE("enum_count returns number of enumerators") {
    CHECK(prism::enum_count<Color>() == 3);
    CHECK(prism::enum_count<Size>() == 4);
}

TEST_CASE("enum_label returns enumerator name") {
    CHECK(prism::enum_label<Color>(0) == "Red");
    CHECK(prism::enum_label<Color>(1) == "Green");
    CHECK(prism::enum_label<Color>(2) == "Blue");
}

TEST_CASE("enum_index returns index for enum value") {
    CHECK(prism::enum_index(Color::Red) == 0);
    CHECK(prism::enum_index(Color::Green) == 1);
    CHECK(prism::enum_index(Color::Blue) == 2);
}

TEST_CASE("enum_from_index returns enum value for index") {
    CHECK(prism::enum_from_index<Color>(0) == Color::Red);
    CHECK(prism::enum_from_index<Color>(1) == Color::Green);
    CHECK(prism::enum_from_index<Color>(2) == Color::Blue);
}

TEST_CASE("enum_index and enum_from_index round-trip") {
    for (size_t i = 0; i < prism::enum_count<Size>(); ++i) {
        CHECK(prism::enum_index(prism::enum_from_index<Size>(i)) == i);
    }
}

TEST_CASE("Dropdown sentinel default-constructs") {
    prism::Dropdown<Color> dd;
    CHECK(dd.value == Color::Red);
    CHECK(dd.labels.empty());
}

TEST_CASE("Dropdown sentinel equality") {
    prism::Dropdown<Color> a{.value = Color::Green};
    prism::Dropdown<Color> b{.value = Color::Green};
    prism::Dropdown<Color> c{.value = Color::Blue};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("DropdownEditState defaults to closed") {
    prism::DropdownEditState es;
    CHECK(es.open == false);
    CHECK(es.highlighted == 0);
}

TEST_CASE("Delegate<ScopedEnum> has tab_and_click focus policy") {
    CHECK(prism::Delegate<Color>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<Dropdown<T>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::Dropdown<Color>>::focus_policy == prism::FocusPolicy::tab_and_click);
}
