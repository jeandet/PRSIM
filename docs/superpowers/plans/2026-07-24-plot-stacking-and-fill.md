# Plot Widget: Stacked/Linked Plots + Fill-Under-Curve Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `PlotModel`-based plots be stacked with a shared, linked x-axis (pan/zoom/crosshair synced, only the bottom panel shows x-axis ticks) and let any series render a filled area under its curve.

**Architecture:** Extract `PlotModel::canvas()`/`handle_canvas_input()`'s bodies into two free functions (`render_plot_panel`, `route_plot_input`) templated over the cursor type via a `PlotCursor` concept. `PlotModel` (standalone) and the new `PlotGroup`/`PlotPanel` (stacked, sharing x-state) both call the same free functions — `PlotModel` passing its own owned fields, `PlotPanel` passing its owning `PlotGroup`'s shared fields. Fill-under-curve is a new `FilledPolygon` triangle-strip draw primitive plus a `SeriesStyle.fill`/`baseline` opt-in.

**Tech Stack:** C++26 (reflection), Meson build, doctest, SDL3/SDL3_ttf (`SDL_RenderGeometry`), headless `SoftwareRenderer`, SVG export.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-24-plot-stacking-and-fill-design.md` — read it if anything below is ambiguous.
- TDD: write the failing test before the implementation for every behavior change; run it, confirm it fails, then implement.
- No new source files — every change lands in an existing header/cpp/test file.
- `PlotModel`'s public behavior must be bit-for-bit unchanged after Task 3's refactor — the existing `test_plot.cpp` suite is the safety net; all pre-existing cases there must keep passing with zero edits to their assertions.
- After every task: run the task's own test target, then before the final commit of the whole plan run the **full** suite (`meson test -C builddir`) and read the literal pass/fail counts — don't infer success from a partial grep.
- Build directory is `builddir` (already configured at the repo root).

---

### Task 1: `FilledPolygon` draw primitive

**Files:**
- Modify: `include/prism/render/draw_list.hpp`
- Modify: `include/prism/render/software_renderer.hpp`
- Modify: `include/prism/render/svg_export.hpp`
- Modify: `include/prism/backends/sdl_window.hpp`
- Modify: `src/backends/sdl_window.cpp`
- Test: `tests/test_draw_list.cpp`
- Test: `tests/test_svg_export.cpp`

**Interfaces:**
- Produces: `struct FilledPolygon { std::vector<Point> points; Color color; };` (added to the `DrawCmd` variant) and `DrawList::filled_polygon(std::vector<Point> pts, Color c)`. **Semantics: `points` is a triangle strip** (triangle `i` is vertices `(i, i+1, i+2)` for every `i` in `[0, points.size()-3]`) — the same generic primitive as a GPU triangle strip, not assumed to be any particular shape. Consumed by Task 2's `draw_series`.

- [ ] **Step 1: Write the failing tests in `tests/test_draw_list.cpp`**

Add at the end of the file (after the last `TEST_CASE`, following the existing `polyline`/`circle` test pattern in that file):

```cpp
TEST_CASE("DrawList filled_polygon command") {
    prism::DrawList dl;
    std::vector<prism::Point> pts = {P(0, 0), P(0, 10), P(10, 0), P(10, 10)};
    dl.filled_polygon(pts, prism::Color::rgba(0, 128, 255));
    REQUIRE(dl.size() == 1);
    auto& fp = std::get<prism::FilledPolygon>(dl.commands[0]);
    CHECK(fp.points.size() == 4);
    CHECK(fp.points[2].x.raw() == 10.f);
    CHECK(fp.color.b == 255);
}

TEST_CASE("clip_push offsets filled_polygon points") {
    prism::DrawList dl;
    dl.clip_push(P(5.f, 10.f), S(200.f, 200.f));
    std::vector<prism::Point> pts = {P(0, 0), P(20, 30)};
    dl.filled_polygon(pts, prism::Color::rgba(0, 0, 0));
    dl.clip_pop();
    auto& fp = std::get<prism::FilledPolygon>(dl.commands[1]);
    CHECK(fp.points[0].x.raw() == 5.f);
    CHECK(fp.points[0].y.raw() == 10.f);
    CHECK(fp.points[1].x.raw() == 25.f);
    CHECK(fp.points[1].y.raw() == 40.f);
}

TEST_CASE("bounding_box includes all filled_polygon points") {
    prism::DrawList dl;
    std::vector<prism::Point> pts = {P(10, 50), P(90, 10), P(50, 80)};
    dl.filled_polygon(pts, prism::Color::rgba(0, 0, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 10.f);
    CHECK(bb.origin.y.raw() == 10.f);
    CHECK(bb.extent.w.raw() == 80.f);
    CHECK(bb.extent.h.raw() == 70.f);
}
```

Add to `tests/test_svg_export.cpp` (after the last `TEST_CASE`):

```cpp
TEST_CASE("FilledPolygon emits one triangle per 3 consecutive strip points") {
    prism::DrawList dl;
    dl.filled_polygon({P(0, 0), P(10, 0), P(5, 10)}, prism::Color::rgba(0, 128, 0, 100));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<polygon") != std::string::npos);
    CHECK(svg.find("points=\"0,0 10,0 5,10\"") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(0,128,0,") != std::string::npos);
}

TEST_CASE("FilledPolygon strip of 4 points emits two triangles") {
    prism::DrawList dl;
    dl.filled_polygon({P(0, 0), P(0, 10), P(10, 0), P(10, 10)}, prism::Color::rgba(255, 0, 0));
    auto svg = prism::to_svg(dl);
    size_t count = 0, pos = 0;
    while ((pos = svg.find("<polygon", pos)) != std::string::npos) { ++count; pos += 8; }
    CHECK(count == 2);
}
```

- [ ] **Step 2: Run the new tests to verify they fail to compile**

Run: `meson test -C builddir draw_list svg_export -v`
Expected: build error — `FilledPolygon`/`filled_polygon` not declared.

- [ ] **Step 3: Add `FilledPolygon` to `draw_list.hpp`**

In `include/prism/render/draw_list.hpp`, add this struct right after `struct Circle { ... };` (currently lines 64-69):

```cpp
struct FilledPolygon {
    // Vertices in triangle-strip order: triangle i is (i, i+1, i+2) for every
    // i in [0, points.size()-3] -- the same generic primitive as a GPU triangle strip.
    std::vector<Point> points;
    Color color;
};
```

Add `FilledPolygon` to the end of the `DrawCmd` variant:

```cpp
using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle, FilledPolygon>;
```

Add a `filled_polygon()` method to `DrawList`, right after `polyline()` (mirrors it exactly, no thickness parameter):

```cpp
void filled_polygon(std::vector<Point> pts, Color c)
{
    auto o = current_offset();
    for (auto& p : pts)
        p = Point{p.x + o.dx, p.y + o.dy};
    commands.emplace_back(FilledPolygon{std::move(pts), c});
}
```

`bounding_box()` needs no change: its `std::visit` lambda already has an `else if constexpr (requires { c.points; })` branch that expands every point, and `FilledPolygon` has a `.points` member — the new `bounding_box` test in Step 1 proves this without touching the function.

- [ ] **Step 4: Add the headless no-op stub so `SoftwareRenderer`'s visitor stays exhaustive**

In `include/prism/render/software_renderer.hpp`, add after `void rasterise(const Circle&) {}      // POC: skip circles`:

```cpp
    void rasterise(const FilledPolygon&) {} // POC: skip filled polygons
```

- [ ] **Step 5: Implement SVG emission in `svg_export.hpp`**

In `include/prism/render/svg_export.hpp`, add to `struct SvgEmitter`, right after `void emit(const Circle& c) { ... }`:

```cpp
    void emit(const FilledPolygon& c) {
        for (std::size_t i = 0; i + 2 < c.points.size(); ++i) {
            out << "<polygon points=\""
                << fmt_float(c.points[i].x.raw()) << "," << fmt_float(c.points[i].y.raw()) << " "
                << fmt_float(c.points[i + 1].x.raw()) << "," << fmt_float(c.points[i + 1].y.raw()) << " "
                << fmt_float(c.points[i + 2].x.raw()) << "," << fmt_float(c.points[i + 2].y.raw())
                << "\" fill=\"" << fmt_color(c.color) << "\"/>\n";
        }
    }
```

- [ ] **Step 6: Run the new draw_list/svg_export tests to verify they pass**

Run: `meson test -C builddir draw_list svg_export -v`
Expected: all cases PASS, including the 5 new ones.

- [ ] **Step 7: Add the SDL backend implementation**

In `include/prism/backends/sdl_window.hpp`, add after `void render_cmd(const Circle& cmd);` (currently line 98):

```cpp
    void render_cmd(const FilledPolygon& cmd);
```

In `src/backends/sdl_window.cpp`, add after `void SdlWindow::render_cmd(const Circle& cmd) { ... }` (the full Bresenham-circle function ending around line 410):

```cpp
void SdlWindow::render_cmd(const FilledPolygon& cmd) {
    size_t n = cmd.points.size();
    if (n < 3) return;
    SDL_FColor col{cmd.color.r / 255.f, cmd.color.g / 255.f, cmd.color.b / 255.f, cmd.color.a / 255.f};
    std::vector<SDL_Vertex> verts(n);
    for (size_t i = 0; i < n; ++i)
        verts[i] = SDL_Vertex{{cmd.points[i].x.raw(), cmd.points[i].y.raw()}, col, {0.f, 0.f}};

    std::vector<int> indices;
    indices.reserve((n - 2) * 3);
    for (size_t i = 0; i + 2 < n; ++i) {
        indices.push_back(static_cast<int>(i));
        indices.push_back(static_cast<int>(i + 1));
        indices.push_back(static_cast<int>(i + 2));
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), static_cast<int>(n),
                      indices.data(), static_cast<int>(indices.size()));
}
```

This uses `SDL_Vertex{SDL_FPoint position; SDL_FColor color; SDL_FPoint tex_coord;}` and `SDL_RenderGeometry(renderer, texture, vertices, num_vertices, indices, num_indices)` — both confirmed present in the vendored SDL3 headers (`SDL3/SDL_render.h`, `SDL3/SDL_pixels.h`). No new backend is added to the visitor dispatch loop — `render_draw_list`'s `std::visit` already calls `render_cmd(c)` generically for any non-`TextCmd` alternative, so this overload is picked up automatically.

- [ ] **Step 8: Build the whole project to confirm every `DrawCmd` visitor is still exhaustive**

Run: `meson compile -C builddir`
Expected: clean build, no "not all control paths" / missing-overload errors from any `std::visit` site (draw_list bounding_box, software_renderer, svg_export, sdl_window all now handle `FilledPolygon`).

- [ ] **Step 9: Commit**

```bash
git add include/prism/render/draw_list.hpp include/prism/render/software_renderer.hpp \
        include/prism/render/svg_export.hpp include/prism/backends/sdl_window.hpp \
        src/backends/sdl_window.cpp tests/test_draw_list.cpp tests/test_svg_export.cpp
git commit -m "feat(render): add FilledPolygon triangle-strip draw primitive"
```

---

### Task 2: Fill-under-curve series style

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Modify: `include/prism/widgets/plot_render.hpp`
- Test: `tests/test_plot.cpp`

**Interfaces:**
- Consumes: `DrawList::filled_polygon(std::vector<Point>, Color)` (Task 1).
- Produces: `SeriesStyle{ Color color; float thickness; bool fill = false; double baseline = 0.0; }`. `draw_series` emits a `FilledPolygon` before the `Polyline` for any series with `fill == true`.

- [ ] **Step 1: Write the failing test in `tests/test_plot.cpp`**

Add after `TEST_CASE("draw_series emits polylines for each series")` (ends around line 234):

```cpp
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

    // Series without fill emits only the Polyline
    DrawList dl2;
    Series s2(XYData{{0.0, 1.0}, {0.0, 1.0}}, SeriesStyle{});
    std::array<Series, 1> arr2 = {std::move(s2)};
    draw_series(dl2, map, arr2);
    CHECK(dl2.size() == 1);
    CHECK(std::holds_alternative<Polyline>(dl2.commands[0]));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `meson test -C builddir plot -v`
Expected: build error — `SeriesStyle` has no 4-argument aggregate init (no `fill`/`baseline` members yet).

- [ ] **Step 3: Add `fill`/`baseline` to `SeriesStyle` in `plot.hpp`**

Replace (lines 34-37):

```cpp
struct SeriesStyle {
    Color color = Color::rgba(0, 140, 200);
    float thickness = 2.f;
};
```

with:

```cpp
struct SeriesStyle {
    Color color = Color::rgba(0, 140, 200);
    float thickness = 2.f;
    bool fill = false;
    double baseline = 0.0;
};
```

- [ ] **Step 4: Emit the fill polygon in `draw_series` (`plot_render.hpp`)**

Replace the `draw_series` function body (currently lines 200-211):

```cpp
template <typename SeriesRange>
void draw_series(DrawList& dl, const PlotMapping& map, const SeriesRange& series)
{
    for (auto& s : series) {
        if (s.size() < 2) continue;
        std::vector<Point> pts;
        pts.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
            pts.push_back(map.to_pixel(s.x(i), s.y(i)));
        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
}
```

with:

```cpp
template <typename SeriesRange>
void draw_series(DrawList& dl, const PlotMapping& map, const SeriesRange& series)
{
    for (auto& s : series) {
        if (s.size() < 2) continue;

        if (s.style().fill) {
            std::vector<Point> strip;
            strip.reserve(s.size() * 2);
            for (size_t i = 0; i < s.size(); ++i) {
                strip.push_back(map.to_pixel(s.x(i), s.y(i)));
                strip.push_back(map.to_pixel(s.x(i), s.style().baseline));
            }
            Color c = s.style().color;
            dl.filled_polygon(std::move(strip), Color::rgba(c.r, c.g, c.b, 40));
        }

        std::vector<Point> pts;
        pts.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
            pts.push_back(map.to_pixel(s.x(i), s.y(i)));
        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
}
```

- [ ] **Step 5: Run the plot tests to verify they pass**

Run: `meson test -C builddir plot -v`
Expected: all cases PASS, including the new fill test. The existing `"PlotModel canvas produces draw commands"` and `"draw_series emits polylines for each series"` cases must still pass unchanged (default `fill = false` means no behavior change for existing series).

- [ ] **Step 6: Commit**

```bash
git add include/prism/widgets/plot.hpp include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add opt-in fill-under-curve series style"
```

---

### Task 3: Extract `render_plot_panel`/`route_plot_input`, add `draw_x_axis` suppression

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Modify: `include/prism/widgets/plot_render.hpp`
- Test: `tests/test_plot.cpp`

**Interfaces:**
- Consumes: `CursorState` (existing, `plot_render.hpp`).
- Produces (used by Task 4 and Task 5):
  - `template <typename C> concept PlotCursor` (`plot_render.hpp`) — `requires(C c) { c.data_x; c.visible; }`.
  - `draw_tick_labels(DrawList&, const PlotMapping&, const TickArrays&, const Theme&, bool draw_x_axis = true)` and `draw_axes_labels(DrawList&, const PlotMapping&, const std::string&, const std::string&, const Theme&, bool draw_x_axis = true)` (`plot_render.hpp`) — x-specific output suppressed when `false`; y-axis output always drawn.
  - `template <PlotCursor C> void render_plot_panel(DrawList&, Rect, const WidgetNode&, const Field<AxisRange>& x_range, const Field<AxisRange>& y_range, const Field<ViewTransform>& view, const Field<C>& cursor, std::span<const Series> series, const std::string& x_label, const std::string& y_label, bool draw_x_axis)` (`plot.hpp`).
  - `template <PlotCursor C> void route_plot_input(const InputEvent&, WidgetNode&, Rect, Field<AxisRange>& x_range, Field<AxisRange>& y_range, Field<ViewTransform>& view, Field<C>& cursor, DragMode& drag_mode, Point& drag_start_pixel, ViewTransform& drag_start_view, std::span<const Series> series)` (`plot.hpp`).

- [ ] **Step 1: Write the failing tests for `draw_x_axis` suppression in `tests/test_plot.cpp`**

Add after `TEST_CASE("draw_grid emits grid lines and tick labels")` (ends around line 206):

```cpp
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
```

- [ ] **Step 2: Run the tests to verify they fail to compile**

Run: `meson test -C builddir plot -v`
Expected: build error — `draw_tick_labels`/`draw_axes_labels` don't accept a 5th argument yet.

- [ ] **Step 3: Add `draw_x_axis` parameter to `draw_tick_labels` (`plot_render.hpp`)**

Replace (currently lines 172-198):

```cpp
inline void draw_tick_labels(DrawList& dl, const PlotMapping& map,
                             const TickArrays& ticks, const Theme& t)
{
    Width cw = char_width(tick_font_size);

    for (double tx : ticks.x) {
        X x = map.to_pixel(tx, 0.0).x;
        if (x < map.left() || x > map.right()) continue;
        dl.line(Point{x, map.bottom()},
                Point{x, map.bottom() + DY{tick_len}}, t.border, 1.f);
        dl.text(fmt::format("{:.6g}", tx),
                Point{x - DX{15.f}, map.bottom() + DY{tick_len + 2.f}},
                tick_font_size, t.text_muted);
    }

    for (double ty : ticks.y) {
        Y y = map.to_pixel(0.0, ty).y;
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{map.left() - DX{tick_len}, y},
                Point{map.left(), y}, t.border, 1.f);
        auto label = fmt::format("{:.6g}", ty);
        Width label_w = cw * static_cast<float>(label.size());
        dl.text(std::move(label),
                Point{map.left() - DX{tick_len + 2.f + label_w.raw()}, y - DY{6.f}},
                tick_font_size, t.text_muted);
    }
}
```

with:

```cpp
inline void draw_tick_labels(DrawList& dl, const PlotMapping& map,
                             const TickArrays& ticks, const Theme& t,
                             bool draw_x_axis = true)
{
    Width cw = char_width(tick_font_size);

    if (draw_x_axis) {
        for (double tx : ticks.x) {
            X x = map.to_pixel(tx, 0.0).x;
            if (x < map.left() || x > map.right()) continue;
            dl.line(Point{x, map.bottom()},
                    Point{x, map.bottom() + DY{tick_len}}, t.border, 1.f);
            dl.text(fmt::format("{:.6g}", tx),
                    Point{x - DX{15.f}, map.bottom() + DY{tick_len + 2.f}},
                    tick_font_size, t.text_muted);
        }
    }

    for (double ty : ticks.y) {
        Y y = map.to_pixel(0.0, ty).y;
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{map.left() - DX{tick_len}, y},
                Point{map.left(), y}, t.border, 1.f);
        auto label = fmt::format("{:.6g}", ty);
        Width label_w = cw * static_cast<float>(label.size());
        dl.text(std::move(label),
                Point{map.left() - DX{tick_len + 2.f + label_w.raw()}, y - DY{6.f}},
                tick_font_size, t.text_muted);
    }
}
```

- [ ] **Step 4: Add `draw_x_axis` parameter to `draw_axes_labels` (`plot_render.hpp`)**

Replace (currently lines 236-252):

```cpp
inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t)
{
    // X+X has no defined result (adding two absolute positions is meaningless),
    // so the midpoint is computed via the algebraically valid a + (b-a)/2 form.
    X cx = map.left() + (map.right() - map.left()) / 2.f;

    if (!x_label.empty())
        dl.text(x_label, Point{cx - DX{30.f}, map.bottom() + DY{18.f}}, label_font_size, t.text);

    if (!y_label.empty()) {
        X lx = map.left() - DX{margin_left.raw() - 10.f};
        Y cy = map.top() + (map.bottom() - map.top()) / 2.f;
        dl.text(y_label, Point{lx, cy}, label_font_size, t.text, 90.f, TextAnchor::Center);
    }
}
```

with:

```cpp
inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t, bool draw_x_axis = true)
{
    // X+X has no defined result (adding two absolute positions is meaningless),
    // so the midpoint is computed via the algebraically valid a + (b-a)/2 form.
    X cx = map.left() + (map.right() - map.left()) / 2.f;

    if (draw_x_axis && !x_label.empty())
        dl.text(x_label, Point{cx - DX{30.f}, map.bottom() + DY{18.f}}, label_font_size, t.text);

    if (!y_label.empty()) {
        X lx = map.left() - DX{margin_left.raw() - 10.f};
        Y cy = map.top() + (map.bottom() - map.top()) / 2.f;
        dl.text(y_label, Point{lx, cy}, label_font_size, t.text, 90.f, TextAnchor::Center);
    }
}
```

- [ ] **Step 5: Add the `PlotCursor` concept to `plot_render.hpp`**

Add right after `struct CursorState { ... };` (currently lines 129-134):

```cpp
template <typename C>
concept PlotCursor = requires(C c) {
    { c.data_x } -> std::convertible_to<double>;
    { c.visible } -> std::convertible_to<bool>;
};
```

- [ ] **Step 6: Run the tests to verify the new draw_x_axis cases pass**

Run: `meson test -C builddir plot -v`
Expected: all cases PASS (existing calls to `draw_tick_labels`/`draw_axes_labels` with 4 args still compile via the new default parameter).

- [ ] **Step 7: Extract `render_plot_panel`/`route_plot_input` in `plot.hpp`**

Insert these two templates right after `compute_mapping` (currently ends at line 101) and before `struct PlotModel {` (currently line 103):

```cpp
template <PlotCursor C>
void render_plot_panel(DrawList& dl, Rect bounds, const WidgetNode& node,
                       const Field<AxisRange>& x_range, const Field<AxisRange>& y_range,
                       const Field<ViewTransform>& view, const Field<C>& cursor,
                       std::span<const Series> series,
                       const std::string& x_label, const std::string& y_label,
                       bool draw_x_axis)
{
    auto& t = *node.theme;
    auto map = compute_mapping(bounds, x_range, y_range, view, series);

    draw_background(dl, map.plot_area, t);
    auto ticks = compute_ticks(map);

    // Inside clip_push, coordinates are local (origin = {0,0})
    PlotMapping local_map = map;
    local_map.plot_area.origin = Point{X{0}, Y{0}};

    dl.clip_push(map.plot_area.origin, map.plot_area.extent);
    draw_grid_lines(dl, local_map, ticks, t);
    draw_series(dl, local_map, series);
    if constexpr (requires (C c) { c.data_y; })
        draw_cursor(dl, local_map, cursor.get(), t);
    else
        draw_vertical_cursor(dl, local_map, cursor.get().data_x, cursor.get().visible, t);
    dl.clip_pop();
    draw_tick_labels(dl, map, ticks, t, draw_x_axis);
    draw_axes_labels(dl, map, x_label, y_label, t, draw_x_axis);
}

template <PlotCursor C>
void route_plot_input(const InputEvent& ev, WidgetNode& /*nd*/, Rect bounds,
                      Field<AxisRange>& x_range, Field<AxisRange>& y_range,
                      Field<ViewTransform>& view, Field<C>& cursor,
                      DragMode& drag_mode, Point& drag_start_pixel,
                      ViewTransform& drag_start_view,
                      std::span<const Series> series)
{
    auto map = compute_mapping(bounds, x_range, y_range, view, series);

    auto freeze_auto_fit = [&] {
        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) {
            xr = auto_fit_range(series, Axis::X);
            xr.auto_fit = false;
            x_range.set(xr);
        }
        if (yr.auto_fit) {
            yr = auto_fit_range(series, Axis::Y);
            yr.auto_fit = false;
            y_range.set(yr);
        }
    };

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
            if constexpr (requires (C c) { c.data_y; })
                cursor.set(C{dx, dy, true});
            else
                cursor.set(C{dx, true});
        } else {
            auto c = cursor.get();
            if (c.visible) {
                if constexpr (requires (C cc) { cc.data_y; })
                    cursor.set(C{c.data_x, c.data_y, false});
                else
                    cursor.set(C{c.data_x, false});
            }
        }

    } else if (auto* mb = std::get_if<MouseButton>(&ev)) {
        if (mb->button == buttons::left) {
            if (mb->pressed) {
                drag_mode = DragMode::Pan;
                drag_start_pixel = mb->position;
                drag_start_view = view.get();

                freeze_auto_fit();
            } else {
                drag_mode = DragMode::None;
            }
        } else if (mb->button == buttons::right && mb->pressed) {
            if (map.plot_area.contains(mb->position)) {
                x_range.set(AxisRange{});
                y_range.set(AxisRange{});
                view.set(ViewTransform{});
            }
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        X px = ms->position.x;
        Y py = ms->position.y;

        bool in_plot = map.plot_area.contains(ms->position);
        bool in_x_axis = (px >= map.left() && px <= map.right()
                          && py > map.bottom() && py <= map.bottom() + DY{margin_bottom.raw()});
        bool in_y_axis = (py >= map.top() && py <= map.bottom()
                          && px >= map.left() - DX{margin_left.raw()} && px < map.left());

        if (!in_plot && !in_x_axis && !in_y_axis) return;

        freeze_auto_fit();

        double factor = std::pow(zoom_base, ms->dy.raw());
        Point clamp_pt{std::clamp(px, map.left(), map.right()),
                       std::clamp(py, map.top(), map.bottom())};
        auto [data_x, data_y] = map.to_data(clamp_pt);

        auto zoom_axis = [factor](double& scale, double& offset,
                                  double data_anchor, AxisRange base) {
            double old = scale;
            scale *= factor;
            double center = (base.min + base.max) / 2.0;
            offset = data_anchor - center
                     + (center + offset - data_anchor) * old / scale;
        };

        auto v = view.get();
        if (in_plot || in_x_axis)
            zoom_axis(v.scale_x, v.offset_x, data_x, x_range.get());
        if (in_plot || in_y_axis)
            zoom_axis(v.scale_y, v.offset_y, data_y, y_range.get());

        view.set(v);
    }
}
```

`render_plot_panel` above calls `draw_vertical_cursor`, which doesn't exist yet — the next step adds it so the file compiles by the end of this task. Task 4 later adds the `PlotGroupCursor` type and the tests that actually exercise this function's `if constexpr` branch; this step only makes the extraction compile.

- [ ] **Step 8: Add `draw_vertical_cursor` to `plot_render.hpp`**

Add right after `draw_cursor` (currently ends at line 234):

```cpp
inline void draw_vertical_cursor(DrawList& dl, const PlotMapping& map,
                                 double data_x, bool visible, const Theme& t)
{
    if (!visible) return;
    auto px = map.to_pixel(data_x, 0.0);
    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);
    dl.line(Point{px.x, map.top()}, Point{px.x, map.bottom()}, crosshair_color, 1.f);
}
```

- [ ] **Step 9: Rewrite `PlotModel::canvas()` to delegate**

Replace the `canvas()` method body inside `struct PlotModel` (currently lines 152-172):

```cpp
    // Canvas interface
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
    {
        auto& t = *node.theme;
        auto map = compute_mapping(bounds, x_range, y_range, view,
                                   std::span<const Series>(series_));

        draw_background(dl, map.plot_area, t);
        auto ticks = compute_ticks(map);

        // Inside clip_push, coordinates are local (origin = {0,0})
        PlotMapping local_map = map;
        local_map.plot_area.origin = Point{X{0}, Y{0}};

        dl.clip_push(map.plot_area.origin, map.plot_area.extent);
        draw_grid_lines(dl, local_map, ticks, t);
        draw_series(dl, local_map, std::span<const Series>(series_));
        draw_cursor(dl, local_map, cursor.get(), t);
        dl.clip_pop();
        draw_tick_labels(dl, map, ticks, t);
        draw_axes_labels(dl, map, x_label.get(), y_label.get(), t);
    }
```

with:

```cpp
    // Canvas interface
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
    {
        render_plot_panel(dl, bounds, node, x_range, y_range, view, cursor,
                          std::span<const Series>(series_), x_label.get(), y_label.get(), true);
    }
```

- [ ] **Step 10: Rewrite `PlotModel::handle_canvas_input()` to delegate**

Replace the entire out-of-class definition (currently lines 180-276):

```cpp
inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& /*nd*/, Rect bounds)
{
    auto series_span = std::span<const Series>(series_);
    auto map = compute_mapping(bounds, x_range, y_range, view, series_span);

    auto freeze_auto_fit = [&] {
        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) {
            xr = auto_fit_range(series_span, Axis::X);
            xr.auto_fit = false;
            x_range.set(xr);
        }
        if (yr.auto_fit) {
            yr = auto_fit_range(series_span, Axis::Y);
            yr.auto_fit = false;
            y_range.set(yr);
        }
    };

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
        if (mb->button == buttons::left) {
            if (mb->pressed) {
                drag_mode = DragMode::Pan;
                drag_start_pixel = mb->position;
                drag_start_view = view.get();

                freeze_auto_fit();
            } else {
                drag_mode = DragMode::None;
            }
        } else if (mb->button == buttons::right && mb->pressed) {
            if (map.plot_area.contains(mb->position))
                reset_view();
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        X px = ms->position.x;
        Y py = ms->position.y;

        bool in_plot = map.plot_area.contains(ms->position);
        bool in_x_axis = (px >= map.left() && px <= map.right()
                          && py > map.bottom() && py <= map.bottom() + DY{margin_bottom.raw()});
        bool in_y_axis = (py >= map.top() && py <= map.bottom()
                          && px >= map.left() - DX{margin_left.raw()} && px < map.left());

        if (!in_plot && !in_x_axis && !in_y_axis) return;

        freeze_auto_fit();

        double factor = std::pow(zoom_base, ms->dy.raw());
        Point clamp_pt{std::clamp(px, map.left(), map.right()),
                       std::clamp(py, map.top(), map.bottom())};
        auto [data_x, data_y] = map.to_data(clamp_pt);

        auto zoom_axis = [factor](double& scale, double& offset,
                                  double data_anchor, AxisRange base) {
            double old = scale;
            scale *= factor;
            double center = (base.min + base.max) / 2.0;
            offset = data_anchor - center
                     + (center + offset - data_anchor) * old / scale;
        };

        auto v = view.get();
        if (in_plot || in_x_axis)
            zoom_axis(v.scale_x, v.offset_x, data_x, x_range.get());
        if (in_plot || in_y_axis)
            zoom_axis(v.scale_y, v.offset_y, data_y, y_range.get());

        view.set(v);
    }
}
```

with:

```cpp
inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds)
{
    route_plot_input(ev, nd, bounds, x_range, y_range, view, cursor,
                     drag_mode, drag_start_pixel, drag_start_view,
                     std::span<const Series>(series_));
}
```

Note this also removes the call to the `reset_view()` member on right-click, inlining the equivalent three `.set()` calls directly (`route_plot_input` is a free function, not a `PlotModel` member, so it can't call `this->reset_view()`) — `PlotModel::reset_view()` itself is untouched and still exists as public API for direct callers (the `"PlotModel reset_view restores auto_fit"` test calls it directly).

- [ ] **Step 11: Run the full existing plot suite to verify the refactor is behavior-preserving**

Run: `meson test -C builddir plot -v`
Expected: **all** cases pass, including every pre-existing case (`PlotModel canvas produces draw commands`, `PlotModel cursor updates on mouse move`, `PlotModel scroll zooms view`, `PlotModel drag pans view`, `PlotModel reset_view restores auto_fit`, `PlotModel right-click resets view`, etc.) — with zero changes to their assertions. This is the proof that the extraction preserved behavior exactly.

- [ ] **Step 12: Commit**

```bash
git add include/prism/widgets/plot.hpp include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "refactor(plot): extract render_plot_panel/route_plot_input, add x-axis suppression"
```

---

### Task 4: `PlotGroupCursor` — exercise the generic cursor path directly

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp`
- Test: `tests/test_plot.cpp`

**Interfaces:**
- Consumes: `PlotCursor` concept, `render_plot_panel`, `route_plot_input`, `draw_vertical_cursor` (Task 3).
- Produces: `struct PlotGroupCursor { double data_x = 0.0; bool visible = false; bool operator==(const PlotGroupCursor&) const = default; };` (`plot_render.hpp`, next to `CursorState`). Consumed by Task 5's `PlotGroup`/`PlotPanel`.

This task proves the `if constexpr` branches added in Task 3 actually work for a cursor type without `data_y`, in isolation from the `PlotGroup`/`PlotPanel` wiring that Task 5 adds — so if something's wrong with the generic templates, it surfaces here with a small, direct reproduction instead of tangled up with the group/panel plumbing.

- [ ] **Step 1: Write the failing tests in `tests/test_plot.cpp`**

Add after the `draw_cursor` test cases (after `TEST_CASE("draw_cursor emits nothing when not visible")`, ends around line 283):

```cpp
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

TEST_CASE("render_plot_panel with PlotGroupCursor draws no coordinate readout")
{
    using namespace prism;
    using namespace prism::plot;

    Field<AxisRange> xr{{0.0, 10.0, false}};
    Field<AxisRange> yr{{0.0, 10.0, false}};
    Field<ViewTransform> vt{{}};
    Field<PlotGroupCursor> cursor{{5.0, true}};

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
```

- [ ] **Step 2: Run the tests to verify they fail to compile**

Run: `meson test -C builddir plot -v`
Expected: build error — `PlotGroupCursor` not declared (`draw_vertical_cursor` already exists from Task 3, so only the `PlotGroupCursor`-dependent cases fail).

- [ ] **Step 3: Add `PlotGroupCursor` to `plot_render.hpp`**

Add right after the `PlotCursor` concept added in Task 3:

```cpp
struct PlotGroupCursor {
    double data_x = 0.0;
    bool visible = false;
    bool operator==(const PlotGroupCursor&) const = default;
};
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `meson test -C builddir plot -v`
Expected: all cases PASS, including the 4 new ones. This confirms `render_plot_panel<PlotGroupCursor>` and `route_plot_input<PlotGroupCursor>` compile and behave correctly before Task 5 builds `PlotGroup`/`PlotPanel` on top of them.

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/plot_render.hpp tests/test_plot.cpp
git commit -m "feat(plot): add PlotGroupCursor, prove generic cursor templates standalone"
```

---

### Task 5: `PlotGroup` + `PlotPanel`

**Files:**
- Modify: `include/prism/widgets/plot.hpp`
- Test: `tests/test_plot.cpp`

**Interfaces:**
- Consumes: `render_plot_panel`, `route_plot_input` (Task 3), `PlotGroupCursor` (Task 4), `WidgetTree::ViewBuilder` (`prism/app/widget_tree.hpp`).
- Produces: `PlotGroup` (`x_range`, `x_view`, `cursor`, `x_label` Fields; `add_plot(std::string y_label = "") -> PlotPanel&`; `reset_view()`; `view(WidgetTree::ViewBuilder&)`) and `PlotPanel` (`y_range`, `y_label`, `revision` Fields; `add_series`, `clear_series`, `notify`, `canvas`, `handle_canvas_input`). Consumed by Task 6's system-monitor migration.

- [ ] **Step 1: Write the failing tests in `tests/test_plot.cpp`**

Add at the end of the file (after `TEST_CASE("default_series_colors returns 8 distinct colors")`):

```cpp
TEST_CASE("PlotGroup shares x pan across panels, keeps y independent")
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

    InputEvent down = MouseButton{center, 1, true};
    p1.handle_canvas_input(down, node, bounds);
    Point moved{X{center.x.raw() + 50.f}, center.y};
    InputEvent drag = MouseMove{moved};
    p1.handle_canvas_input(drag, node, bounds);

    CHECK(group.x_view.get().offset_x != 0.0);
    CHECK(p2.y_range.get().min == 0.0);
    CHECK(p2.y_range.get().max == 20.0);
}

TEST_CASE("PlotGroup cursor syncs data_x across panels, no data_y")
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
```

- [ ] **Step 2: Run the tests to verify they fail to compile**

Run: `meson test -C builddir plot -v`
Expected: build error — `PlotGroup`/`PlotPanel` not declared.

- [ ] **Step 3: Add the `prism/app/widget_tree.hpp` include to `plot.hpp`**

Add to the include block at the top of `include/prism/widgets/plot.hpp` (after `#include <prism/widgets/plot_render.hpp>`):

```cpp
#include <prism/app/widget_tree.hpp>
```

This is not a layering violation: `widgets/inspector.hpp` and `widgets/field_mirror.hpp` already include `prism/app/widget_tree.hpp` directly, and `widget_tree.hpp` itself includes nothing under `prism/widgets/`.

Also add `using namespace prism::app;` to the `using namespace` block near the top of the `prism::plot` namespace (alongside the existing `using namespace prism::ui;`), so `WidgetTree::ViewBuilder` can be referenced unqualified like the rest of the file's types.

- [ ] **Step 4: Add `PlotPanel` and `PlotGroup` to `plot.hpp`**

Add at the end of the file, after `PlotModel::handle_canvas_input`'s definition and before the closing `} // namespace prism::plot`:

```cpp
class PlotGroup;

class PlotPanel {
  public:
    Field<AxisRange> y_range{};
    Field<std::string> y_label{""};
    Field<uint32_t> revision{0};

    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    explicit PlotPanel(std::string label) : y_label(std::move(label)) {}

    template <PlotSource S>
    void add_series(S source, SeriesStyle style)
    {
        series_.emplace_back(std::move(source), style);
    }

    void add_series(std::vector<double> xs, std::vector<double> ys, SeriesStyle style)
    {
        add_series(XYData{std::move(xs), std::move(ys)}, style);
    }

    void clear_series() { series_.clear(); }
    void notify() { revision.set(revision.get() + 1); }

    // Fixed 3-arg signatures -- vb.canvas(panel) requires exactly `canvas(dl, r, n)` /
    // `handle_canvas_input(ev, nd, r)` (see widget_node.hpp's node_canvas()), so the group's
    // shared x-state can't be passed as a call parameter. PlotPanel reaches it through the
    // `group_` back-pointer set by PlotGroup::add_plot() instead.
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node);
    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    friend class PlotGroup;
    PlotGroup* group_ = nullptr;   // set by PlotGroup::add_plot(); never null once added
    bool draw_x_axis_ = false;     // true only for the group's current last panel
    std::vector<Series> series_;
};

class PlotGroup {
  public:
    Field<AxisRange> x_range{};
    Field<ViewTransform> x_view{};
    Field<PlotGroupCursor> cursor{};
    Field<std::string> x_label{""};

    // node_canvas() captures &panel by reference into the widget tree for the panel's
    // lifetime, so panels must never move once added -- panels_ stores unique_ptr so the
    // owning vector can grow (on later add_plot() calls) without invalidating references
    // returned by earlier add_plot() calls or the &panel captures inside the widget tree.
    PlotPanel& add_plot(std::string y_label = "")
    {
        if (!panels_.empty())
            panels_.back()->draw_x_axis_ = false;
        panels_.push_back(std::make_unique<PlotPanel>(std::move(y_label)));
        panels_.back()->group_ = this;
        panels_.back()->draw_x_axis_ = true;
        return *panels_.back();
    }

    void reset_view()
    {
        x_view.set(ViewTransform{});
        x_range.set(AxisRange{});
        for (auto& p : panels_)
            p->y_range.set(AxisRange{});
    }

    void view(WidgetTree::ViewBuilder& vb)
    {
        for (auto& p : panels_) {
            vb.canvas(*p).depends_on(x_range, x_view, cursor, p->y_range, p->revision)
              .min_size(Height{120});
        }
    }

  private:
    std::vector<std::unique_ptr<PlotPanel>> panels_;
};

inline void PlotPanel::canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
{
    render_plot_panel(dl, bounds, node, group_->x_range, y_range, group_->x_view,
                      group_->cursor, std::span<const Series>(series_),
                      group_->x_label.get(), y_label.get(), draw_x_axis_);
}

inline void PlotPanel::handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds)
{
    route_plot_input(ev, nd, bounds, group_->x_range, y_range, group_->x_view,
                     group_->cursor, drag_mode, drag_start_pixel, drag_start_view,
                     std::span<const Series>(series_));
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `meson test -C builddir plot -v`
Expected: all cases PASS, including the 4 new `PlotGroup`/`PlotPanel` cases, and every pre-existing case in the file still passes.

- [ ] **Step 6: Build the whole project**

Run: `meson compile -C builddir`
Expected: clean build (the new `#include <prism/app/widget_tree.hpp>` in a widgets/ header must not break any other translation unit).

- [ ] **Step 7: Commit**

```bash
git add include/prism/widgets/plot.hpp tests/test_plot.cpp
git commit -m "feat(plot): add PlotGroup/PlotPanel for stacked plots with a shared x-axis"
```

---

### Task 6: Migrate the system-monitor example, regenerate its screenshot

**Files:**
- Modify: `examples/model_system_monitor/model_system_monitor.cpp`
- Modify: `doc/screenshots/system_monitor.svg` (regenerated, not hand-edited)

**Interfaces:**
- Consumes: `PlotGroup`, `PlotPanel` (Task 5).

- [ ] **Step 1: Replace the three `PlotModel` members with a `PlotGroup`**

In `examples/model_system_monitor/model_system_monitor.cpp`, replace:

```cpp
    prism::plot::PlotModel cpu_plot;
    prism::plot::PlotModel mem_plot;
    prism::plot::PlotModel net_plot;
```

with:

```cpp
    prism::plot::PlotGroup plot_group;
    prism::plot::PlotPanel* cpu_plot = &plot_group.add_plot("CPU %");
    prism::plot::PlotPanel* mem_plot = &plot_group.add_plot("Mem (MB)");
    prism::plot::PlotPanel* net_plot = &plot_group.add_plot("Net (KB/s)");
```

(Raw pointers, not references — `PlotPanel&` reference members would make `SystemMonitor` non-trivially-reflectable; a pointer with a default member initializer referencing the already-declared `plot_group` above it is exactly the same pattern this file already uses for `tree_ctrl`'s in-class initializer referencing `tree_source`.)

- [ ] **Step 2: Update `rebuild_plot` to take a `PlotPanel&` and an optional fill flag**

Replace:

```cpp
    static void rebuild_plot(prism::plot::PlotModel& plot, const History& h) {
        std::vector<double> xs(h.values.size());
        std::vector<double> ys(h.values.begin(), h.values.end());
        for (size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        auto colors = prism::plot::default_series_colors(prism::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys)},
                        prism::plot::SeriesStyle{colors[0], 2.f});
        plot.notify();
    }
```

with:

```cpp
    static void rebuild_plot(prism::plot::PlotPanel& plot, const History& h, bool fill = false) {
        std::vector<double> xs(h.values.size());
        std::vector<double> ys(h.values.begin(), h.values.end());
        for (size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        auto colors = prism::plot::default_series_colors(prism::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys)},
                        prism::plot::SeriesStyle{colors[0], 2.f, fill, 0.0});
        plot.notify();
    }
```

- [ ] **Step 3: Update `ingest_system` to use the pointer members and dogfood fill on CPU**

Replace:

```cpp
    void ingest_system(const SystemSample& s) {
        cpu_history.push(s.cpu_percent);
        mem_history.push(static_cast<float>(s.mem_used_mb));
        net_history.push(static_cast<float>(s.net_rx_kbps));
        rebuild_plot(cpu_plot, cpu_history);
        rebuild_plot(mem_plot, mem_history);
        rebuild_plot(net_plot, net_history);
    }
```

with:

```cpp
    void ingest_system(const SystemSample& s) {
        cpu_history.push(s.cpu_percent);
        mem_history.push(static_cast<float>(s.mem_used_mb));
        net_history.push(static_cast<float>(s.net_rx_kbps));
        rebuild_plot(*cpu_plot, cpu_history, /*fill=*/true);
        rebuild_plot(*mem_plot, mem_history);
        rebuild_plot(*net_plot, net_history);
    }
```

- [ ] **Step 4: Collapse the three plot canvases in `view()` into `plot_group.view(vb)`**

Replace:

```cpp
        vb.vstack([&] {
            vb.canvas(cpu_plot)
                .depends_on(cpu_plot.x_range, cpu_plot.y_range, cpu_plot.view,
                            cpu_plot.cursor, cpu_plot.revision)
                .min_size(prism::Height{120});
            vb.canvas(mem_plot)
                .depends_on(mem_plot.x_range, mem_plot.y_range, mem_plot.view,
                            mem_plot.cursor, mem_plot.revision)
                .min_size(prism::Height{120});
            vb.canvas(net_plot)
                .depends_on(net_plot.x_range, net_plot.y_range, net_plot.view,
                            net_plot.cursor, net_plot.revision)
                .min_size(prism::Height{120});
            vb.handle();
```

with:

```cpp
        vb.vstack([&] {
            plot_group.view(vb);
            vb.handle();
```

- [ ] **Step 5: Build the example and run the full test suite**

Run: `meson compile -C builddir`
Expected: clean build.

Run: `meson test -C builddir`
Expected: read the literal summary line (e.g. `Ok: N, Fail: 0`) — every test must pass, not just `plot`.

- [ ] **Step 6: Regenerate the system-monitor screenshot**

The custom_target doesn't track header dependencies, so force a rebuild of the specific output first:

```bash
rm -f builddir/examples/model_system_monitor/system_monitor.svg
meson compile -C builddir
cp builddir/examples/model_system_monitor/system_monitor.svg doc/screenshots/system_monitor.svg
```

Read `doc/screenshots/system_monitor.svg` (it's a text/XML file — open it, or view it as an image) and visually confirm: three stacked plot panels, a shaded area under the CPU% curve, and only the bottom (net) panel showing x-axis tick numbers.

- [ ] **Step 7: Commit**

```bash
git add examples/model_system_monitor/model_system_monitor.cpp doc/screenshots/system_monitor.svg
git commit -m "refactor(system-monitor): migrate cpu/mem/net plots to a shared-x PlotGroup"
```

---

## Final Verification

- [ ] Run `meson test -C builddir` one more time from a clean perspective and read the literal pass/fail count.
- [ ] Confirm `git log --oneline -7` shows the 6 task commits plus the earlier spec commit.
- [ ] Update the PRISM project memory (`plot widget` entry) noting: stacked/linked plots (`PlotGroup`/`PlotPanel`) and fill-under-curve shipped, system-monitor migrated as the dogfood case.
