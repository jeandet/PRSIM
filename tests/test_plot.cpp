#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/widgets/plot_render.hpp>
#include <prism/widgets/plot.hpp>

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

TEST_CASE("auto_fit_range computes bounds with padding")
{
    using namespace prism::plot;
    Series s1(XYData{{0.0, 5.0, 10.0}, {-1.0, 3.0, 7.0}}, SeriesStyle{});
    std::array<Series, 1> arr = {std::move(s1)};

    auto xr = auto_fit_range(arr, Axis::X);
    CHECK(xr.min < 0.0);   // 5% padding
    CHECK(xr.max > 10.0);

    auto yr = auto_fit_range(arr, Axis::Y);
    CHECK(yr.min < -1.0);
    CHECK(yr.max > 7.0);
}

TEST_CASE("auto_fit_range returns default for empty series")
{
    auto r = prism::plot::auto_fit_range(std::span<const prism::plot::Series>{}, prism::plot::Axis::X);
    CHECK(r.min == doctest::Approx(0.0));
    CHECK(r.max == doctest::Approx(1.0));
}

TEST_CASE("compute_mapping subtracts margins from bounds")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};

    auto map = compute_mapping(bounds, xr, yr, vt, {});
    CHECK(map.plot_area.origin.x.raw() > 0.f);
    CHECK(map.plot_area.extent.w.raw() < 400.f);
    CHECK(map.plot_area.extent.h.raw() < 300.f);
}

TEST_CASE("draw_background emits filled rect and border")
{
    using namespace prism;
    DrawList dl;
    Rect area{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}};
    Theme t = default_theme();

    prism::plot::draw_background(dl, area, t);
    CHECK(dl.size() == 2);
    CHECK(std::holds_alternative<FilledRect>(dl.commands[0]));
    CHECK(std::holds_alternative<RectOutline>(dl.commands[1]));
}

TEST_CASE("draw_grid emits grid lines and tick labels")
{
    using namespace prism;
    DrawList dl;
    prism::plot::PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();

    prism::plot::draw_grid(dl, map, t);
    CHECK(dl.size() > 0);

    bool has_line = false, has_text = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<Line>(cmd)) has_line = true;
        if (std::holds_alternative<TextCmd>(cmd)) has_text = true;
    }
    CHECK(has_line);
    CHECK(has_text);
}

TEST_CASE("draw_series emits polylines for each series")
{
    using namespace prism;
    using namespace prism::plot;
    DrawList dl;
    PlotMapping map{
        .x_range = {0.0, 2.0},
        .y_range = {0.0, 4.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };

    Series s(XYData{{0.0, 1.0, 2.0}, {0.0, 2.0, 4.0}},
             SeriesStyle{Color::rgba(255, 0, 0), 2.f});
    std::array<Series, 1> arr = {std::move(s)};

    draw_series(dl, map, arr);
    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<Polyline>(dl.commands[1]));
    CHECK(std::holds_alternative<ClipPop>(dl.commands[2]));

    auto& poly = std::get<Polyline>(dl.commands[1]);
    CHECK(poly.points.size() == 3);
}

TEST_CASE("draw_cursor emits crosshair when visible")
{
    using namespace prism;
    using namespace prism::plot;
    DrawList dl;
    PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 10.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();

    CursorState cursor{5.0, 5.0, true};
    draw_cursor(dl, map, cursor, t);
    CHECK(dl.size() >= 3);

    bool has_line = false;
    for (auto& cmd : dl.commands)
        if (std::holds_alternative<Line>(cmd)) has_line = true;
    CHECK(has_line);
}

TEST_CASE("draw_cursor emits nothing when not visible")
{
    using namespace prism;
    using namespace prism::plot;
    DrawList dl;
    PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 10.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();

    CursorState cursor{5.0, 5.0, false};
    draw_cursor(dl, map, cursor, t);
    CHECK(dl.size() == 0);
}
