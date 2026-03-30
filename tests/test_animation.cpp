#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/animation.hpp>
#include <prism/core/draw_list.hpp>
#include <string>

using namespace prism;

TEST_CASE("ease::linear") {
    CHECK(ease::linear(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::linear(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::linear(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_quad") {
    CHECK(ease::in_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_quad(Progress{0.5f}).raw() == doctest::Approx(0.25f));
    CHECK(ease::in_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::out_quad") {
    CHECK(ease::out_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::out_quad(Progress{0.5f}).raw() == doctest::Approx(0.75f));
    CHECK(ease::out_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_out_quad") {
    CHECK(ease::in_out_quad(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_out_quad(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::in_out_quad(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_cubic") {
    CHECK(ease::in_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_cubic(Progress{0.5f}).raw() == doctest::Approx(0.125f));
    CHECK(ease::in_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::out_cubic") {
    CHECK(ease::out_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::out_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("ease::in_out_cubic") {
    CHECK(ease::in_out_cubic(Progress{0.f}).raw() == doctest::Approx(0.f));
    CHECK(ease::in_out_cubic(Progress{0.5f}).raw() == doctest::Approx(0.5f));
    CHECK(ease::in_out_cubic(Progress{1.f}).raw() == doctest::Approx(1.f));
}

TEST_CASE("lerp float") {
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{0.f}) == doctest::Approx(0.f));
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{0.5f}) == doctest::Approx(5.f));
    CHECK(prism::lerp(0.f, 10.f, EasedProgress{1.f}) == doctest::Approx(10.f));
}

TEST_CASE("lerp Scalar<Tag>") {
    auto a = X{0.f};
    auto b = X{100.f};
    auto mid = prism::lerp(a, b, EasedProgress{0.25f});
    CHECK(mid.raw() == doctest::Approx(25.f));
}

TEST_CASE("lerp Color") {
    auto black = Color::rgba(0, 0, 0, 255);
    auto white = Color::rgba(255, 255, 255, 255);
    auto mid = prism::lerp(black, white, EasedProgress{0.5f});
    CHECK(mid.r == 128);
    CHECK(mid.g == 128);
    CHECK(mid.b == 128);
    CHECK(mid.a == 255);
}

TEST_CASE("Lerpable concept") {
    static_assert(prism::Lerpable<float>);
    static_assert(prism::Lerpable<X>);
    static_assert(prism::Lerpable<Color>);
    static_assert(!prism::Lerpable<std::string>);
}
