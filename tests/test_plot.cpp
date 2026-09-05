#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/widgets/plot_render.hpp>
#include <prism/widgets/plot.hpp>
#include <array>
#include <cmath>
#include <limits>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


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
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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

TEST_CASE("compute_mapping reclaims the bottom margin when draw_x_axis is false")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};

    auto map_with_axis = compute_mapping(bounds, xr, yr, vt, {}, true);
    auto map_without_axis = compute_mapping(bounds, xr, yr, vt, {}, false);

    // Suppressing the x-axis must shrink the reserved bottom margin down to the
    // same size as the top margin (a symmetric, snug box), not leave the old
    // tick-label-sized margin blank underneath an unlabeled plot.
    CHECK(map_without_axis.plot_area.extent.h.raw() >
          map_with_axis.plot_area.extent.h.raw());
    CHECK(map_without_axis.plot_area.extent.h.raw() ==
          doctest::Approx(map_with_axis.plot_area.extent.h.raw()
                           + (margin_bottom.raw() - margin_top.raw())));
}

TEST_CASE("draw_background emits filled rect and border")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    DrawList dl;
    prism::plot::PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();

    auto ticks = prism::plot::compute_ticks(map);
    prism::plot::draw_grid_lines(dl, map, ticks, t);
    CHECK(dl.size() > 0);

    bool has_line = false;
    for (auto& cmd : dl.commands)
        if (std::holds_alternative<Line>(cmd)) has_line = true;
    CHECK(has_line);

    DrawList dl2;
    prism::plot::draw_tick_labels(dl2, map, ticks, t);
    bool has_text = false;
    for (auto& cmd : dl2.commands)
        if (std::holds_alternative<TextCmd>(cmd)) has_text = true;
    CHECK(has_text);
}

TEST_CASE("draw_tick_labels suppresses x-axis output when draw_x_axis is false")
{
    using namespace prism;
    using namespace prism::plot;
    PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();
    auto ticks = compute_ticks(map);

    DrawList with_x;
    draw_tick_labels(with_x, map, ticks, t);
    DrawList without_x;
    draw_tick_labels(without_x, map, ticks, t, false);

    CHECK(without_x.size() < with_x.size());

    bool has_y_text = false;
    for (auto& cmd : without_x.commands)
        if (std::holds_alternative<TextCmd>(cmd)) has_y_text = true;
    CHECK(has_y_text);
}

TEST_CASE("draw_tick_labels uses a custom x_tick_format when given one")
{
    using namespace prism;
    using namespace prism::plot;
    PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();
    auto ticks = compute_ticks(map);

    DrawList dl;
    draw_tick_labels(dl, map, ticks, t, true, [](double v) { return fmt::format("<{:.0f}>", v); });

    int formatted_x_labels = 0;
    for (auto& cmd : dl.commands) {
        auto* txt = std::get_if<TextCmd>(&cmd);
        if (txt && !txt->text.empty() && txt->text.front() == '<' && txt->text.back() == '>')
            ++formatted_x_labels;
    }
    CHECK(formatted_x_labels == static_cast<int>(ticks.x.size()));
}

TEST_CASE("draw_axes_labels suppresses x_label when draw_x_axis is false")
{
    using namespace prism;
    using namespace prism::plot;
    PlotMapping map{
        .x_range = {0.0, 10.0},
        .y_range = {0.0, 100.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };
    Theme t = default_theme();

    DrawList dl;
    draw_axes_labels(dl, map, "Time", "Value", t, false);

    REQUIRE(dl.size() == 1);
    auto& txt = std::get<TextCmd>(dl.commands[0]);
    CHECK(txt.angle == 90.f);  // only the rotated y_label remains; x_label suppressed
}

TEST_CASE("draw_series emits polylines for each series")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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
    CHECK(dl.size() == 1);
    CHECK(std::holds_alternative<Polyline>(dl.commands[0]));

    auto& poly = std::get<Polyline>(dl.commands[0]);
    CHECK(poly.points.size() == 3);
}

TEST_CASE("draw_series emits a filled polygon under the curve when fill is set")
{
    using namespace prism;
    using namespace prism::plot;
    DrawList dl;
    PlotMapping map{
        .x_range = {0.0, 2.0},
        .y_range = {-1.0, 4.0},
        .plot_area = Rect{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}},
    };

    Series s(XYData{{0.0, 1.0, 2.0}, {0.0, 2.0, 4.0}},
             SeriesStyle{Color::rgba(255, 0, 0), 2.f, /*fill=*/true, /*baseline=*/0.0});
    std::array<Series, 1> arr = {std::move(s)};

    draw_series(dl, map, arr);
    REQUIRE(dl.size() == 2);
    CHECK(std::holds_alternative<FilledPolygon>(dl.commands[0]));
    CHECK(std::holds_alternative<Polyline>(dl.commands[1]));

    auto& fp = std::get<FilledPolygon>(dl.commands[0]);
    CHECK(fp.points.size() == 6);  // 3 data points x (curve, baseline)

    // First strip pair: curve point at x=0,y=0 and its baseline projection (x=0, y=baseline=0)
    auto expected_curve0 = map.to_pixel(0.0, 0.0);
    auto expected_base0 = map.to_pixel(0.0, 0.0);
    CHECK(fp.points[0].x.raw() == doctest::Approx(expected_curve0.x.raw()));
    CHECK(fp.points[0].y.raw() == doctest::Approx(expected_curve0.y.raw()));
    CHECK(fp.points[1].y.raw() == doctest::Approx(expected_base0.y.raw()));

    // Second strip pair: curve point at x=1,y=2 vs its baseline projection at x=1,y=0 --
    // these must differ, proving the baseline substitution actually happened (the first
    // pair's curve/baseline coincide at (0,0), which wouldn't catch a dropped substitution).
    auto expected_curve1 = map.to_pixel(1.0, 2.0);
    auto expected_base1 = map.to_pixel(1.0, 0.0);
    CHECK(fp.points[2].x.raw() == doctest::Approx(expected_curve1.x.raw()));
    CHECK(fp.points[2].y.raw() == doctest::Approx(expected_curve1.y.raw()));
    CHECK(fp.points[3].x.raw() == doctest::Approx(expected_base1.x.raw()));
    CHECK(fp.points[3].y.raw() == doctest::Approx(expected_base1.y.raw()));
    CHECK(fp.points[2].y.raw() != doctest::Approx(fp.points[3].y.raw()));

    // Fill color reuses the series color at a fixed reduced alpha
    CHECK(fp.color.r == 255);
    CHECK(fp.color.g == 0);
    CHECK(fp.color.b == 0);
    CHECK(fp.color.a == 40);

    // Series without fill emits only the Polyline
    DrawList dl2;
    Series s2(XYData{{0.0, 1.0}, {0.0, 1.0}}, SeriesStyle{});
    std::array<Series, 1> arr2 = {std::move(s2)};
    draw_series(dl2, map, arr2);
    CHECK(dl2.size() == 1);
    CHECK(std::holds_alternative<Polyline>(dl2.commands[0]));
}

TEST_CASE("draw_cursor emits crosshair when visible")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
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

TEST_CASE("draw_vertical_cursor emits only a vertical line when visible")
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

    draw_vertical_cursor(dl, map, 5.0, true, t);
    REQUIRE(dl.size() == 1);
    CHECK(std::holds_alternative<Line>(dl.commands[0]));
}

TEST_CASE("draw_vertical_cursor emits nothing when not visible")
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

    draw_vertical_cursor(dl, map, 5.0, false, t);
    CHECK(dl.size() == 0);
}

TEST_CASE("render_plot_panel with PlotGroupCursor draws no value readout when not visible")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Field<PlotGroupCursor> cursor{{5.0, false}};

    Series s(XYData{{0.0, 5.0, 10.0}, {0.0, 5.0, 10.0}}, SeriesStyle{});
    std::array<Series, 1> arr = {std::move(s)};

    DrawList dl;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;

    render_plot_panel(dl, bounds, node, xr, yr, vt, cursor,
                      std::span<const Series>(arr), "X", "Y", true);

    int filled_rect_count = 0;
    for (auto& cmd : dl.commands)
        if (std::holds_alternative<FilledRect>(cmd)) ++filled_rect_count;
    CHECK(filled_rect_count == 1);  // only the plot background -- no readout box behind it
}

TEST_CASE("render_plot_panel with PlotGroupCursor labels each series' nearest value")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Field<PlotGroupCursor> cursor{{4.0, true}};  // nearest sample is x=5.0, not an exact hit

    Series s(XYData{{0.0, 5.0, 10.0}, {0.0, 5.0, 10.0}}, SeriesStyle{});
    std::array<Series, 1> arr = {std::move(s)};

    DrawList dl;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;

    render_plot_panel(dl, bounds, node, xr, yr, vt, cursor,
                      std::span<const Series>(arr), "X", "Y", true);

    int filled_rect_count = 0;
    bool has_circle = false;
    bool has_value_text = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<FilledRect>(cmd)) ++filled_rect_count;
        if (std::holds_alternative<Circle>(cmd)) has_circle = true;
        if (auto* txt = std::get_if<TextCmd>(&cmd); txt && txt->text == "5")
            has_value_text = true;
    }
    CHECK(filled_rect_count == 2);  // background + the value readout box
    CHECK(has_circle);             // marker at the nearest sample
    CHECK(has_value_text);         // nearest sample's y (5.0), not the cursor's own x (4.0)
}

TEST_CASE("route_plot_input with PlotGroupCursor sets data_x without data_y")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Field<PlotGroupCursor> cursor{{}};
    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, xr, yr, vt, std::span<const Series>{});
    Point center = map.plot_area.center();
    InputEvent ev = MouseMove{center};

    route_plot_input(ev, node, bounds, xr, yr, vt, cursor,
                     drag_mode, drag_start_pixel, drag_start_view,
                     std::span<const Series>{});

    CHECK(cursor.get().visible);
    CHECK(cursor.get().data_x == doctest::Approx(5.0));
}

TEST_CASE("PlotModel canvas produces draw commands")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.add_series(XYData{{0.0, 1.0, 2.0}, {0.0, 1.0, 0.0}},
                    SeriesStyle{Color::rgba(255, 0, 0), 2.f});

    DrawList dl;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;

    plot.canvas(dl, bounds, node);

    CHECK(dl.size() > 0);
    bool has_filled = false, has_polyline = false, has_line = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<FilledRect>(cmd)) has_filled = true;
        if (std::holds_alternative<Polyline>(cmd)) has_polyline = true;
        if (std::holds_alternative<Line>(cmd)) has_line = true;
    }
    CHECK(has_filled);
    CHECK(has_polyline);
    CHECK(has_line);
}

TEST_CASE("PlotModel series management")
{
    using namespace prism::plot;

    PlotModel plot;
    CHECK(plot.series_count() == 0);

    plot.add_series(XYData{{1.0, 2.0}, {3.0, 4.0}}, SeriesStyle{});
    CHECK(plot.series_count() == 1);

    plot.add_series(XYData{{5.0}, {6.0}}, SeriesStyle{});
    CHECK(plot.series_count() == 2);

    plot.remove_series(0);
    CHECK(plot.series_count() == 1);

    plot.clear_series();
    CHECK(plot.series_count() == 0);
}

TEST_CASE("PlotModel notify bumps revision")
{
    using namespace prism::plot;

    PlotModel plot;
    auto r0 = plot.revision.get();
    plot.notify();
    CHECK(plot.revision.get() == r0 + 1);
}

TEST_CASE("PlotModel cursor updates on mouse move")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, plot.x_range, plot.y_range, plot.view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();
    InputEvent ev = MouseMove{center};

    plot.handle_canvas_input(ev, node, bounds);
    CHECK(plot.cursor.get().visible);
}

TEST_CASE("PlotModel scroll zooms view")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, plot.x_range, plot.y_range, plot.view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();

    InputEvent ev = MouseScroll{center, DX{0}, DY{3}};
    plot.handle_canvas_input(ev, node, bounds);

    auto v = plot.view.get();
    CHECK(v.scale_x > 1.0);
    CHECK(v.scale_y > 1.0);
}

TEST_CASE("PlotModel drag pans view")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, plot.x_range, plot.y_range, plot.view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();

    // Mouse down
    InputEvent down = MouseButton{center, 1, true};
    plot.handle_canvas_input(down, node, bounds);
    CHECK(plot.drag_mode == DragMode::Pan);

    // Mouse move (drag right)
    Point moved{X{center.x.raw() + 50.f}, center.y};
    InputEvent drag = MouseMove{moved};
    plot.handle_canvas_input(drag, node, bounds);

    auto v = plot.view.get();
    CHECK(v.offset_x != 0.0);

    // Mouse up
    InputEvent up = MouseButton{moved, 1, false};
    plot.handle_canvas_input(up, node, bounds);
    CHECK(plot.drag_mode == DragMode::None);
}

TEST_CASE("PlotModel reset_view restores auto_fit")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});
    plot.view.set(ViewTransform{1.0, 2.0, 3.0, 3.0});

    plot.reset_view();

    CHECK(plot.x_range.get().auto_fit);
    CHECK(plot.y_range.get().auto_fit);
    CHECK(plot.view.get().offset_x == 0.0);
    CHECK(plot.view.get().offset_y == 0.0);
    CHECK(plot.view.get().scale_x == 1.0);
    CHECK(plot.view.get().scale_y == 1.0);
}

TEST_CASE("PlotModel right-click resets view")
{
    using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});
    plot.view.set(ViewTransform{1.0, 2.0, 2.0, 2.0});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, plot.x_range, plot.y_range, plot.view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();

    InputEvent ev = MouseButton{center, 3, true};
    plot.handle_canvas_input(ev, node, bounds);

    CHECK(plot.x_range.get().auto_fit);
    CHECK(plot.view.get().scale_x == 1.0);
}

TEST_CASE("PlotModel 'm' key resets view like right-click does")
{
    using namespace prism;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});
    plot.view.set(ViewTransform{1.0, 2.0, 2.0, 2.0});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    InputEvent ev = KeyPress{keys::m, 0};
    plot.handle_canvas_input(ev, node, bounds);

    CHECK(plot.x_range.get().auto_fit);
    CHECK(plot.y_range.get().auto_fit);
    CHECK(plot.view.get().scale_x == 1.0);
}

TEST_CASE("PlotModel ignores keys other than 'm'")
{
    using namespace prism;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.view.set(ViewTransform{1.0, 2.0, 2.0, 2.0});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    InputEvent ev = KeyPress{keys::i, 0};
    plot.handle_canvas_input(ev, node, bounds);

    CHECK_FALSE(plot.x_range.get().auto_fit);
    CHECK(plot.view.get().scale_x == 2.0);
}

TEST_CASE("default_series_colors returns 8 distinct colors")
{
    auto colors = prism::plot::default_series_colors(prism::default_theme());
    CHECK(colors.size() == 8);

    for (auto& c : colors)
        CHECK(c.a == 255);

    for (size_t i = 0; i < colors.size(); ++i)
        for (size_t j = i + 1; j < colors.size(); ++j)
            CHECK((colors[i].r != colors[j].r
                   || colors[i].g != colors[j].g
                   || colors[i].b != colors[j].b));
}

TEST_CASE("PlotGroup shares x pan/zoom across panels, keeps y pan/zoom independent per panel")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    auto& p2 = group.add_plot("B");
    p1.add_series(XYData{{0.0, 1.0, 2.0}, {0.0, 1.0, 0.0}}, SeriesStyle{});
    p2.add_series(XYData{{0.0, 1.0, 2.0}, {0.0, 10.0, 0.0}}, SeriesStyle{});

    group.x_range.set({0.0, 10.0, false});
    p1.y_range.set({0.0, 10.0, false});
    p2.y_range.set({0.0, 20.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, group.x_range, p1.y_range, group.x_view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();

    // Diagonal drag on p1: moves both horizontally and vertically.
    InputEvent down = MouseButton{center, 1, true};
    p1.handle_canvas_input(down, node, bounds);
    Point moved{X{center.x.raw() + 50.f}, Y{center.y.raw() + 30.f}};
    InputEvent drag = MouseMove{moved};
    p1.handle_canvas_input(drag, node, bounds);
    InputEvent up = MouseButton{moved, 1, false};
    p1.handle_canvas_input(up, node, bounds);

    CHECK(group.x_view.get().offset_x != 0.0);   // shared x pan moved
    CHECK(p1.y_view.get().offset_y != 0.0);      // p1's own y pan moved
    CHECK(p2.y_view.get().offset_y == 0.0);      // p2's y pan untouched
    CHECK(group.x_view.get().offset_y == 0.0);   // vertical delta never leaks into the shared field
    CHECK(p2.y_range.get().min == 0.0);
    CHECK(p2.y_range.get().max == 20.0);

    // Scroll-zoom on p1, centered in the plot area (zooms both axes by design):
    InputEvent scroll = MouseScroll{center, DX{0}, DY{3}};
    p1.handle_canvas_input(scroll, node, bounds);

    CHECK(group.x_view.get().scale_x > 1.0);     // shared x zoom applied
    CHECK(p1.y_view.get().scale_y > 1.0);        // p1's own y zoom applied
    CHECK(p2.y_view.get().scale_y == 1.0);       // p2's y zoom untouched
}

TEST_CASE("PlotGroup reset_view resets shared x-state and every panel's y-state")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    auto& p2 = group.add_plot("B");

    group.x_range.set({0.0, 10.0, false});
    group.x_view.set(ViewTransform{1.0, 2.0, 3.0, 3.0});
    p1.y_range.set({0.0, 10.0, false});
    p1.y_view.set(ViewTransform{0.0, 5.0, 1.0, 2.0});
    p2.y_range.set({0.0, 20.0, false});
    p2.y_view.set(ViewTransform{0.0, 7.0, 1.0, 4.0});

    group.reset_view();

    CHECK(group.x_range.get().auto_fit);
    CHECK(group.x_view.get().offset_x == 0.0);
    CHECK(group.x_view.get().scale_x == 1.0);
    CHECK(p1.y_range.get().auto_fit);
    CHECK(p1.y_view.get().offset_y == 0.0);
    CHECK(p1.y_view.get().scale_y == 1.0);
    CHECK(p2.y_range.get().auto_fit);
    CHECK(p2.y_view.get().offset_y == 0.0);
    CHECK(p2.y_view.get().scale_y == 1.0);
}

TEST_CASE("PlotGroup 'm' key on one panel resets the whole group, not just that panel")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    auto& p2 = group.add_plot("B");

    group.x_range.set({0.0, 10.0, false});
    group.x_view.set(ViewTransform{1.0, 2.0, 3.0, 3.0});
    p1.y_range.set({0.0, 10.0, false});
    p1.y_view.set(ViewTransform{0.0, 5.0, 1.0, 2.0});
    p2.y_range.set({0.0, 20.0, false});
    p2.y_view.set(ViewTransform{0.0, 7.0, 1.0, 4.0});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    // The shortcut reaches only p1 (the panel that has focus) -- p2 never receives this
    // KeyPress at all, same one-widget dispatch as PlotGroup's mouse-cursor tests above.
    InputEvent ev = KeyPress{keys::m, 0};
    p1.handle_canvas_input(ev, node, bounds);

    CHECK(group.x_range.get().auto_fit);
    CHECK(group.x_view.get().scale_x == 1.0);
    CHECK(p1.y_range.get().auto_fit);
    CHECK(p1.y_view.get().scale_y == 1.0);
    CHECK(p2.y_range.get().auto_fit);   // p2 reset too, even though it got no event
    CHECK(p2.y_view.get().scale_y == 1.0);
}

TEST_CASE("PlotGroup cursor syncs data_x across panels")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    group.add_plot("B");
    group.x_range.set({0.0, 10.0, false});
    p1.y_range.set({0.0, 10.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, group.x_range, p1.y_range, group.x_view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();
    InputEvent ev = MouseMove{center};
    p1.handle_canvas_input(ev, node, bounds);

    CHECK(group.cursor.get().visible);
    CHECK(group.cursor.get().data_x == doctest::Approx(5.0));
}

TEST_CASE("PlotGroup labels every panel's own value at the shared cursor, hovered or not")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    auto& p2 = group.add_plot("B");
    group.x_range.set({0.0, 10.0, false});
    p1.y_range.set({0.0, 100.0, false});
    p2.y_range.set({0.0, 20.0, false});
    p1.add_series(XYData{{0.0, 5.0, 10.0}, {0.0, 50.0, 100.0}}, SeriesStyle{});
    p2.add_series(XYData{{0.0, 5.0, 10.0}, {0.0, 15.0, 20.0}}, SeriesStyle{});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    // Hovering p1 alone sets the group's shared (x-only) cursor -- p2 never receives
    // this MouseMove at all, since event_routing.hpp dispatches it only to the single
    // hit-tested widget.
    auto map = compute_mapping(bounds, group.x_range, p1.y_range, group.x_view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();
    InputEvent ev = MouseMove{center};
    p1.handle_canvas_input(ev, node, bounds);
    CHECK(group.cursor.get().data_x == doctest::Approx(5.0));

    auto has_value_text = [](const DrawList& dl, const std::string& value) {
        for (auto& cmd : dl.commands)
            if (auto* txt = std::get_if<TextCmd>(&cmd); txt && txt->text == value)
                return true;
        return false;
    };

    DrawList dl1;
    p1.canvas(dl1, bounds, node);
    CHECK(has_value_text(dl1, "50"));  // p1's own series value at x=5.0

    DrawList dl2;
    p2.canvas(dl2, bounds, node);
    CHECK(has_value_text(dl2, "15"));  // p2 never got the move event, but still labels
                                       // its own value at the shared cursor x
}

TEST_CASE("PlotGroup only the last-added panel draws the x-axis")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p1 = group.add_plot("A");
    auto& p2 = group.add_plot("B");
    group.x_range.set({0.0, 10.0, false});
    p1.y_range.set({0.0, 10.0, false});
    p2.y_range.set({0.0, 10.0, false});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto count_text = [](const DrawList& dl) {
        int n = 0;
        for (auto& cmd : dl.commands) if (std::holds_alternative<TextCmd>(cmd)) ++n;
        return n;
    };

    DrawList dl1, dl2;
    p1.canvas(dl1, bounds, node);
    p2.canvas(dl2, bounds, node);
    CHECK(count_text(dl2) > count_text(dl1));  // p2 (last) also has x-tick text

    auto& p3 = group.add_plot("C");
    p3.y_range.set({0.0, 10.0, false});
    DrawList dl2_after, dl3;
    p2.canvas(dl2_after, bounds, node);
    p3.canvas(dl3, bounds, node);
    CHECK(count_text(dl2_after) < count_text(dl2));  // p2 lost the x-axis to p3
    CHECK(count_text(dl3) > count_text(dl2_after));
}

TEST_CASE("PlotPanel series management")
{
    using namespace prism;
    using namespace prism::plot;

    PlotGroup group;
    auto& p = group.add_plot("A");
    p.add_series(XYData{{1.0, 2.0}, {3.0, 4.0}}, SeriesStyle{});
    auto r0 = p.revision.get();
    p.notify();
    CHECK(p.revision.get() == r0 + 1);

    p.clear_series();
    DrawList dl;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    p.canvas(dl, bounds, node);

    bool has_polyline = false;
    for (auto& cmd : dl.commands) if (std::holds_alternative<Polyline>(cmd)) has_polyline = true;
    CHECK(!has_polyline);
}

// --- min-max decimation (plot_render.hpp) ---

static prism::plot::PlotMapping make_test_map(float w, float h,
                                              double xmin, double xmax,
                                              double ymin, double ymax)
{
    return prism::plot::PlotMapping{
        .x_range = {.min = xmin, .max = xmax, .auto_fit = false},
        .y_range = {.min = ymin, .max = ymax, .auto_fit = false},
        .plot_area = prism::Rect{prism::Point{prism::X{0.f}, prism::Y{0.f}},
                                 prism::Size{prism::Width{w}, prism::Height{h}}},
    };
}

TEST_CASE("series_is_monotonic_x accepts non-decreasing, rejects any decrease")
{
    prism::plot::XYData up{{0, 1, 2, 3}, {0, 0, 0, 0}};
    CHECK(prism::plot::series_is_monotonic_x(up));
    prism::plot::XYData flat{{0, 1, 1, 2}, {0, 0, 0, 0}};
    CHECK(prism::plot::series_is_monotonic_x(flat));
    prism::plot::XYData dip{{0, 2, 1, 3}, {0, 0, 0, 0}};
    CHECK_FALSE(prism::plot::series_is_monotonic_x(dip));
}

TEST_CASE("decimated_series_points keeps the exact per-column envelope")
{
    prism::plot::XYData data;
    const size_t n = 10'000;
    for (size_t i = 0; i < n; ++i) {
        data.xs.push_back(static_cast<double>(i));
        data.ys.push_back(std::sin(static_cast<double>(i) * 0.01));
    }
    data.ys[4242] = 1.9; // unique global max above the sine's amplitude

    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);
    auto out = prism::plot::decimated_series_points(data, map);

    CHECK(out.size() <= 200); // <= 2 points per pixel column
    CHECK(out.size() > 50);

    // Output x is non-decreasing (monotonic input, column order, index order inside).
    for (size_t i = 1; i < out.size(); ++i)
        CHECK(out[i].x.raw() >= out[i - 1].x.raw());

    // The global extremes survive decimation.
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < n; ++i) {
        const float py = map.to_pixel(data.x(i), data.y(i)).y.raw();
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }
    bool has_min = false, has_max = false;
    for (auto& p : out) {
        if (p.y.raw() == min_y) has_min = true;
        if (p.y.raw() == max_y) has_max = true;
    }
    CHECK(has_min);
    CHECK(has_max);

    // Every emitted point is an actual input sample's pixel point (not an interpolation).
    for (auto& p : out) {
        bool found = false;
        for (size_t i = 0; i < n && !found; ++i) {
            const auto q = map.to_pixel(data.x(i), data.y(i));
            if (q.x.raw() == p.x.raw() && q.y.raw() == p.y.raw()) found = true;
        }
        CHECK(found);
    }
}

TEST_CASE("decimated_series_points skips NaN samples")
{
    prism::plot::XYData data;
    for (size_t i = 0; i < 1000; ++i) {
        data.xs.push_back(static_cast<double>(i));
        data.ys.push_back(std::nan(""));
    }
    data.ys[500] = 1.0; // a single real sample
    auto map = make_test_map(10, 100, 0.0, 1000.0, -2.0, 2.0);
    auto out = prism::plot::decimated_series_points(data, map);
    CHECK(out.size() == 1); // its column's min == max, emitted once
}

TEST_CASE("draw_series keeps the full-fidelity path below the threshold")
{
    prism::plot::XYData data;
    for (size_t i = 0; i < 200; ++i) { // 2 samples/px < 4x threshold
        data.xs.push_back(static_cast<double>(i));
        data.ys.push_back(std::sin(static_cast<double>(i) * 0.1));
    }
    prism::plot::Series s(data, prism::plot::SeriesStyle{});
    std::array<prism::plot::Series, 1> range{std::move(s)};
    auto map = make_test_map(100, 100, 0.0, 200.0, -2.0, 2.0);

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, range);
    REQUIRE(dl.commands.size() == 1);
    auto& pl = std::get<prism::Polyline>(dl.commands[0]);
    CHECK(pl.points.size() == 200);
}

TEST_CASE("draw_series decimates a dense monotonic series")
{
    prism::plot::XYData data;
    for (size_t i = 0; i < 10'000; ++i) {
        data.xs.push_back(static_cast<double>(i));
        data.ys.push_back(std::sin(static_cast<double>(i) * 0.01));
    }
    prism::plot::Series s(data, prism::plot::SeriesStyle{});
    std::array<prism::plot::Series, 1> range{std::move(s)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, range);
    REQUIRE(dl.commands.size() == 1);
    auto& pl = std::get<prism::Polyline>(dl.commands[0]);
    CHECK(pl.points.size() <= 200);
}

TEST_CASE("draw_series does not decimate a dense non-monotonic series")
{
    prism::plot::XYData data;
    for (size_t i = 0; i < 10'000; ++i) { // x zig-zags: every even step decreases
        data.xs.push_back(i % 2 == 0 ? static_cast<double>(i) : static_cast<double>(i) - 1.5);
        data.ys.push_back(std::sin(static_cast<double>(i) * 0.01));
    }
    prism::plot::Series s(data, prism::plot::SeriesStyle{});
    std::array<prism::plot::Series, 1> range{std::move(s)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, range);
    REQUIRE(dl.commands.size() == 1);
    auto& pl = std::get<prism::Polyline>(dl.commands[0]);
    CHECK(pl.points.size() == 10'000);
}

// A PlotSource that counts accessor calls, so tests can tell a full rescan from a
// cache hit. Counters live outside (the Series moves its own copy of the source).
struct CountingSource {
    std::vector<double> xs;
    std::vector<double> ys;
    struct Calls { size_t x = 0, y = 0, size = 0; };
    Calls* calls;
    size_t size() const { ++calls->size; return xs.size(); }
    double x(size_t i) const { ++calls->x; return xs[i]; }
    double y(size_t i) const { ++calls->y; return ys[i]; }
};

static prism::plot::Series counting_series(CountingSource::Calls& calls)
{
    return prism::plot::Series(
        CountingSource{{0.0, 1.0, 2.0}, {0.0, 2.0, 1.0}, &calls},
        prism::plot::SeriesStyle{});
}

TEST_CASE("resolve_auto_fit_ranges returns manual ranges untouched and leaves the cache cold")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{counting_series(calls)};
    prism::Field<prism::plot::AxisRange> xr{prism::plot::AxisRange{-5.0, 5.0, false}};
    prism::Field<prism::plot::AxisRange> yr{prism::plot::AxisRange{-1.0, 3.0, false}};
    prism::plot::AutoFitCache cache;

    auto r = prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);

    CHECK(r.x.min == doctest::Approx(-5.0));
    CHECK(r.x.max == doctest::Approx(5.0));
    CHECK(r.y.min == doctest::Approx(-1.0));
    CHECK(r.y.max == doctest::Approx(3.0));
    CHECK_FALSE(cache.valid);
    CHECK(calls.x == 0);
    CHECK(calls.y == 0);
}

TEST_CASE("resolve_auto_fit_ranges scans once, then reuses the cached fit")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{counting_series(calls)};
    prism::Field<prism::plot::AxisRange> xr;
    prism::Field<prism::plot::AxisRange> yr;
    prism::plot::AutoFitCache cache;

    auto r1 = prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    REQUIRE(cache.valid);
    // Data x in [0,2], y in [0,2], both padded 5%: [-0.1, 2.1].
    CHECK(r1.x.min == doctest::Approx(-0.1));
    CHECK(r1.x.max == doctest::Approx(2.1));
    CHECK(r1.y.min == doctest::Approx(-0.1));
    CHECK(r1.y.max == doctest::Approx(2.1));
    CHECK_FALSE(r1.x.auto_fit);
    CHECK_FALSE(r1.y.auto_fit);
    const size_t scanned = calls.x + calls.y;
    REQUIRE(scanned > 0);

    auto r2 = prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    CHECK(calls.x + calls.y == scanned); // no rescan on unchanged data
    CHECK(r2.x.min == doctest::Approx(r1.x.min));
    CHECK(r2.x.max == doctest::Approx(r1.x.max));
    CHECK(r2.y.min == doctest::Approx(r1.y.min));
    CHECK(r2.y.max == doctest::Approx(r1.y.max));
}

TEST_CASE("resolve_auto_fit_ranges refits immediately when the data grows")
{
    CountingSource::Calls calls;
    std::vector<prism::plot::Series> series;
    series.emplace_back(counting_series(calls));
    prism::Field<prism::plot::AxisRange> xr;
    prism::Field<prism::plot::AxisRange> yr;
    prism::plot::AutoFitCache cache;

    auto r1 = prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    const size_t scanned = calls.x + calls.y;

    series.emplace_back(prism::plot::Series(
        CountingSource{{3.0}, {10.0}, &calls}, prism::plot::SeriesStyle{}));
    auto r2 = prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    CHECK(calls.x + calls.y > scanned); // size key changed -> full refit
    CHECK(r2.y.max == doctest::Approx(10.0 + (10.0 - 0.0) * 0.05));
    CHECK(r2.x.max > r1.x.max);
}

TEST_CASE("resolve_auto_fit_ranges refits once the throttle expires")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{counting_series(calls)};
    prism::Field<prism::plot::AxisRange> xr;
    prism::Field<prism::plot::AxisRange> yr;
    prism::plot::AutoFitCache cache;

    prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    const size_t scanned = calls.x + calls.y;

    for (int i = 0; i < prism::plot::auto_fit_throttle_frames; ++i)
        prism::plot::resolve_auto_fit_ranges(
            xr, yr, std::span<const prism::plot::Series>(series), cache);
    CHECK(calls.x + calls.y == scanned); // every render inside the window is a hit

    prism::plot::resolve_auto_fit_ranges(
        xr, yr, std::span<const prism::plot::Series>(series), cache);
    CHECK(calls.x + calls.y > scanned); // window expired -> one refit
}

static void canvas_into(prism::plot::PlotModel& plot, prism::DrawList& dl)
{
    prism::Rect bounds{prism::Point{prism::X{0}, prism::Y{0}},
                       prism::Size{prism::Width{400}, prism::Height{300}}};
    prism::Theme t = prism::default_theme();
    prism::app::WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;
    plot.canvas(dl, bounds, node);
}

TEST_CASE("PlotModel canvas reuses the auto-fit cache across renders")
{
    CountingSource::Calls calls;
    prism::plot::PlotModel plot;
    plot.add_series(CountingSource{{0.0, 1.0, 2.0}, {0.0, 2.0, 1.0}, &calls},
                    prism::plot::SeriesStyle{});

    prism::DrawList dl;
    canvas_into(plot, dl);
    REQUIRE(plot.autofit_cache.valid);

    canvas_into(plot, dl);
    CHECK(plot.autofit_cache.frames_since_fit == 1);
}

TEST_CASE("PlotPanel canvas reuses the auto-fit cache across renders")
{
    CountingSource::Calls calls;
    prism::plot::PlotGroup group;
    prism::plot::PlotPanel& panel = group.add_plot("y");
    panel.add_series(CountingSource{{0.0, 1.0, 2.0}, {0.0, 2.0, 1.0}, &calls},
                     prism::plot::SeriesStyle{});

    prism::DrawList dl;
    prism::Rect bounds{prism::Point{prism::X{0}, prism::Y{0}},
                       prism::Size{prism::Width{400}, prism::Height{300}}};
    prism::Theme t = prism::default_theme();
    prism::app::WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;

    panel.canvas(dl, bounds, node);
    REQUIRE(panel.autofit_cache.valid);
    panel.canvas(dl, bounds, node);
    CHECK(panel.autofit_cache.frames_since_fit == 1);
}

static prism::plot::Series dense_counting_series(CountingSource::Calls& calls,
                                                 size_t n = 10'000)
{
    std::vector<double> xs, ys;
    xs.reserve(n);
    ys.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        xs.push_back(static_cast<double>(i));
        ys.push_back(std::sin(static_cast<double>(i) * 0.01));
    }
    return prism::plot::Series(CountingSource{std::move(xs), std::move(ys), &calls},
                               prism::plot::SeriesStyle{});
}

TEST_CASE("draw_series reuses cached points while revision and map are unchanged")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{dense_counting_series(calls)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);
    std::array<prism::plot::SeriesDrawCache, 1> caches;

    prism::DrawList dl1;
    prism::plot::draw_series(dl1, map, series, 7, caches);
    REQUIRE(caches[0].valid);
    CHECK(caches[0].decimated);
    REQUIRE(dl1.commands.size() == 1);
    const size_t n1 = std::get<prism::Polyline>(dl1.commands[0]).points.size();
    CHECK(n1 <= 200);
    const size_t scanned = calls.x + calls.y;
    REQUIRE(scanned > 0);

    prism::DrawList dl2;
    prism::plot::draw_series(dl2, map, series, 7, caches);
    CHECK(calls.x + calls.y == scanned); // gate + transform skipped
    REQUIRE(dl2.commands.size() == 1);
    CHECK(std::get<prism::Polyline>(dl2.commands[0]).points.size() == n1);
}

TEST_CASE("draw_series cache misses when the revision changes")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{dense_counting_series(calls)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);
    std::array<prism::plot::SeriesDrawCache, 1> caches;

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, series, 7, caches);
    const size_t scanned = calls.x + calls.y;

    prism::plot::draw_series(dl, map, series, 8, caches);
    CHECK(calls.x + calls.y > scanned);
}

TEST_CASE("draw_series cache misses when the map changes")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{dense_counting_series(calls)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);
    std::array<prism::plot::SeriesDrawCache, 1> caches;

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, series, 7, caches);
    const size_t scanned = calls.x + calls.y;

    auto zoomed = make_test_map(100, 100, 0.0, 5000.0, -2.0, 2.0);
    prism::plot::draw_series(dl, zoomed, series, 7, caches);
    CHECK(calls.x + calls.y > scanned);
}

TEST_CASE("draw_series ignores a cache span whose size mismatches the series")
{
    CountingSource::Calls calls;
    std::array<prism::plot::Series, 1> series{dense_counting_series(calls)};
    auto map = make_test_map(100, 100, 0.0, 10000.0, -2.0, 2.0);
    std::array<prism::plot::SeriesDrawCache, 2> wrong;

    prism::DrawList dl;
    prism::plot::draw_series(dl, map, series, 7, wrong);
    REQUIRE(dl.commands.size() == 1);
    CHECK(std::get<prism::Polyline>(dl.commands[0]).points.size() <= 200);
    CHECK_FALSE(wrong[0].valid);
    CHECK_FALSE(wrong[1].valid);
}

TEST_CASE("PlotModel canvas skips series rescans while the revision is unchanged")
{
    CountingSource::Calls calls;
    prism::plot::PlotModel plot;
    plot.add_series(dense_counting_series(calls), prism::plot::SeriesStyle{});

    prism::DrawList dl;
    canvas_into(plot, dl);
    const size_t scanned = calls.x + calls.y;
    REQUIRE(scanned > 0);

    canvas_into(plot, dl);
    CHECK(calls.x + calls.y == scanned); // auto-fit hit + decimation hit

    plot.notify();
    canvas_into(plot, dl);
    CHECK(calls.x + calls.y > scanned); // new revision -> gate + decimate rerun
}

static void panel_canvas_into(prism::plot::PlotPanel& panel, prism::DrawList& dl)
{
    prism::Rect bounds{prism::Point{prism::X{0}, prism::Y{0}},
                       prism::Size{prism::Width{400}, prism::Height{300}}};
    prism::Theme t = prism::default_theme();
    prism::app::WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;
    panel.canvas(dl, bounds, node);
}

TEST_CASE("PlotPanel canvas skips series rescans while the revision is unchanged")
{
    CountingSource::Calls calls;
    prism::plot::PlotGroup group;
    prism::plot::PlotPanel& panel = group.add_plot("y");
    panel.add_series(dense_counting_series(calls), prism::plot::SeriesStyle{});

    prism::DrawList dl;
    panel_canvas_into(panel, dl);
    const size_t scanned = calls.x + calls.y;
    REQUIRE(scanned > 0);

    panel_canvas_into(panel, dl);
    CHECK(calls.x + calls.y == scanned);

    panel.notify();
    panel_canvas_into(panel, dl);
    CHECK(calls.x + calls.y > scanned);
}
