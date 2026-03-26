#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/core/input_event.hpp>

TEST_CASE("InputEvent variant holds WindowClose") {
    prism::InputEvent ev = prism::WindowClose{};
    CHECK(std::holds_alternative<prism::WindowClose>(ev));
}

TEST_CASE("InputEvent variant holds WindowResize") {
    prism::InputEvent ev = prism::WindowResize{.width = 1024, .height = 768};
    auto& resize = std::get<prism::WindowResize>(ev);
    CHECK(resize.width == 1024);
    CHECK(resize.height == 768);
}

TEST_CASE("InputEvent variant holds MouseMove") {
    prism::InputEvent ev = prism::MouseMove{.position = {10.0f, 20.0f}};
    auto& move = std::get<prism::MouseMove>(ev);
    CHECK(move.position.x == doctest::Approx(10.0f));
    CHECK(move.position.y == doctest::Approx(20.0f));
}
