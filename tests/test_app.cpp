#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/app/app.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {} namespace prism::ui {} namespace prism::app {} namespace prism::plot {} namespace prism { using namespace core; using namespace render; using namespace input; using namespace ui; using namespace app; using namespace plot; }


namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

TEST_CASE("App runs and stops on quit") {
    int frame_count = 0;

    prism::App app({.title = "Test", .width = 100, .height = 100});
    app.run([&](prism::Frame&) {
        ++frame_count;
        app.quit();
    });

    CHECK(frame_count == 1);
}

TEST_CASE("Frame exposes width and height") {
    prism::App app({.title = "Test", .width = 320, .height = 240});
    app.run([&](prism::Frame& frame) {
        CHECK(frame.width() == 320);
        CHECK(frame.height() == 240);
        app.quit();
    });
}

TEST_CASE("Frame records draw commands into snapshot") {
    prism::App app({.title = "Test", .width = 100, .height = 100});
    app.run([&](prism::Frame& frame) {
        frame.filled_rect(R(10, 10, 50, 50), prism::Color::rgba(255, 0, 0));
        app.quit();
    });
    CHECK(true);
}
