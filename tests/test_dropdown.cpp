#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

enum class Color { Red, Green, Blue };
enum class Size { Small, Medium, Large, XLarge };

namespace {
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

TEST_CASE("Enum delegate record produces background and current label") {
    prism::Field<Color> field{Color::Green};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);

    CHECK_FALSE(dl.empty());
    bool has_label = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Green") has_label = true;
        }
    }
    CHECK(has_label);
}

TEST_CASE("Enum delegate record shows arrow indicator") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);

    bool has_arrow = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "\xe2\x96\xbe") has_arrow = true;  // ▾
        }
    }
    CHECK(has_arrow);
}

TEST_CASE("Enum delegate renders focus ring when focused") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Enum delegate open popup produces overlay draws") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open the dropdown
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{{10, 10}, 1, true}, node);

    // Re-record to populate overlay
    prism::DrawList dl;
    node.overlay_draws.clear();
    prism::Delegate<Color>::record(dl, field, node);

    CHECK_FALSE(node.overlay_draws.empty());

    // Should have text for each enum option
    int option_count = 0;
    for (auto& cmd : node.overlay_draws.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Red" || t->text == "Green" || t->text == "Blue")
                option_count++;
        }
    }
    CHECK(option_count == 3);
}

TEST_CASE("Enum delegate closed popup has no overlay draws") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);
    CHECK(node.overlay_draws.empty());
}
