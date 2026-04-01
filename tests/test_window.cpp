#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/window.hpp>

TEST_CASE("WindowConfig has sensible defaults") {
    prism::WindowConfig cfg;
    CHECK(std::string(cfg.title) == "PRISM");
    CHECK(cfg.width == 800);
    CHECK(cfg.height == 600);
    CHECK(cfg.resizable == true);
    CHECK(cfg.fullscreen == false);
    CHECK(cfg.decoration == prism::DecorationMode::Native);
}

TEST_CASE("RenderConfig defaults to null font path") {
    prism::RenderConfig cfg;
    CHECK(cfg.font_path == nullptr);
}

TEST_CASE("WindowConfig designated initializer override") {
    prism::WindowConfig cfg{.title = "Test", .width = 320, .height = 240, .fullscreen = true};
    CHECK(std::string(cfg.title) == "Test");
    CHECK(cfg.width == 320);
    CHECK(cfg.height == 240);
    CHECK(cfg.fullscreen == true);
    CHECK(cfg.decoration == prism::DecorationMode::Native);
}

TEST_CASE("WindowEvent wraps InputEvent with WindowId") {
    prism::WindowEvent we{.window = 1, .event = prism::WindowClose{}};
    CHECK(we.window == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(we.event));
}

TEST_CASE("DecorationMode enum values") {
    CHECK(prism::DecorationMode::Native != prism::DecorationMode::Custom);
    CHECK(prism::DecorationMode::Custom != prism::DecorationMode::None);
    CHECK(prism::DecorationMode::Native != prism::DecorationMode::None);
}
