#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/render/pixel_buffer.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {} namespace prism::ui {} namespace prism::app {} namespace prism::plot {} namespace prism { using namespace core; using namespace render; using namespace input; using namespace ui; using namespace app; using namespace plot; }


namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

TEST_CASE("PixelBuffer starts cleared to black") {
    prism::PixelBuffer buf(4, 3);
    CHECK(buf.width() == 4);
    CHECK(buf.height() == 3);
    CHECK(buf.pixel(0, 0) == 0xFF000000); // opaque black (ARGB8888)
}

TEST_CASE("fill_rect writes correct pixels") {
    prism::PixelBuffer buf(10, 10);
    // Fill a 3x2 rect at (2,1) with red
    buf.fill_rect(R(2.0f, 1.0f, 3.0f, 2.0f), prism::Color::rgba(255, 0, 0));

    // Inside the rect
    uint32_t expected = 0xFFFF0000; // ARGB: A=FF, R=FF, G=00, B=00
    CHECK(buf.pixel(2, 1) == expected);
    CHECK(buf.pixel(4, 2) == expected);

    // Outside the rect
    CHECK(buf.pixel(0, 0) == 0xFF000000);
    CHECK(buf.pixel(5, 1) == 0xFF000000);
}

TEST_CASE("fill_rect clips to buffer bounds") {
    prism::PixelBuffer buf(5, 5);
    // Rect extends beyond buffer
    buf.fill_rect(R(3.0f, 3.0f, 10.0f, 10.0f), prism::Color::rgba(0, 0, 255));

    uint32_t expected = 0xFF0000FF; // ARGB
    CHECK(buf.pixel(3, 3) == expected);
    CHECK(buf.pixel(4, 4) == expected);
    CHECK(buf.pixel(2, 2) == 0xFF000000); // outside
}

TEST_CASE("clear resets all pixels") {
    prism::PixelBuffer buf(4, 4);
    buf.fill_rect(R(0, 0, 4, 4), prism::Color::rgba(255, 255, 255));
    buf.clear();
    CHECK(buf.pixel(0, 0) == 0xFF000000);
}
