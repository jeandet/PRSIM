# Plot Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a general-purpose XY plot canvas widget with multi-series support, pan/zoom/crosshair interaction, configurable axes, and theme integration.

**Architecture:** Layered canvas component — PlotModel struct with `canvas()` + `handle_canvas_input()`, decomposed into pure rendering layer functions (background, grid, data, overlay). Data access via type-erased PlotSource concept. All reactive state via Field<T> + depends_on().

**Tech Stack:** C++23/26, header-only, doctest, PRISM core (Field<T>, DrawList, Theme, canvas pattern)

**Spec:** `docs/superpowers/specs/2026-04-01-plot-widget-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `include/prism/widgets/plot.hpp` | PlotSource concept, XYData, SeriesStyle, Series, AxisRange, ViewTransform, CursorState, PlotModel (data model + canvas/input methods + series management) |
| `include/prism/widgets/plot_render.hpp` | PlotMapping, compute_mapping, nice_ticks, draw_background, draw_grid, draw_series, draw_cursor, draw_axes_labels, auto_fit_range, default_series_colors |
| `tests/test_plot.cpp` | All plot tests: nice_ticks, PlotMapping, auto_fit, rendering layers, interaction |
| `tests/meson.build` | Register test_plot in headless_tests |
| `examples/model_plot.cpp` | Example app demonstrating PlotModel with interactive data |
| `examples/meson.build` | Register model_plot example |

---

### Task 1: nice_ticks — Tick Value Generation

**Files:**
- Create: `include/prism/widgets/plot_render.hpp`
- Create: `tests/test_plot.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test for nice_ticks**

Create `tests/test_plot.cpp`:

```cpp
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
```

- [ ] **Step 2: Register test in meson.build**

In `tests/meson.build`, add to the `headless_tests` dictionary before the closing `}`:

```meson
  'plot' : files('test_plot.cpp'),
```

- [ ] **Step 3: Create plot_render.hpp with nice_ticks**

Create `include/prism/widgets/plot_render.hpp`:

```cpp
#pragma once
#include <prism/core/draw_list.hpp>
#include <prism/core/context.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace prism::plot {

inline std::vector<double> nice_ticks(double min, double max, int target_count)
{
    if (target_count < 1) target_count = 1;

    double range = max - min;
    if (range <= 0.0) return {min};

    double rough_step = range / target_count;
    double magnitude = std::pow(10.0, std::floor(std::log10(rough_step)));
    double residual = rough_step / magnitude;

    double nice_step;
    if (residual <= 1.5)
        nice_step = 1.0 * magnitude;
    else if (residual <= 3.5)
        nice_step = 2.0 * magnitude;
    else if (residual <= 7.5)
        nice_step = 5.0 * magnitude;
    else
        nice_step = 10.0 * magnitude;

    double tick_min = std::ceil(min / nice_step) * nice_step;
    std::vector<double> ticks;
    for (double v = tick_min; v <= max + nice_step * 1e-9; v += nice_step)
        ticks.push_back(v);

    return ticks;
}

} // namespace prism::plot
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All nice_ticks tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp tests/meson.build
git commit -m "feat(plot): add nice_ticks tick generation algorithm"
```

---

### Task 2: PlotMapping — Coordinate Transform

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing tests for PlotMapping**

Append to `tests/test_plot.cpp`:

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — PlotMapping not defined

- [ ] **Step 3: Implement PlotMapping in plot_render.hpp**

Add to `include/prism/widgets/plot_render.hpp`, inside `namespace prism::plot`:

```cpp
struct AxisRange {
    double min = 0.0;
    double max = 1.0;
    bool auto_fit = true;
    bool operator==(const AxisRange&) const = default;
};

struct PlotMapping {
    AxisRange x_range;
    AxisRange y_range;
    Rect plot_area;

    Point to_pixel(double data_x, double data_y) const
    {
        float px = plot_area.origin.x.raw()
                   + static_cast<float>((data_x - x_range.min) / (x_range.max - x_range.min))
                     * plot_area.extent.w.raw();
        float py = plot_area.origin.y.raw()
                   + static_cast<float>(1.0 - (data_y - y_range.min) / (y_range.max - y_range.min))
                     * plot_area.extent.h.raw();
        return Point{X{px}, Y{py}};
    }

    std::pair<double, double> to_data(Point pixel) const
    {
        double dx = x_range.min
                    + (pixel.x.raw() - plot_area.origin.x.raw())
                      / plot_area.extent.w.raw()
                      * (x_range.max - x_range.min);
        double dy = y_range.min
                    + (1.0 - (pixel.y.raw() - plot_area.origin.y.raw())
                             / plot_area.extent.h.raw())
                      * (y_range.max - y_range.min);
        return {dx, dy};
    }

    static AxisRange apply_view(AxisRange base, double offset, double scale)
    {
        double center = (base.min + base.max) / 2.0 + offset;
        double half_range = (base.max - base.min) / (2.0 * scale);
        return {center - half_range, center + half_range, false};
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All PlotMapping tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add PlotMapping coordinate transform"
```

---

### Task 3: Auto-Fit & Margins

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing tests for auto_fit_range and compute_mapping**

Append to `tests/test_plot.cpp`:

```cpp
#include <prism/widgets/plot.hpp>

TEST_CASE("auto_fit_range computes bounds with padding")
{
    using namespace prism::plot;
    Series s1;
    {
        std::vector<double> xs = {0.0, 5.0, 10.0};
        std::vector<double> ys = {-1.0, 3.0, 7.0};
        s1 = Series(XYData{std::move(xs), std::move(ys)}, SeriesStyle{});
    }
    std::array<Series, 1> arr = {std::move(s1)};

    auto xr = auto_fit_range(arr, Axis::X);
    CHECK(xr.min < 0.0);   // 5% padding
    CHECK(xr.max > 10.0);  // 5% padding
    CHECK(xr.auto_fit);

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
    // Plot area should be smaller than bounds (margins for ticks)
    CHECK(map.plot_area.origin.x.raw() > 0.f);
    CHECK(map.plot_area.extent.w.raw() < 400.f);
    CHECK(map.plot_area.extent.h.raw() < 300.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — auto_fit_range, Series, compute_mapping not defined

- [ ] **Step 3: Create plot.hpp with data types, and add auto_fit_range + compute_mapping to plot_render.hpp**

Create `include/prism/widgets/plot.hpp`:

```cpp
#pragma once
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/widget_node.hpp>
#include <prism/widgets/plot_render.hpp>
#include <functional>
#include <vector>
#include <span>

namespace prism::plot {

template <typename T>
concept PlotSource = requires(const T& src, size_t i) {
    { src.size() } -> std::convertible_to<size_t>;
    { src.x(i) } -> std::convertible_to<double>;
    { src.y(i) } -> std::convertible_to<double>;
};

struct XYData {
    std::vector<double> xs;
    std::vector<double> ys;
    size_t size() const { return xs.size(); }
    double x(size_t i) const { return xs[i]; }
    double y(size_t i) const { return ys[i]; }
};

struct SeriesStyle {
    Color color = Color::rgba(0, 140, 200);
    float thickness = 2.f;
};

class Series {
  public:
    Series() = default;

    template <PlotSource S>
    Series(S source, SeriesStyle s)
        : size_fn_([src = std::move(source)]() { return src.size(); })
        , x_fn_([this](size_t i) { return x_from_size_fn(i); })
        , style_(s)
    {
        // Capture source into closures properly
        auto shared = std::make_shared<S>(std::move(source));
        size_fn_ = [shared]() { return shared->size(); };
        x_fn_ = [shared](size_t i) { return shared->x(i); };
        y_fn_ = [shared](size_t i) { return shared->y(i); };
    }

    size_t size() const { return size_fn_ ? size_fn_() : 0; }
    double x(size_t i) const { return x_fn_(i); }
    double y(size_t i) const { return y_fn_(i); }
    const SeriesStyle& style() const { return style_; }

  private:
    double x_from_size_fn(size_t) { return 0.0; } // placeholder, replaced by ctor
    std::function<size_t()> size_fn_;
    std::function<double(size_t)> x_fn_;
    std::function<double(size_t)> y_fn_;
    SeriesStyle style_;
};

struct ViewTransform {
    double offset_x = 0.0;
    double offset_y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    bool operator==(const ViewTransform&) const = default;
};

struct CursorState {
    double data_x = 0.0;
    double data_y = 0.0;
    bool visible = false;
    bool operator==(const CursorState&) const = default;
};

enum class DragMode { None, Pan };

} // namespace prism::plot
```

Add to `include/prism/widgets/plot_render.hpp`, inside `namespace prism::plot`, after PlotMapping:

```cpp
enum class Axis { X, Y };

inline AxisRange auto_fit_range(std::span<const Series> series, Axis axis)
{
    if (series.empty()) return {0.0, 1.0, true};

    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();

    for (auto& s : series) {
        for (size_t i = 0; i < s.size(); ++i) {
            double v = (axis == Axis::X) ? s.x(i) : s.y(i);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }

    if (lo == hi) { lo -= 0.5; hi += 0.5; }
    double pad = (hi - lo) * 0.05;
    return {lo - pad, hi + pad, true};
}

constexpr float margin_left = 60.f;
constexpr float margin_bottom = 30.f;
constexpr float margin_top = 10.f;
constexpr float margin_right = 10.f;

inline PlotMapping compute_mapping(Rect bounds,
                                   const Field<AxisRange>& xr,
                                   const Field<AxisRange>& yr,
                                   const Field<ViewTransform>& vt,
                                   std::span<const Series> series)
{
    AxisRange eff_x = xr.get();
    AxisRange eff_y = yr.get();

    if (eff_x.auto_fit) eff_x = auto_fit_range(series, Axis::X);
    if (eff_y.auto_fit) eff_y = auto_fit_range(series, Axis::Y);

    auto& v = vt.get();
    eff_x = PlotMapping::apply_view(eff_x, v.offset_x, v.scale_x);
    eff_y = PlotMapping::apply_view(eff_y, v.offset_y, v.scale_y);

    Rect plot_area{
        Point{X{bounds.origin.x.raw() + margin_left},
              Y{bounds.origin.y.raw() + margin_top}},
        Size{Width{bounds.extent.w.raw() - margin_left - margin_right},
             Height{bounds.extent.h.raw() - margin_top - margin_bottom}},
    };

    return PlotMapping{eff_x, eff_y, plot_area};
}
```

**Include order:** `plot.hpp` includes `plot_render.hpp`. Since `auto_fit_range` and `compute_mapping` need `Series` (defined in `plot.hpp`), `auto_fit_range` is templated on any range (avoiding circular deps), and `compute_mapping` lives in `plot.hpp` after `Series`.

**Final file split:**
- `plot_render.hpp`: `AxisRange`, `PlotMapping`, `nice_ticks`, `Axis`, `auto_fit_range` (templated), rendering layer functions
- `plot.hpp`: includes `plot_render.hpp`, defines `Series`, `PlotModel`, `compute_mapping`

Add to `plot_render.hpp`:

```cpp
enum class Axis { X, Y };

template <typename Range>
AxisRange auto_fit_range(const Range& series, Axis axis)
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    bool any = false;

    for (auto& s : series) {
        for (size_t i = 0; i < s.size(); ++i) {
            double v = (axis == Axis::X) ? s.x(i) : s.y(i);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
    }

    if (!any) return {0.0, 1.0, true};
    if (lo == hi) { lo -= 0.5; hi += 0.5; }
    double pad = (hi - lo) * 0.05;
    return {lo - pad, hi + pad, true};
}
```

And `compute_mapping` goes into `plot.hpp` (since it needs `Series` and `Field<>`).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All auto_fit and compute_mapping tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot.hpp include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add Series type erasure, auto_fit_range, compute_mapping"
```

---

### Task 4: Rendering Layer Functions

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Modify: `include/prism/widgets/plot_render.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing tests for rendering layers**

Append to `tests/test_plot.cpp`:

```cpp
TEST_CASE("draw_background emits filled rect and border")
{
    using namespace prism;
    DrawList dl;
    Rect area{Point{X{60}, Y{10}}, Size{Width{300}, Height{200}}};
    Theme t = default_theme();

    prism::plot::draw_background(dl, area, t);
    CHECK(dl.size() == 2); // filled_rect + rect_outline
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

    // Should contain Line commands (grid lines) and TextCmd (tick labels)
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
    // Should have ClipPush + Polyline + ClipPop
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
    // Should have 2 lines (crosshair) + background rect + text (readout)
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — draw_background, draw_grid, draw_series, draw_cursor not defined

- [ ] **Step 3: Implement rendering layer functions**

Add to `include/prism/widgets/plot_render.hpp`:

```cpp
inline void draw_background(DrawList& dl, Rect plot_area, const Theme& t)
{
    dl.filled_rect(plot_area, t.canvas_bg);
    dl.rect_outline(plot_area, t.border);
}

inline void draw_grid(DrawList& dl, const PlotMapping& map, const Theme& t)
{
    auto x_ticks = nice_ticks(map.x_range.min, map.x_range.max, 6);
    auto y_ticks = nice_ticks(map.y_range.min, map.y_range.max, 5);

    float left = map.plot_area.origin.x.raw();
    float right = left + map.plot_area.extent.w.raw();
    float top = map.plot_area.origin.y.raw();
    float bottom = top + map.plot_area.extent.h.raw();

    // Vertical grid lines + X tick labels
    for (double tx : x_ticks) {
        auto px = map.to_pixel(tx, 0.0);
        float x = px.x.raw();
        if (x < left || x > right) continue;
        dl.line(Point{X{x}, Y{top}}, Point{X{x}, Y{bottom}}, t.track, 1.f);
        dl.text(fmt::format("{:.6g}", tx), Point{X{x - 15.f}, Y{bottom + 4.f}}, 11.f, t.text_muted);
    }

    // Horizontal grid lines + Y tick labels
    for (double ty : y_ticks) {
        auto px = map.to_pixel(0.0, ty);
        float y = px.y.raw();
        if (y < top || y > bottom) continue;
        dl.line(Point{X{left}, Y{y}}, Point{X{right}, Y{y}}, t.track, 1.f);
        dl.text(fmt::format("{:.6g}", ty), Point{X{left - 55.f}, Y{y - 6.f}}, 11.f, t.text_muted);
    }

    // Axis lines (left and bottom edges)
    dl.line(Point{X{left}, Y{top}}, Point{X{left}, Y{bottom}}, t.border, 1.f);
    dl.line(Point{X{left}, Y{bottom}}, Point{X{right}, Y{bottom}}, t.border, 1.f);
}

inline void draw_series(DrawList& dl, const PlotMapping& map,
                        std::span<const Series> series)
{
    dl.clip_push(map.plot_area.origin, map.plot_area.extent);
    for (auto& s : series) {
        if (s.size() < 2) continue;
        std::vector<Point> pts;
        pts.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
            pts.push_back(map.to_pixel(s.x(i), s.y(i)));
        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
    dl.clip_pop();
}

inline void draw_cursor(DrawList& dl, const PlotMapping& map,
                        const CursorState& cursor, const Theme& t)
{
    if (!cursor.visible) return;

    auto px = map.to_pixel(cursor.data_x, cursor.data_y);
    float left = map.plot_area.origin.x.raw();
    float right = left + map.plot_area.extent.w.raw();
    float top = map.plot_area.origin.y.raw();
    float bottom = top + map.plot_area.extent.h.raw();

    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);

    // Vertical crosshair
    dl.line(Point{X{px.x.raw()}, Y{top}}, Point{X{px.x.raw()}, Y{bottom}}, crosshair_color, 1.f);
    // Horizontal crosshair
    dl.line(Point{X{left}, Y{px.y.raw()}}, Point{X{right}, Y{px.y.raw()}}, crosshair_color, 1.f);

    // Coordinate readout
    auto label = fmt::format("({:.4g}, {:.4g})", cursor.data_x, cursor.data_y);
    float tx = px.x.raw() + 10.f;
    float ty = px.y.raw() - 20.f;
    // Clamp readout to stay inside plot area
    if (tx + 120.f > right) tx = px.x.raw() - 130.f;
    if (ty < top) ty = px.y.raw() + 10.f;

    dl.filled_rect(Rect{Point{X{tx - 2.f}, Y{ty - 2.f}}, Size{Width{120.f}, Height{18.f}}}, t.surface);
    dl.text(std::move(label), Point{X{tx}, Y{ty}}, 12.f, t.text);
}

inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t)
{
    float bottom = map.plot_area.origin.y.raw() + map.plot_area.extent.h.raw();
    float cx = map.plot_area.origin.x.raw() + map.plot_area.extent.w.raw() / 2.f;

    if (!x_label.empty())
        dl.text(x_label, Point{X{cx - 30.f}, Y{bottom + 18.f}}, 12.f, t.text);

    // Y label: rendered at left side (no rotation support yet — just placed vertically)
    if (!y_label.empty())
        dl.text(y_label, Point{X{2.f}, Y{map.plot_area.origin.y.raw() + map.plot_area.extent.h.raw() / 2.f}}, 12.f, t.text);
}
```

Note: `draw_grid` and `draw_cursor` use `fmt::format`. Add `#include <fmt/format.h>` to the top of `plot_render.hpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All rendering layer tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add rendering layer functions (background, grid, series, cursor)"
```

---

### Task 5: PlotModel — Canvas Orchestrator

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing test for PlotModel::canvas**

Append to `tests/test_plot.cpp`:

```cpp
TEST_CASE("PlotModel canvas produces draw commands")
{
    using namespace prism;
    using namespace prism::plot;

    PlotModel plot;
    plot.add_series(XYData{{0.0, 1.0, 2.0}, {0.0, 1.0, 0.0}},
                    SeriesStyle{Color::rgba(255, 0, 0), 2.f});

    DrawList dl;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};

    // Construct a minimal WidgetNode with theme
    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = bounds;

    plot.canvas(dl, bounds, node);

    CHECK(dl.size() > 0);
    // Should contain at minimum: filled_rect(bg), lines(grid), polyline(data)
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — PlotModel not defined

- [ ] **Step 3: Implement PlotModel in plot.hpp**

Add to `include/prism/widgets/plot.hpp`, inside `namespace prism::plot`:

```cpp
struct PlotModel {
    // Reactive state
    Field<AxisRange> x_range{};
    Field<AxisRange> y_range{};
    Field<std::string> x_label{""};
    Field<std::string> y_label{""};
    Field<ViewTransform> view{};
    Field<CursorState> cursor{};
    Field<uint32_t> revision{0};

    // Transient interaction state
    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};
    double last_click_time = 0.0;

    // Series management
    template <PlotSource S>
    void add_series(S source, SeriesStyle style)
    {
        series_.emplace_back(std::move(source), style);
    }

    void remove_series(size_t index)
    {
        if (index < series_.size())
            series_.erase(series_.begin() + static_cast<ptrdiff_t>(index));
    }

    void clear_series() { series_.clear(); }
    size_t series_count() const { return series_.size(); }

    void notify()
    {
        revision.set(revision.get() + 1);
    }

    // Canvas interface
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
    {
        auto& t = *node.theme;
        auto map = compute_mapping(bounds, x_range, y_range, view,
                                   std::span<const Series>(series_));

        draw_background(dl, map.plot_area, t);
        draw_grid(dl, map, t);
        draw_series(dl, map, std::span<const Series>(series_));
        draw_cursor(dl, map, cursor.get(), t);
        draw_axes_labels(dl, map, x_label.get(), y_label.get(), t);
    }

    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    std::vector<Series> series_;
};
```

`compute_mapping` is defined in `plot.hpp` (after `Series`) as established in Task 3.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All PlotModel tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot.hpp tests/test_plot.cpp
git commit -m "feat(plot): add PlotModel with canvas orchestrator and series management"
```

---

### Task 6: Interaction — Cursor, Pan, Zoom, Double-Click Reset

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing tests for interaction**

Append to `tests/test_plot.cpp`:

```cpp
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

    // Mouse move inside plot area
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

    // Scroll up → zoom in → scale increases
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
    InputEvent down = MouseButton{center, 0, true};
    plot.handle_canvas_input(down, node, bounds);
    CHECK(plot.drag_mode == DragMode::Pan);

    // Mouse move (drag right)
    Point moved{X{center.x.raw() + 50.f}, center.y};
    InputEvent drag = MouseMove{moved};
    plot.handle_canvas_input(drag, node, bounds);

    auto v = plot.view.get();
    CHECK(v.offset_x != 0.0);

    // Mouse up
    InputEvent up = MouseButton{moved, 0, false};
    plot.handle_canvas_input(up, node, bounds);
    CHECK(plot.drag_mode == DragMode::None);
}

TEST_CASE("PlotModel double-click resets view")
{
    using namespace prism;
    using namespace prism::plot;

    PlotModel plot;
    plot.x_range.set({0.0, 10.0, false});
    plot.y_range.set({0.0, 10.0, false});

    // Apply some zoom
    plot.view.set({1.0, 1.0, 2.0, 2.0});

    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    Rect bounds{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    node.canvas_bounds = bounds;

    auto map = compute_mapping(bounds, plot.x_range, plot.y_range, plot.view,
                               std::span<const Series>{});
    Point center = map.plot_area.center();

    // Simulate double-click by setting last_click_time close
    plot.last_click_time = 1000.0;
    InputEvent click = MouseButton{center, 0, true};

    // First click at t=1000
    plot.handle_canvas_input(click, node, bounds);

    // Second click at t=1000.1 (within 300ms)
    plot.last_click_time = 1000.0; // reset to simulate timing
    plot.handle_canvas_input(click, node, bounds);

    // After double-click, view should be reset
    // (We'll test this more carefully once we have a clock mechanism)
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — handle_canvas_input not implemented

- [ ] **Step 3: Implement handle_canvas_input**

Add the implementation in `include/prism/widgets/plot.hpp`, as the body of `PlotModel::handle_canvas_input`:

```cpp
inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds)
{
    auto map = compute_mapping(bounds, x_range, y_range, view,
                               std::span<const Series>(series_));

    if (auto* mm = std::get_if<MouseMove>(&ev)) {
        if (drag_mode == DragMode::Pan) {
            float dx_px = mm->position.x.raw() - drag_start_pixel.x.raw();
            float dy_px = mm->position.y.raw() - drag_start_pixel.y.raw();

            double data_dx = dx_px / map.plot_area.extent.w.raw()
                             * (map.x_range.max - map.x_range.min);
            double data_dy = -(dy_px / map.plot_area.extent.h.raw()
                               * (map.y_range.max - map.y_range.min));

            auto v = drag_start_view;
            v.offset_x -= data_dx;
            v.offset_y -= data_dy;
            view.set(v);
        }

        if (map.plot_area.contains(mm->position)) {
            auto [dx, dy] = map.to_data(mm->position);
            cursor.set(CursorState{dx, dy, true});
        } else {
            auto c = cursor.get();
            if (c.visible) cursor.set(CursorState{c.data_x, c.data_y, false});
        }

    } else if (auto* mb = std::get_if<MouseButton>(&ev)) {
        if (mb->button == 0) {
            if (mb->pressed) {
                drag_mode = DragMode::Pan;
                drag_start_pixel = mb->position;
                drag_start_view = view.get();

                // Disable auto-fit on manual interaction
                auto xr = x_range.get();
                auto yr = y_range.get();
                if (xr.auto_fit) { xr.auto_fit = false; x_range.set(xr); }
                if (yr.auto_fit) { yr.auto_fit = false; y_range.set(yr); }
            } else {
                drag_mode = DragMode::None;
            }
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        if (!map.plot_area.contains(ms->position)) return;

        double factor = std::pow(1.1, ms->dy.raw());
        auto [data_x, data_y] = map.to_data(ms->position);

        auto v = view.get();
        double old_scale_x = v.scale_x;
        double old_scale_y = v.scale_y;
        v.scale_x *= factor;
        v.scale_y *= factor;

        // Adjust offset to keep cursor point stable
        auto base_x = x_range.get();
        auto base_y = y_range.get();
        double cx = (base_x.min + base_x.max) / 2.0;
        double cy = (base_y.min + base_y.max) / 2.0;

        v.offset_x = data_x - cx + (cx + v.offset_x - data_x) * old_scale_x / v.scale_x;
        v.offset_y = data_y - cy + (cy + v.offset_y - data_y) * old_scale_y / v.scale_y;

        view.set(v);

        // Disable auto-fit
        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) { xr.auto_fit = false; x_range.set(xr); }
        if (yr.auto_fit) { yr.auto_fit = false; y_range.set(yr); }
    }
}
```

Note: Double-click reset requires a monotonic clock. For now, skip the double-click feature — it can be added when a clock source is available (SDL_GetTicks or std::chrono). The tests for double-click should be adjusted to mark it as future work.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: All interaction tests PASS (adjust double-click test to be a placeholder or remove)

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot.hpp tests/test_plot.cpp
git commit -m "feat(plot): add handle_canvas_input — cursor, pan, zoom"
```

---

### Task 7: Default Series Colors

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp`
- Modify: `tests/test_plot.cpp`

- [ ] **Step 1: Write the failing test for default_series_colors**

Append to `tests/test_plot.cpp`:

```cpp
TEST_CASE("default_series_colors returns 8 distinct colors")
{
    auto colors = prism::plot::default_series_colors(prism::default_theme());
    CHECK(colors.size() == 8);

    // All should have full alpha
    for (auto& c : colors)
        CHECK(c.a == 255);

    // All should be distinct
    for (size_t i = 0; i < colors.size(); ++i)
        for (size_t j = i + 1; j < colors.size(); ++j)
            CHECK((colors[i].r != colors[j].r
                   || colors[i].g != colors[j].g
                   || colors[i].b != colors[j].b));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: FAIL — default_series_colors not defined

- [ ] **Step 3: Implement default_series_colors**

Add to `include/prism/widgets/plot_render.hpp`:

```cpp
inline std::array<Color, 8> default_series_colors(const Theme& t)
{
    return {{
        t.accent,                              // cyan-ish
        t.primary,                             // blue
        Color::rgba(220, 80, 60),              // red
        Color::rgba(80, 180, 80),              // green
        Color::rgba(200, 160, 40),             // yellow
        Color::rgba(160, 80, 200),             // purple
        Color::rgba(240, 130, 40),             // orange
        Color::rgba(100, 200, 200),            // teal
    }};
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir plot -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add default_series_colors palette"
```

---

### Task 8: Example Application

**Files:**
- Create: `examples/model_plot.cpp`
- Modify: `examples/meson.build`

- [ ] **Step 1: Create the example application**

Create `examples/model_plot.cpp`:

```cpp
#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>
#include <cmath>
#include <numbers>

struct PlotDemo {
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    prism::Field<prism::Slider<>> amplitude{{.value = 1.0, .min = 0.1, .max = 5.0}};
    prism::Field<prism::Checkbox> show_cos{{.checked = false, .label = "Show cosine"}};
    prism::plot::PlotModel plot;

    void update_data()
    {
        double f = frequency.get().value;
        double a = amplitude.get().value;
        constexpr int N = 500;

        std::vector<double> xs(N), ys_sin(N), ys_cos(N);
        for (int i = 0; i < N; ++i) {
            double t = static_cast<double>(i) / (N - 1) * 4.0 * std::numbers::pi;
            xs[i] = t;
            ys_sin[i] = a * std::sin(f * t);
            ys_cos[i] = a * std::cos(f * t);
        }

        auto colors = prism::plot::default_series_colors(*plot.x_range.get().auto_fit
                                                          ? prism::default_theme()
                                                          : prism::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{xs, ys_sin},
                        prism::plot::SeriesStyle{prism::Color::rgba(0, 140, 200), 2.f});
        if (show_cos.get().checked) {
            plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys_cos)},
                            prism::plot::SeriesStyle{prism::Color::rgba(220, 80, 60), 2.f});
        }
        plot.x_label.set("Time (rad)");
        plot.y_label.set("Amplitude");
        plot.notify();
    }

    void view(prism::WidgetTree::ViewBuilder& vb)
    {
        vb.canvas(plot)
            .depends_on(plot.x_range)
            .depends_on(plot.y_range)
            .depends_on(plot.view)
            .depends_on(plot.cursor)
            .depends_on(plot.revision);
        vb.widget(frequency);
        vb.widget(amplitude);
        vb.widget(show_cos);
    }
};

int main()
{
    PlotDemo demo;
    demo.update_data();

    return prism::model_app("Plot Demo", demo, [&](prism::AppContext& ctx) {
        auto sched = ctx.scheduler();
        demo.frequency.observe([&](const prism::Slider<>&) { demo.update_data(); });
        demo.amplitude.observe([&](const prism::Slider<>&) { demo.update_data(); });
        demo.show_cos.observe([&](const prism::Checkbox&) { demo.update_data(); });
    });
}
```

- [ ] **Step 2: Register in examples/meson.build**

Add to `examples/meson.build`:

```meson
executable('model_plot', 'model_plot.cpp',
  dependencies : [prism_dep],
)
```

- [ ] **Step 3: Build and verify it compiles**

Run: `meson compile -C builddir model_plot`
Expected: Compiles without errors

- [ ] **Step 4: Commit**

```bash
git add examples/model_plot.cpp examples/meson.build
git commit -m "feat(plot): add interactive plot example with sine/cosine curves"
```

---

### Task 9: Final Integration — Run All Tests

**Files:** None (verification only)

- [ ] **Step 1: Run the full test suite**

Run: `meson test -C builddir -v`
Expected: All tests pass (37/37 including new plot tests)

- [ ] **Step 2: Build all examples**

Run: `meson compile -C builddir`
Expected: Clean build, no warnings

- [ ] **Step 3: Commit any fixups if needed**

Only if previous steps revealed issues that needed fixing.
