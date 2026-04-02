#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/window.hpp>
#include <prism/app/headless_window.hpp>

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

TEST_CASE("HeadlessWindow stores creation config") {
    prism::HeadlessWindow w(1, {.title = "Test", .width = 320, .height = 240,
                                .resizable = false, .fullscreen = true,
                                .decoration = prism::DecorationMode::Custom});
    CHECK(w.id() == 1);
    auto [ww, hh] = w.size();
    CHECK(ww == 320);
    CHECK(hh == 240);
    CHECK(w.is_resizable() == false);
    CHECK(w.is_fullscreen() == true);
    CHECK(w.decoration_mode() == prism::DecorationMode::Custom);
}

TEST_CASE("HeadlessWindow set_title updates title") {
    prism::HeadlessWindow w(1, {});
    w.set_title("New Title");
}

TEST_CASE("HeadlessWindow set_size updates size") {
    prism::HeadlessWindow w(1, {.width = 100, .height = 100});
    w.set_size(640, 480);
    auto [ww, hh] = w.size();
    CHECK(ww == 640);
    CHECK(hh == 480);
}

TEST_CASE("HeadlessWindow position defaults to 0,0") {
    prism::HeadlessWindow w(1, {});
    auto [x, y] = w.position();
    CHECK(x == 0);
    CHECK(y == 0);
}

TEST_CASE("HeadlessWindow set_position updates position") {
    prism::HeadlessWindow w(1, {});
    w.set_position(100, 200);
    auto [x, y] = w.position();
    CHECK(x == 100);
    CHECK(y == 200);
}

TEST_CASE("HeadlessWindow set_decoration_mode") {
    prism::HeadlessWindow w(1, {});
    CHECK(w.decoration_mode() == prism::DecorationMode::Native);
    w.set_decoration_mode(prism::DecorationMode::None);
    CHECK(w.decoration_mode() == prism::DecorationMode::None);
}

TEST_CASE("HeadlessWindow fullscreen toggle") {
    prism::HeadlessWindow w(1, {});
    CHECK(w.is_fullscreen() == false);
    w.set_fullscreen(true);
    CHECK(w.is_fullscreen() == true);
}

TEST_CASE("HeadlessWindow state methods are no-ops") {
    prism::HeadlessWindow w(1, {});
    w.minimize();
    w.maximize();
    w.restore();
    w.show();
    w.hide();
    w.close();
}
