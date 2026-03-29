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
    prism::InputEvent ev = prism::MouseMove{.position = {prism::X{10.0f}, prism::Y{20.0f}}};
    auto& move = std::get<prism::MouseMove>(ev);
    CHECK(move.position.x.raw() == doctest::Approx(10.0f));
    CHECK(move.position.y.raw() == doctest::Approx(20.0f));
}

TEST_CASE("TextInput event holds text") {
    prism::InputEvent ev = prism::TextInput{"abc"};
    auto* ti = std::get_if<prism::TextInput>(&ev);
    REQUIRE(ti != nullptr);
    CHECK(ti->text == "abc");
}

TEST_CASE("Key constants match SDL keycodes") {
    CHECK(prism::keys::backspace == 0x08);
    CHECK(prism::keys::delete_   == 0x7F);
    CHECK(prism::keys::right     == 0x4000'004F);
    CHECK(prism::keys::left      == 0x4000'0050);
    CHECK(prism::keys::home      == 0x4000'004A);
    CHECK(prism::keys::end       == 0x4000'004D);
}

TEST_CASE("Arrow and Escape key constants match SDL keycodes") {
    CHECK(prism::keys::up      == 0x4000'0052);
    CHECK(prism::keys::down    == 0x4000'0051);
    CHECK(prism::keys::escape  == 0x1B);
}
