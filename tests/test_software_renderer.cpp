#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/core/software_renderer.hpp>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

TEST_CASE("render_frame rasterises a FilledRect") {
    prism::SoftwareRenderer renderer(10, 10);

    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, R(2.0f, 1.0f, 3.0f, 2.0f)});
    snap.draw_lists.emplace_back();
    snap.draw_lists[0].filled_rect(R(2.0f, 1.0f, 3.0f, 2.0f), prism::Color::rgba(255, 0, 0));
    snap.z_order.push_back(0);

    renderer.render_frame(snap);
    auto& buf = renderer.buffer();

    uint32_t red = 0xFFFF0000;
    CHECK(buf.pixel(2, 1) == red);
    CHECK(buf.pixel(4, 2) == red);
    CHECK(buf.pixel(0, 0) == 0xFF000000); // background
}

TEST_CASE("render_frame clears before drawing") {
    prism::SoftwareRenderer renderer(10, 10);

    // First frame: red rect
    prism::SceneSnapshot snap1;
    snap1.version = 1;
    snap1.geometry.push_back({1, R(0.0f, 0.0f, 10.0f, 10.0f)});
    snap1.draw_lists.emplace_back();
    snap1.draw_lists[0].filled_rect(R(0.0f, 0.0f, 10.0f, 10.0f), prism::Color::rgba(255, 0, 0));
    snap1.z_order.push_back(0);
    renderer.render_frame(snap1);

    // Second frame: empty
    prism::SceneSnapshot snap2;
    snap2.version = 2;
    renderer.render_frame(snap2);

    CHECK(renderer.buffer().pixel(5, 5) == 0xFF000000); // cleared
}

TEST_CASE("render_frame respects z_order") {
    prism::SoftwareRenderer renderer(10, 10);

    prism::SceneSnapshot snap;
    snap.version = 1;
    // Widget 0: blue background
    snap.geometry.push_back({0, R(0.0f, 0.0f, 10.0f, 10.0f)});
    snap.draw_lists.emplace_back();
    snap.draw_lists[0].filled_rect(R(0.0f, 0.0f, 10.0f, 10.0f), prism::Color::rgba(0, 0, 255));
    // Widget 1: red foreground (overlapping)
    snap.geometry.push_back({1, R(3.0f, 3.0f, 4.0f, 4.0f)});
    snap.draw_lists.emplace_back();
    snap.draw_lists[1].filled_rect(R(3.0f, 3.0f, 4.0f, 4.0f), prism::Color::rgba(255, 0, 0));
    // z_order: 0 first (back), 1 second (front)
    snap.z_order = {0, 1};

    renderer.render_frame(snap);

    CHECK(renderer.buffer().pixel(0, 0) == 0xFF0000FF); // blue
    CHECK(renderer.buffer().pixel(5, 5) == 0xFFFF0000); // red on top
}

TEST_CASE("resize changes buffer dimensions") {
    prism::SoftwareRenderer renderer(10, 10);
    renderer.resize(20, 15);
    CHECK(renderer.buffer().width() == 20);
    CHECK(renderer.buffer().height() == 15);
}
