#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/fixed_string.hpp>

TEST_CASE("fixed_string stores a string literal and exposes it as string_view") {
    constexpr prism::core::fixed_string fs{"Custom Name"};
    static_assert(fs.view() == "Custom Name");
    CHECK(fs.view() == "Custom Name");
}

TEST_CASE("fixed_string is usable as a template non-type parameter") {
    // If this compiles, NTTP deduction from a string literal works end to end.
    constexpr auto make = []<prism::core::fixed_string S>() { return S.view(); };
    CHECK(make.operator()<prism::core::fixed_string{"abc"}>() == "abc");
}
