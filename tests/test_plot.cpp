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
    CHECK(dl.size() == 1);
    CHECK(std::holds_alternative<Polyline>(dl.commands[0]));

    auto& poly = std::get<Polyline>(dl.commands[0]);
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

TEST_CASE("PlotModel canvas produces draw commands")
{
    using namespace prism;
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
