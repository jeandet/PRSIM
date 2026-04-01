#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/context.hpp>

TEST_CASE("default_theme returns consistent values") {
    auto t = prism::default_theme();
    CHECK(t.surface.r == 45);
    CHECK(t.surface.g == 45);
    CHECK(t.surface.b == 55);
    CHECK(t.primary.r == 40);
    CHECK(t.primary.g == 105);
    CHECK(t.primary.b == 180);
    CHECK(t.focus_ring.r == 80);
    CHECK(t.focus_ring.g == 160);
    CHECK(t.focus_ring.b == 240);
    CHECK(t.accent.r == 0);
    CHECK(t.accent.g == 140);
    CHECK(t.accent.b == 200);
}

TEST_CASE("Theme is copy-constructible and modifiable") {
    auto t = prism::default_theme();
    t.primary = prism::Color::rgba(255, 0, 0);
    CHECK(t.primary.r == 255);
    auto t2 = prism::default_theme();
    CHECK(t2.primary.r == 40);
}
