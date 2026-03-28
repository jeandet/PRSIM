#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

TEST_CASE("TextField default-constructs with empty value") {
    prism::TextField<> tf;
    CHECK(tf.value.empty());
    CHECK(tf.placeholder.empty());
    CHECK(tf.max_length == 0);
}

TEST_CASE("TextField equality comparison") {
    prism::TextField<> a{.value = "hello"};
    prism::TextField<> b{.value = "hello"};
    prism::TextField<> c{.value = "world"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches TextField") {
    static_assert(prism::TextEditable<prism::TextField<>>);
    static_assert(!prism::TextEditable<std::string>);
    static_assert(!prism::TextEditable<int>);
}

TEST_CASE("char_width returns positive value") {
    CHECK(prism::char_width(14.f) > 0.f);
    CHECK(prism::char_width(14.f) == doctest::Approx(0.6f * 14.f));
}

TEST_CASE("Delegate<TextField<>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::TextField<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}
