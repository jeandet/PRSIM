# Plot Widget — Design Spec

## Overview

A general-purpose XY plot widget for PRISM, implemented as a layered canvas component. Supports multiple data series, pan/zoom/crosshair interaction, configurable axes, and theme integration. Header-only, no new framework concepts.

## Data Model

### PlotSource Concept

```cpp
template <typename T>
concept PlotSource = requires(const T& src, size_t i) {
    { src.size() } -> std::convertible_to<size_t>;
    { src.x(i) } -> std::convertible_to<double>;
    { src.y(i) } -> std::convertible_to<double>;
};
```

Any type satisfying this concept can back a plot series. The plot queries data on demand via indexed access.

### XYData — Convenience Source

```cpp
struct XYData {
    std::vector<double> xs;
    std::vector<double> ys;
    size_t size() const { return xs.size(); }
    double x(size_t i) const { return xs[i]; }
    double y(size_t i) const { return ys[i]; }
};
```

### Series

Each series wraps a type-erased `PlotSource` plus visual style:

```cpp
struct SeriesStyle {
    Color color;
    float thickness = 2.f;
};

class Series {
    // Type-erased PlotSource (std::function or small-buffer)
    size_t size() const;
    double x(size_t i) const;
    double y(size_t i) const;
    SeriesStyle style;
};
```

Type erasure via three stored `std::function`s: `std::function<size_t()> size_fn`, `std::function<double(size_t)> x_fn`, `std::function<double(size_t)> y_fn`. Captured by value at `add_series()` time — the source object is moved/copied into the closures.

### PlotModel

```cpp
struct AxisRange {
    double min = 0.0;
    double max = 1.0;
    bool auto_fit = true;
};

struct ViewTransform {
    double offset_x = 0.0;
    double offset_y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
};

struct CursorState {
    double data_x = 0.0;
    double data_y = 0.0;
    bool visible = false;
};

struct PlotModel {
    // Data
    std::vector<Series> series;

    // Reactive state
    Field<AxisRange> x_range{};
    Field<AxisRange> y_range{};
    Field<std::string> x_label{""};
    Field<std::string> y_label{""};
    Field<ViewTransform> view{};
    Field<CursorState> cursor{};
    Field<uint32_t> revision{0};  // bumped by notify()

    // Transient interaction state (not Field — no redraw triggers)
    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    // Series management
    template <PlotSource S>
    void add_series(S source, SeriesStyle style);
    void add_series(std::vector<double> xs, std::vector<double> ys, SeriesStyle style);
    void remove_series(size_t index);
    void clear_series();
    void notify();  // ++revision

    // Canvas interface
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node);
    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);
};
```

### Usage in a Component

```cpp
struct MyApp {
    PlotModel plot;

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.canvas(plot)
            .depends_on(plot.x_range)
            .depends_on(plot.y_range)
            .depends_on(plot.view)
            .depends_on(plot.cursor)
            .depends_on(plot.revision);
    }
};
```

## Coordinate Mapping

Three coordinate spaces:

- **Data space** — actual X/Y values (e.g., 0.0–100.0)
- **Normalized space** — 0.0–1.0 within the plot area (internal)
- **Pixel space** — the `Rect` bounds from the canvas

```cpp
struct PlotMapping {
    AxisRange x_range, y_range;  // effective (after auto-fit + view transform)
    Rect plot_area;              // pixel rect excluding axis margins

    Point to_pixel(double data_x, double data_y) const;
    std::pair<double, double> to_data(Point pixel) const;

    static AxisRange apply_view(AxisRange base, double offset, double scale);
};
```

### Margins

```
┌─────────────────────────────┐
│  y_label  │   plot area     │
│  y_ticks  │                 │
│           │                 │
│           ├─────────────────┤
│           │ x_ticks         │
│           │ x_label         │
└─────────────────────────────┘
```

Margins are computed dynamically: no label = no margin. Tick text width estimated from data range magnitude. Fixed estimates are acceptable for the first iteration (e.g., 60px left margin for Y ticks, 30px bottom for X ticks).

## Rendering Layers

The `canvas()` call orchestrates four pure functions:

```
Layer 4: Overlay  — cursor crosshair + coordinate readout
Layer 3: Data     — series polylines (clipped to plot area)
Layer 2: Grid     — grid lines + tick labels + axis lines
Layer 1: Background — filled rect + border
```

### Layer 1 — Background

```cpp
void draw_background(DrawList& dl, Rect plot_area, const Theme& t);
```

Filled rect with `theme.canvas_bg`, border with `theme.border`.

### Layer 2 — Grid + Axes

```cpp
void draw_grid(DrawList& dl, const PlotMapping& map, const Theme& t);
```

- Tick values via `nice_ticks(min, max, target_count)` — snaps to 1/2/5 × 10^n intervals
- Vertical grid lines + X tick labels below plot area
- Horizontal grid lines + Y tick labels left of plot area
- Axis lines on left and bottom edges
- Grid lines use `theme.track`, tick text uses `theme.text_muted`, axis lines use `theme.border`

### Layer 3 — Data

```cpp
void draw_series(DrawList& dl, const PlotMapping& map,
                 std::span<const Series> series);
```

For each series: map all points to pixels via `PlotMapping::to_pixel()`, emit a `Polyline` draw command. Entire layer is clipped to plot area via `clip_push`/`clip_pop`.

### Layer 4 — Overlay

```cpp
void draw_cursor(DrawList& dl, const PlotMapping& map,
                 const CursorState& cursor, const Theme& t);
```

- Full-width horizontal + full-height vertical crosshair lines
- Coordinate readout text near cursor (offset to avoid edge clipping)
- Crosshair uses `theme.text_muted` with `a` set to 80 (out of 255), readout uses `theme.text` on `theme.surface`

### Orchestrator

```cpp
void PlotModel::canvas(DrawList& dl, Rect bounds, const WidgetNode& node) {
    auto& t = *node.theme;
    auto map = compute_mapping(bounds, x_range, y_range, view);

    draw_background(dl, map.plot_area, t);
    dl.clip_push(map.plot_area.origin, map.plot_area.extent);
    draw_grid(dl, map, t);
    draw_series(dl, map, series);
    draw_cursor(dl, map, cursor.get(), t);
    dl.clip_pop();
    draw_axes_labels(dl, map, x_label, y_label, t);
}
```

## Interaction

All interaction via `handle_canvas_input(const InputEvent&, WidgetNode&, Rect bounds)`.

### Cursor (MouseMove)

Convert pixel position to data coordinates via `PlotMapping::to_data()`. Set `cursor = {data_x, data_y, visible=true}`. If mouse is outside plot area, set `cursor.visible = false`.

### Zoom (MouseScroll)

- Zoom factor: `1.1^(-scroll_dy)`
- Zoom centered on cursor position in data space
- Update `view.scale_x` and `view.scale_y`, adjust `view.offset_x/y` to keep the cursor data point stationary
- Set `x_range.auto_fit = y_range.auto_fit = false`

### Pan (Click-Drag)

- `MouseButton` pressed → set `drag_mode = Pan`, record `drag_start_pixel` and `drag_start_view`
- `MouseMove` while `drag_mode == Pan` → compute pixel delta from `drag_start_pixel`, convert to data-space delta via `PlotMapping`, update `view.offset_x/y = drag_start_view.offset + delta`
- `MouseButton` released → set `drag_mode = None`
- Set `auto_fit = false` on first pan

### Reset (Double-Click)

Reset `view` to `{0, 0, 1.0, 1.0}` and set `x_range.auto_fit = y_range.auto_fit = true`.

Double-click detection: track last click time, trigger if interval < 300ms.

## Auto-Fit

```cpp
AxisRange auto_fit_range(std::span<const Series> series, Axis axis);
```

- Scans all series for min/max on the requested axis
- Adds 5% padding on each side
- Returns `{min, max, auto_fit=true}`
- Empty series → default range `{0.0, 1.0}`

Auto-fit runs at the start of `compute_mapping()` when the flag is set. Any pan or zoom sets `auto_fit = false`.

## Theme Integration

| Element | Theme field |
|---|---|
| Plot background | `canvas_bg` |
| Plot border | `border` |
| Grid lines | `track` |
| Tick text | `text_muted` |
| Axis labels | `text` |
| Crosshair lines | `text_muted` with `a=80` |
| Coordinate readout bg | `surface` |
| Coordinate readout text | `text` |
| Default series palette | Derived from `accent`, `primary` |

### Default Series Palette

```cpp
std::array<Color, 8> default_series_colors(const Theme& t);
```

Returns 8 distinguishable colors derived from `accent` and `primary` hues, rotated through the color wheel. Used when the user doesn't specify per-series colors.

## File Organization

```
include/prism/widgets/plot.hpp          — PlotModel, PlotSource, Series, XYData, public API
include/prism/widgets/plot_render.hpp   — draw_background, draw_grid, draw_series,
                                          draw_cursor, draw_axes_labels, nice_ticks,
                                          PlotMapping, compute_mapping
```

Header-only. No `.cpp` files. Rendering functions in a separate header to keep `plot.hpp` focused on the data model and to allow independent testing of layer functions.

## Testing Strategy

- **nice_ticks**: unit test with known ranges, verify tick values are human-friendly
- **PlotMapping**: unit test to_pixel/to_data roundtrip, verify margins
- **auto_fit_range**: unit test with empty/single/multi series
- **Layer functions**: headless render with SoftwareBackend, verify DrawList commands
- **Interaction**: simulate mouse events, verify ViewTransform/CursorState changes

## Out of Scope (Future)

- Log scale axes
- Custom tick formatters
- Legend box
- Multi-panel / synchronized axes
- GPU-accelerated rendering (Phase 5)
- Data downsampling for large datasets (Phase 5)
- Markers / scatter mode
