#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/hit_test.hpp>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

TEST_CASE("hit_test returns nullopt on empty snapshot") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    auto result = prism::hit_test(snap, P(50, 50));
    CHECK(!result.has_value());
}

TEST_CASE("hit_test finds widget containing point") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, R(0, 0, 100, 100)});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    auto result = prism::hit_test(snap, P(50, 50));
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

TEST_CASE("hit_test returns nullopt when point outside all widgets") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, R(0, 0, 100, 100)});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    auto result = prism::hit_test(snap, P(200, 200));
    CHECK(!result.has_value());
}

TEST_CASE("hit_test returns topmost widget in z-order") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    // Two overlapping widgets
    snap.geometry.push_back({1, R(0, 0, 200, 200)});     // index 0, behind
    snap.geometry.push_back({2, R(50, 50, 100, 100)});    // index 1, in front
    snap.draw_lists.push_back({});
    snap.draw_lists.push_back({});
    snap.z_order = {0, 1};  // back-to-front: 0 then 1

    // Point in overlapping area -- should hit widget 2 (front)
    auto result = prism::hit_test(snap, P(75, 75));
    REQUIRE(result.has_value());
    CHECK(*result == 2);

    // Point only in widget 1
    auto result2 = prism::hit_test(snap, P(10, 10));
    REQUIRE(result2.has_value());
    CHECK(*result2 == 1);
}

TEST_CASE("hit_test edge: point on boundary is inside") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, R(10, 10, 100, 100)});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    // Top-left corner (exactly on boundary)
    auto result = prism::hit_test(snap, P(10, 10));
    REQUIRE(result.has_value());
    CHECK(*result == 1);

    // Bottom-right corner (exclusive: x=110, y=110 is outside)
    auto outside = prism::hit_test(snap, P(110, 110));
    CHECK(!outside.has_value());
}
