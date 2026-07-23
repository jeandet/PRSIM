# Plot Widget: Stacked/Linked Plots + Fill-Under-Curve — Design Spec

## Overview

Follow-up to `2026-04-01-plot-widget-design.md`, which explicitly deferred "multi-panel / synchronized axes" and treated the plot widget as a single-panel primitive. This spec adds two capabilities, motivated directly by the system-monitor example's cpu/mem/net plots (currently three fully independent `PlotModel`s manually stacked in a `vstack`, each with its own pan/zoom/cursor state and ~4 lines of repeated `.depends_on(...)` boilerplate):

1. **Stacked plots sharing an x-axis** — pan/zoom and crosshair position linked across a vertical stack of plots, each keeping its own y-scale.
2. **Fill-under-curve** — an opt-in per-series style that shades the area between the curve and a baseline value.

Both are additive. No existing `PlotModel` behavior changes for standalone (non-grouped) usage.

## Goals

- Eliminate the boilerplate of manually wiring N independent plots with linked x-behavior.
- Match common monitoring-dashboard conventions: hovering any panel in a stack shows a vertical cursor line at the same x in every panel; only the bottom panel renders x-axis ticks/label.
- Support area-fill styling for line series (common in CPU/mem/network monitors).
- Zero risk to existing single-plot call sites (`model_plot.cpp`, `showcase_plot.cpp`) — `PlotModel` is untouched at the API level; internals are refactored but externally observable behavior is identical.

## Out of Scope (explicitly deferred, per prior spec + user confirmation)

- Legend box, log-scale axes, custom tick formatters, markers/scatter mode.
- Per-panel y-readout / tooltip synced to the shared cursor (v1 shows only a synced vertical line; each panel's own crosshair-driven y-value readout is not part of this iteration).
- General (non-x-monotonic) polygon fill — the fill primitive is built for line-chart-shaped data (x increasing along the series).

## Architecture: Shared Core, Two Wrapper Types

### Why a split instead of one type

`compute_mapping`, the `draw_*` layer functions, and the pan/zoom/cursor math in `plot_render.hpp` are already written against `Field<AxisRange>&` / `Field<ViewTransform>&` / `Field<CursorState>&` parameters, not hardcoded members. Only `PlotModel`'s two public entry points — `canvas()` and `handle_canvas_input()` — currently always pass `this->x_range`, `this->view`, etc. That's the seam: extract those method bodies into free functions parameterized the same way `compute_mapping` already is, then give grouped plots a type that passes in *borrowed* fields instead of its own.

Rejected alternatives:
- **Inject optional external field pointers directly into `PlotModel`** (single type, dual mode: sometimes owns its fields, sometimes borrows them). Rejected — a type whose ownership semantics differ at runtime based on how it was constructed is a hidden-mode trap; call sites can't tell which mode they're in by reading the type.
- **Merge everything into one fat `PlotGroup`, retire standalone `PlotModel`.** Rejected — unnecessary churn to existing single-plot usage for no behavioral benefit; a plot with one panel doesn't need group machinery.

### Core extraction

`PlotModel`'s standalone cursor (`CursorState`: `data_x`, `data_y`, `visible`) and the group's shared cursor (`PlotGroupCursor`: `data_x`, `visible` — no `data_y`, since panels have independent y-scales) are different types. To let both share one implementation, the cursor parameter is a template constrained by a small concept rather than fixed to `CursorState`:

```cpp
template <typename C>
concept PlotCursor = requires(C c, double d, bool b) {
    { c.data_x } -> std::convertible_to<double>;
    { c.visible } -> std::convertible_to<bool>;
};

// plot_render.hpp — new free functions, called by both PlotModel and PlotPanel
template <PlotCursor C>
void render_plot_panel(DrawList& dl, Rect bounds, const WidgetNode& node,
                       const Field<AxisRange>& x_range, const Field<AxisRange>& y_range,
                       const Field<ViewTransform>& view, const Field<C>& cursor,
                       std::span<const Series> series,
                       const std::string& x_label, const std::string& y_label,
                       bool draw_x_axis);

template <PlotCursor C>
void route_plot_input(const InputEvent& ev, WidgetNode& nd, Rect bounds,
                      Field<AxisRange>& x_range, Field<AxisRange>& y_range,
                      Field<ViewTransform>& view, Field<C>& cursor,
                      DragMode& drag_mode, Point& drag_start_pixel,
                      ViewTransform& drag_start_view,
                      std::span<const Series> series);
```

`render_plot_panel` is `PlotModel::canvas()`'s current body, generalized with a `draw_x_axis` flag (ticks/label suppressed when `false`). Where it currently builds `CursorState{dx, dy, true}` for `draw_cursor`, it now does so `if constexpr` the concrete `C` has a `data_y` member (`requires (C c) { c.data_y; }`), otherwise constructs a vertical-line-only cursor draw (see Cursor Sync below). `route_plot_input` is `PlotModel::handle_canvas_input()`'s current body, with drag state passed by reference instead of accessed via `this->`, and the same `if constexpr` split for setting `data_y` on `MouseMove`.

`draw_cursor` (`plot_render.hpp`) gains an overload/flag for the vertical-only case: draws the vertical crosshair line but skips the horizontal line and the coordinate-readout box entirely when no `data_y` is available.

`PlotModel` becomes a thin wrapper that owns `x_range`, `y_range`, `view`, `cursor`, `drag_mode`, `drag_start_pixel`, `drag_start_view` exactly as today, and its `canvas()`/`handle_canvas_input()` delegate to the free functions passing its own members. **No change in observable behavior.**

### PlotGroup + PlotPanel

```cpp
struct PlotGroupCursor {
    double data_x = 0.0;
    bool visible = false;
    bool operator==(const PlotGroupCursor&) const = default;
};

struct PlotPanel {
    Field<AxisRange> y_range{};
    Field<std::string> y_label{""};
    Field<uint32_t> revision{0};

    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    template <PlotSource S> void add_series(S source, SeriesStyle style);
    void add_series(std::vector<double> xs, std::vector<double> ys, SeriesStyle style);
    void notify() { revision.set(revision.get() + 1); }

    // Fixed 3-arg signatures -- vb.canvas(panel) requires exactly `canvas(dl, r, n)` /
    // `handle_canvas_input(ev, nd, r)` (see widget_tree.hpp's `requires` clause on canvas()
    // and node_canvas()'s dispatch), so the group's shared x-state can't be passed as a call
    // parameter. PlotPanel instead reaches it through the `group_` back-pointer set by
    // PlotGroup::add_plot(), and both methods forward into the templated free functions above
    // using group_->x_range/x_view/cursor/x_label alongside its own y_range/series_.
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node);
    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    friend struct PlotGroup;
    PlotGroup* group_ = nullptr;   // set by PlotGroup::add_plot(); never null once added to a group
    bool draw_x_axis_ = false;     // true only for the group's current last panel
    std::vector<Series> series_;
};

struct PlotGroup {
    Field<AxisRange> x_range{};
    Field<ViewTransform> x_view{};
    Field<PlotGroupCursor> cursor{};
    Field<std::string> x_label{""};

    PlotPanel& add_plot(std::string y_label = "");
    void reset_view();  // resets x_range/x_view and every panel's y_range

    void view(WidgetTree::ViewBuilder& vb);  // stacks vb.canvas(panel) per panel, wires depends_on, min_size

  private:
    std::vector<std::unique_ptr<PlotPanel>> panels_;  // unique_ptr: stable addresses for add_plot() refs held by caller
};
```

`PlotGroup::add_plot()` appends a new `PlotPanel`, sets its `group_` back-pointer, flips the previous last panel's `draw_x_axis_` to `false`, and sets the new one's to `true`.

`PlotGroup::view(vb)` is where the boilerplate collapses: it iterates `panels_`, calling `vb.canvas(*panel).depends_on(x_range, x_view, cursor, panel->y_range, panel->revision).min_size(...)` for each. The calling component just does:

```cpp
void view(ViewBuilder& vb) {
    vb.vstack([&] {
        plot_group.view(vb);
        // ...rest of the app's widgets
    });
}
```

### Cursor Sync

`PlotGroupCursor` carries only `{data_x, visible}` — no `data_y`, no per-axis magnitude. `route_plot_input`'s `MouseMove` handling, when driven through `PlotPanel::handle_canvas_input`, sets `cursor.data_x` from the hovered panel's `to_data()` conversion and leaves other panels to redraw (via `depends_on(cursor)`) with a vertical-only crosshair line at that x — no horizontal line, no coordinate readout. This reuses `draw_cursor` with a variant/flag rather than inventing nearest-sample y-interpolation (deferred — see Out of Scope).

Pan and zoom (drag, scroll) always mutate the group's shared `x_range`/`x_view`, regardless of which panel received the input event, exactly as `route_plot_input` already computes today for a standalone `x_range`/`view` — the only change is *which* `Field` it's writing into.

## Fill-Under-Curve

`SeriesStyle` gains two fields:

```cpp
struct SeriesStyle {
    Color color = Color::rgba(0, 140, 200);
    float thickness = 2.f;
    bool fill = false;
    double baseline = 0.0;
};
```

Fill color is the series' own `color` at a fixed reduced alpha (e.g. 40/255) — no separate fill-color field, keeping the struct small (YAGNI: a distinct fill color can be added later if ever needed).

### New draw primitive

```cpp
// draw_list.hpp
struct FilledPolygon {
    std::vector<Point> points;
    Color color;
};
```

Added to the `DrawCmd` variant. `DrawList::filled_polygon(std::vector<Point> pts, Color c)` follows the existing offset-translation pattern used by `polyline()`/`line()`.

### Emission

In `draw_series` (`plot_render.hpp`), when `s.style().fill` is true and `s.size() >= 2`: build a triangle-strip point list — for each data point, its curve pixel position and its baseline-projected pixel position (`map.to_pixel(x_i, baseline)`), interleaved. This is correct for x-monotonic series (the expected case: time-series line charts) without needing general polygon triangulation. Emitted via `dl.filled_polygon(...)` *before* `dl.polyline(...)` for the same series, so the stroke draws on top of the fill.

### Backend implementations

| Backend | Handling |
|---|---|
| SDL (`sdl_window.cpp`) | `SDL_RenderGeometry` over the triangle-strip vertices — SDL3 supports indexed/strip geometry directly. |
| SVG export (`svg_export.hpp`) | Single `<polygon points="...">` tracing curve-then-reversed-baseline, `fill` set, no stroke. |
| Headless `SoftwareRenderer` | No-op stub, consistent with the existing `rasterise(const Polyline&) {}` / `rasterise(const Circle&) {}` precedent — tests assert on `DrawList` command contents for this backend, not pixels. |

`bounding_box()` in `draw_list.hpp` needs a `FilledPolygon` branch (reuses the existing `requires { c.points; }` fallback since `FilledPolygon` also has a `.points` member — likely needs no code change, verify during implementation).

## System-Monitor Migration

`model_system_monitor.cpp`'s `cpu_plot`/`mem_plot`/`net_plot` (three independent `PlotModel`s) become one `PlotGroup` with three `PlotPanel`s added via `add_plot()`. `rebuild_plot()` is updated to take a `PlotPanel&`. The `view()` method's three repeated `vb.canvas(...).depends_on(...).min_size(...)` blocks collapse to one `plot_group.view(vb)` call inside the existing `vstack`. At least one series (CPU%) gets `SeriesStyle{.fill = true}` as a visible dogfood of the new option, with `baseline = 0.0`.

## Testing Strategy

- **Refactor safety**: existing `test_plot.cpp` cases for standalone `PlotModel` must pass unchanged after the `render_plot_panel`/`route_plot_input` extraction — this is a refactor of existing logic, verified byte-for-byte via current tests before any new behavior is added (TDD: extraction lands and is verified green before `PlotGroup` is introduced).
- **PlotGroup**: pan/zoom on one panel reflected in `x_range`/`x_view` and all panels' rendered mapping; cursor set via one panel's `MouseMove` reflected in `PlotGroupCursor.data_x` and visible (as vertical line only) in sibling panels; only the last-added panel receives `draw_x_axis = true`.
- **Fill-under-curve**: `draw_series` with `fill = true` emits a `FilledPolygon` command before the `Polyline` command, with the expected triangle-strip vertex count (`2 * n` for `n` data points) and baseline-projected y-coordinates; SVG emission produces a valid `<polygon>` element.

## File Organization

No new files. `PlotGroup`/`PlotPanel`/`PlotGroupCursor` and the extracted free functions live in the existing `plot.hpp`/`plot_render.hpp` pair, consistent with the original spec's file layout.
