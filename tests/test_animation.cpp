#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/animation.hpp>

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
