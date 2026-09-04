# Plot Decimation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make dense monotonic-x plot series render at cost proportional to pixel width (min-max per-column decimation in `draw_series`), eliminating the perf-lab-measured ~260 ms/publish 1M-point path case.

**Architecture:** Two pure helpers in `include/prism/widgets/plot_render.hpp` (monotonic-x gate + min-max bucketing), engaged by `draw_series` only when `size > 4 × plot width`; every other case takes the existing path byte-identically. Spec: `docs/superpowers/specs/2026-09-04-plot-decimation-design.md`.

**Tech Stack:** C++26, Meson, doctest.

## Global Constraints

- Build: `ninja -C builddir` — one build/test invocation at a time, foreground, never backgrounded (AGENTS.md).
- Full suite: `meson test -C builddir` — green after the task (77/77).
- Tests use doctest: append plain `TEST_CASE`s to the existing `tests/test_plot.cpp`.
- Small plots must be bit-identical: the regenerated SVG screenshots (`model_plot` N=500, `showcase_plot` N=200, `perf_lab` N=2000 — all below threshold) must not change checksum.
- Commit explicitly listed files only; never `git add .`.
- Reference points: `draw_series` at `include/prism/widgets/plot_render.hpp:220-243`; `Polyline{points,color,thickness}` / `FilledPolygon{points,color}` at `include/prism/render/draw_list.hpp:58-76`; `PlotMapping` at `plot_render.hpp:28-68`.

---

### Task 1: Min-max decimation in `draw_series`

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp` (two helpers + `draw_series` rewrite)
- Test: `tests/test_plot.cpp` (append TEST_CASEs)

**Interfaces:**
- Consumes: nothing new.
- Produces (namespace `prism::plot`, used by tests and potentially future widgets):
  - `template <typename S> bool series_is_monotonic_x(const S& s)`
  - `template <typename S> std::vector<Point> decimated_series_points(const S& s, const PlotMapping& map)`

- [ ] **Step 1: Record pre-change SVG checksums**

Run: `ninja -C builddir examples/model_plot/model_plot.svg examples/showcase/showcase_plot.svg examples/perf_lab/perf_lab.svg && sha256sum builddir/examples/model_plot/model_plot.svg builddir/examples/showcase/showcase_plot.svg builddir/examples/perf_lab/perf_lab.svg > /tmp/prism_svg_before.txt && cat /tmp/prism_svg_before.txt`
Expected: three checksums recorded. (All three plots are below the decimation threshold, so these MUST be identical after the change.)

- [ ] **Step 2: Write the failing tests**

Append to `tests/test_plot.cpp`:

```cpp
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
```

(`std::nan("")` needs `<cmath>` and `std::numeric_limits` needs `<limits>` — add both includes at the top of the file if not already present; `<array>` for the Series range.)

- [ ] **Step 3: Run tests to verify they fail**

Run: `ninja -C builddir tests/test_plot && ./builddir/tests/test_plot`
Expected: FAIL — compile error, `series_is_monotonic_x` / `decimated_series_points` are not members of `prism::plot`.

- [ ] **Step 4: Implement the helpers**

In `include/prism/widgets/plot_render.hpp`, insert immediately before `draw_series` (line 220):

```cpp
// Gate for min-max decimation: per-column bucketing would turn a non-monotonic
// series' back-and-forth lines into an envelope — a silent visual change, so dense
// decimation is only allowed for non-decreasing x.
template <typename S>
bool series_is_monotonic_x(const S& s)
{
    for (size_t i = 1; i < s.size(); ++i)
        if (s.x(i) < s.x(i - 1)) return false;
    return true;
}

// Min-max decimation: the min-y and max-y pixel point of each pixel column, in column
// order — the exact visual envelope of the full polyline at this density, at
// <= 2 points per column. Only meaningful for monotonic-x series (see the gate above).
// NaN samples are skipped, so a NaN-only column vanishes (a gap) instead of drawing
// a garbage point.
template <typename S>
std::vector<Point> decimated_series_points(const S& s, const PlotMapping& map)
{
    const float left = map.left().raw();
    const int columns = std::max(1, static_cast<int>(map.right().raw() - left));

    struct Bucket {
        Point min_pt{}, max_pt{};
        size_t min_i = 0, max_i = 0;
        bool any = false;
    };
    std::vector<Bucket> buckets(static_cast<size_t>(columns));

    for (size_t i = 0; i < s.size(); ++i) {
        const Point p = map.to_pixel(s.x(i), s.y(i));
        if (std::isnan(p.x.raw()) || std::isnan(p.y.raw())) continue;
        // Points outside the visible x-range fold into the edge columns, keeping the
        // polyline continuous instead of opening a gap at the boundary.
        const int col = std::clamp(static_cast<int>(p.x.raw() - left), 0, columns - 1);
        auto& b = buckets[static_cast<size_t>(col)];
        if (!b.any || p.y.raw() < b.min_pt.y.raw()) { b.min_pt = p; b.min_i = i; }
        if (!b.any || p.y.raw() > b.max_pt.y.raw()) { b.max_pt = p; b.max_i = i; }
        b.any = true;
    }

    std::vector<Point> out;
    out.reserve(static_cast<size_t>(columns) * 2);
    for (const auto& b : buckets) {
        if (!b.any) continue;
        if (b.min_i == b.max_i) {
            out.push_back(b.min_pt);
        } else if (b.min_i < b.max_i) {
            out.push_back(b.min_pt);
            out.push_back(b.max_pt);
        } else {
            out.push_back(b.max_pt);
            out.push_back(b.min_pt);
        }
    }
    return out;
}
```

- [ ] **Step 5: Rewrite `draw_series`**

Replace the whole existing `draw_series` (`plot_render.hpp:220-243`) with:

```cpp
template <typename SeriesRange>
void draw_series(DrawList& dl, const PlotMapping& map, const SeriesRange& series)
{
    for (auto& s : series) {
        if (s.size() < 2) continue;

        // A series much denser than the plot is wide pays O(N) transform + allocation
        // here and O(N) per-segment rendering in the backend, for output no pixel can
        // show (measured by the perf lab: ~260 ms/publish at 1M points). Min-max
        // decimation is the exact visual envelope at <= 2 points per pixel column;
        // non-monotonic-x series keep the full-fidelity path — their back-and-forth
        // line shape IS the data.
        const float plot_w = map.right().raw() - map.left().raw();
        const bool decimate = plot_w > 0.f
            && s.size() > static_cast<size_t>(plot_w) * 4
            && series_is_monotonic_x(s);

        std::vector<Point> pts;
        if (decimate) {
            pts = decimated_series_points(s, map);
        } else {
            pts.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i)
                pts.push_back(map.to_pixel(s.x(i), s.y(i)));
        }

        if (s.style().fill) {
            std::vector<Point> strip;
            strip.reserve(pts.size() * 2);
            const Y baseline_y = map.to_pixel(0.0, s.style().baseline).y;
            for (const auto& p : pts) {
                strip.push_back(p);
                strip.push_back(Point{p.x, baseline_y});
            }
            Color c = s.style().color;
            dl.filled_polygon(std::move(strip), Color::rgba(c.r, c.g, c.b, 40));
        }

        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
}
```

Note for the implementer: the restructured fill path is bit-identical to the old one for non-decimated series — `to_pixel(x, baseline).y` is x-independent, and the baseline point's x equals the data point's x. Do not "simplify" the non-decimate branch differently.

- [ ] **Step 6: Run the tests**

Run: `ninja -C builddir tests/test_plot && ./builddir/tests/test_plot && meson test -C builddir`
Expected: new cases PASS, full suite green (77/77).

- [ ] **Step 7: Verify small-plot output is bit-identical**

Run: `ninja -C builddir examples/model_plot/model_plot.svg examples/showcase/showcase_plot.svg examples/perf_lab/perf_lab.svg && sha256sum -c /tmp/prism_svg_before.txt`
Expected: all three OK. If any checksum changed, STOP — the non-decimate path is not behavior-identical; find the difference before continuing.

- [ ] **Step 8: Perf-lab verification (the point of the change)**

Run: `./builddir/examples/perf_lab/perf_lab --rows 100000 --points 1000000 --rate 1000 --headless 4 2>&1 | grep -v warning`
Baseline before this change: 3.6 publishes/s, median build 260.7 ms, last frame 7.7 MB.
Success criteria: publishes > 20/s, median build < 60 ms, last frame < 500 KB.
(The residual build time is expected to be the two `auto_fit_range` O(N) scans — a documented non-goal of this change, not a failure.)

Run: `./builddir/examples/perf_lab/perf_lab --rows 100000 --points 5000 --rate 1000 --headless 4 2>&1 | grep -v warning`
Expected: median build ~1 ms (no regression on the table-heavy path).

- [ ] **Step 9: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "perf(widgets): min-max decimation for dense monotonic-x plot series"
```
