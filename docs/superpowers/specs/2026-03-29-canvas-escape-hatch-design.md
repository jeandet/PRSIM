# Canvas Escape Hatch — Design Spec

## Summary

Add `vb.canvas(model)` to `ViewBuilder`, allowing model structs to provide a `canvas(DrawList&, Rect, const WidgetNode&)` method for custom rendering. The canvas node participates in layout as an expandable leaf — it fills available space like a spacer but also renders content. Optional `handle_canvas_input()` for custom input handling.

## Motivation

Standard delegates handle common widget types, but many applications need custom-drawn areas: plots, waveform visualizers, hex editors, game viewports, data grids. The canvas escape hatch lets a struct draw directly into a `DrawList` while keeping its Field-based widgets for controls — all in the same `view()` layout.

## Design

### Detection

A struct is a canvas source if it has a `canvas()` method with this signature:

```cpp
void canvas(DrawList& dl, Rect bounds, const WidgetNode& node);
```

Detected at compile time via `requires`:

```cpp
template <typename T>
concept HasCanvas = requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
    t.canvas(dl, r, n);
};
```

Optional input handling:

```cpp
template <typename T>
concept HasCanvasInput = requires(T& t, const InputEvent& ev, WidgetNode& n, Rect r) {
    t.handle_canvas_input(ev, n, r);
};
```

### ViewBuilder API

```cpp
class ViewBuilder {
public:
    // Existing...
    template <typename T>
    void widget(Field<T>& field);
    template <typename C>
    void component(C& comp);
    void row(std::invocable auto&& fn);
    void column(std::invocable auto&& fn);
    void spacer();

    // New:
    template <HasCanvas T>
    void canvas(T& model);
};
```

### Canvas Node

`vb.canvas(model)` creates a leaf `WidgetNode` with:

- `is_container = false`
- `layout_kind = LayoutKind::Canvas` (new enum value)
- `record` closure calls `model.canvas(dl, bounds, node)` where `bounds` is the node's allocated rect
- `wire` closure connects `handle_canvas_input()` if present
- `focus_policy = tab_and_click` if `HasCanvasInput`, else `none`

### Layout Behavior

Canvas nodes behave like spacers in layout: `expand = true`. But unlike spacers, they have a minimum preferred size (configurable, default 0x0) and they produce draw commands.

In `build_layout()`:

```cpp
if (node.layout_kind == LK::Canvas) {
    LayoutNode canvas_leaf;
    canvas_leaf.kind = LayoutNode::Kind::Canvas;  // new Kind
    canvas_leaf.id = node.id;
    canvas_leaf.hint = {.preferred = 0, .expand = true};
    canvas_leaf.draws = node.draws;
    canvas_leaf.overlay_draws = node.overlay_draws;
    parent.children.push_back(std::move(canvas_leaf));
}
```

In `layout_measure()`: Canvas nodes report `preferred = 0, expand = true` — they absorb all remaining space after fixed-size siblings are measured. The `cross` hint is also 0 since canvas content fills whatever cross-axis space is available.

In `layout_flatten()`: Canvas nodes are treated like leaves — they have geometry and draw commands.

### Record Cycle

The canvas `record` closure differs from delegate-based leaves: it receives the **allocated bounds** (from layout) so the user can draw proportional content. This requires a two-phase approach:

1. **Initial record** at build time: draws into a small placeholder rect (e.g. `{0,0,1,1}`) — just enough for layout to know the node exists.
2. **Post-layout re-record**: After `layout_arrange()` resolves geometry, canvas nodes are re-recorded with their actual allocated rect.

Implementation: the `record` closure for canvas nodes captures a `Rect* bounds_ptr` that points into a stored `Rect` on the `WidgetNode` (via `edit_state` or a dedicated field). After layout, `refresh_canvas_nodes()` updates the bounds and calls `record()`.

Simpler alternative (recommended): canvas nodes always record with `{0, 0, allocated_width, allocated_height}` — they draw in local coordinates. The layout system's `translate_draw_list()` already handles positioning. This means:

- `record()` is called with `Rect{0, 0, W, H}` where W,H come from layout
- The canvas draws at `{0,0}` origin
- `layout_flatten()` translates to screen position

This matches how `clip_push` local coordinates already work.

**When is record() called?** Canvas `record()` is called during `build_snapshot()`, after layout resolves geometry. The `WidgetNode` stores a `Rect canvas_bounds` that is updated from the layout pass. The `record` closure reads this field. Dirty tracking works normally — if any Field the canvas reads changes, the struct marks the canvas node dirty (via a manual `mark_dirty()` call or by connecting observed Fields).

### Dirty Tracking

Unlike delegate-based leaves (where `Field<T>::on_change()` auto-marks the node dirty), canvas content depends on whichever Fields the `canvas()` method reads. Two approaches:

**Approach chosen: explicit dependency.** `vb.canvas(model)` returns a `CanvasHandle` that supports `.depends_on(field)`:

```cpp
void view(WidgetTree::ViewBuilder& vb) {
    vb.widget(frequency);
    vb.widget(amplitude);
    vb.canvas(*this).depends_on(frequency).depends_on(amplitude);
}
```

Each `.depends_on(field)` connects `field.on_change()` → mark canvas node dirty. This is explicit, zero-overhead, and matches the existing dirty tracking pattern.

### Input Handling

If the model provides `handle_canvas_input()`:

```cpp
void handle_canvas_input(const InputEvent& ev, WidgetNode& node, Rect bounds);
```

The canvas node's `wire` closure connects `on_input` to call this method. The `bounds` argument is the current allocated rect (same as passed to `canvas()`), letting the handler do its own hit math relative to the canvas area.

If no `handle_canvas_input()` exists, the canvas node is non-interactive (no focus, no input dispatch).

### Usage Example — Waveform Visualizer

```cpp
struct Waveform {
    prism::Field<prism::Slider<>> frequency{{.value = 2.0, .min = 0.5, .max = 10.0}};
    prism::Field<prism::Slider<>> amplitude{{.value = 0.8, .min = 0.0, .max = 1.0}};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        auto w = bounds.extent.w.raw();
        auto h = bounds.extent.h.raw();

        // Background
        dl.filled_rect(bounds, prism::Color::rgba(20, 20, 30));

        // Draw sine wave as horizontal bars
        float freq = frequency.get().value;
        float amp = amplitude.get().value;
        int steps = static_cast<int>(w / 2);
        float bar_w = w / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            float y_val = amp * std::sin(2.0f * 3.14159f * freq * t);
            float bar_h = std::abs(y_val) * h * 0.4f;
            float bar_y = h * 0.5f - (y_val > 0 ? bar_h : 0);

            dl.filled_rect(
                prism::Rect{
                    prism::Point{prism::X{i * bar_w}, prism::Y{bar_y}},
                    prism::Size{prism::Width{std::max(bar_w - 1, 1.0f)},
                                prism::Height{bar_h}}},
                prism::Color::rgba(0, 180, 80));
        }
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(frequency);
            vb.widget(amplitude);
        });
        vb.canvas(*this).depends_on(frequency).depends_on(amplitude);
    }
};
```

### Dashboard Demo Addition

The existing `model_dashboard.cpp` gets a `Waveform` sub-component:

```cpp
struct Dashboard {
    Settings settings;
    Waveform waveform;  // new: canvas demo
    prism::Field<prism::Label<>> status{{"All systems go"}};
    // ... rest unchanged
};
```

## What Changes

| File | Change |
|------|--------|
| `widget_tree.hpp` | `WidgetNode::LayoutKind::Canvas` enum value, `WidgetNode::canvas_bounds` field, `ViewBuilder::canvas()` method, `CanvasHandle` return type, canvas node in `build_layout()`, post-layout canvas re-record in `build_snapshot()` |
| `layout.hpp` | `LayoutNode::Kind::Canvas` enum value, canvas handling in `layout_measure()` (expand=true), `layout_flatten()` (same as leaf) |
| `model_dashboard.cpp` | Add `Waveform` sub-component with sine wave canvas |

## What Stays Unchanged

- `DrawList`, `SceneSnapshot`, `BackendBase` — unchanged
- All existing delegates — unchanged
- `Field<T>`, `State<T>`, `SenderHub`, `Connection` — unchanged
- Existing `view()` override behavior — unchanged, canvas is additive
- `model_app()` event loop — unchanged, canvas input routed through existing dispatch
