#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/widgets/plot_render.hpp>

TEST_CASE("nice_ticks produces human-friendly values")
{
    auto ticks = prism::plot::nice_ticks(0.0, 1.0, 5);
    CHECK(!ticks.empty());
    CHECK(ticks.front() >= 0.0);
    CHECK(ticks.back() <= 1.0);
    // 1/2/5 multiples: expect 0.0, 0.2, 0.4, 0.6, 0.8, 1.0
    CHECK(ticks.size() == 6);
    CHECK(ticks[0] == doctest::Approx(0.0));
    CHECK(ticks[1] == doctest::Approx(0.2));
    CHECK(ticks[2] == doctest::Approx(0.4));
}

TEST_CASE("nice_ticks handles large range")
{
    auto ticks = prism::plot::nice_ticks(0.0, 10000.0, 5);
    CHECK(!ticks.empty());
    for (size_t i = 1; i < ticks.size(); ++i)
        CHECK(ticks[i] > ticks[i - 1]);
}

TEST_CASE("nice_ticks handles negative range")
{
    auto ticks = prism::plot::nice_ticks(-5.0, 5.0, 5);
    CHECK(!ticks.empty());
    CHECK(ticks.front() >= -5.0);
    CHECK(ticks.back() <= 5.0);
}

TEST_CASE("nice_ticks handles tiny range")
{
    auto ticks = prism::plot::nice_ticks(1.0, 1.001, 5);
    CHECK(!ticks.empty());
    CHECK(ticks.size() >= 2);
}

TEST_CASE("nice_ticks handles degenerate range")
{
    auto ticks = prism::plot::nice_ticks(5.0, 5.0, 5);
    CHECK(ticks.size() >= 1);
}

TEST_CASE("PlotMapping to_pixel maps data corners to plot area corners")
{
    using namespace prism;
    prism::plot::PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{0}}, Size{Width{200}, Height{150}}},
    };

    // Bottom-left of data → bottom-left of plot area (y is flipped)
    auto bl = map.to_pixel(0.0, 0.0);
    CHECK(bl.x.raw() == doctest::Approx(60.f));
    CHECK(bl.y.raw() == doctest::Approx(150.f));

    // Top-right of data → top-right of plot area
    auto tr = map.to_pixel(10.0, 100.0);
    CHECK(tr.x.raw() == doctest::Approx(260.f));
    CHECK(tr.y.raw() == doctest::Approx(0.f));
}

TEST_CASE("PlotMapping to_data roundtrips with to_pixel")
{
    using namespace prism;
    prism::plot::PlotMapping map{
        .x_range = {-5.0, 5.0},
        .y_range = {0.0, 1.0},
        .plot_area = Rect{Point{X{50}, Y{10}}, Size{Width{300}, Height{200}}},
    };

    auto px = map.to_pixel(2.5, 0.75);
    auto [dx, dy] = map.to_data(px);
    CHECK(dx == doctest::Approx(2.5));
    CHECK(dy == doctest::Approx(0.75));
}

TEST_CASE("PlotMapping apply_view scales and offsets range")
{
    prism::plot::AxisRange base{0.0, 10.0, false};
    // Zoom 2x centered at midpoint: range becomes 2.5..7.5
    auto result = prism::plot::PlotMapping::apply_view(base, 0.0, 2.0);
    CHECK(result.min == doctest::Approx(2.5));
    CHECK(result.max == doctest::Approx(7.5));

    // Pan by +3 on top of 2x zoom: range becomes 5.5..10.5
    auto panned = prism::plot::PlotMapping::apply_view(base, 3.0, 2.0);
    CHECK(panned.min == doctest::Approx(5.5));
    CHECK(panned.max == doctest::Approx(10.5));
}
