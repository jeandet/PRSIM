# Widget Model

## Overview

A widget is any type that can record draw commands into a `DrawList`. No base class, no inheritance, no registration system. Built-in and user-defined widgets use the exact same API.

## The Widget Concept

```cpp
template <typename T>
concept Widget = requires(const T w, DrawList& dl, const Context& ctx) {
    { w.record(dl, ctx) } -> std::same_as<void>;
};
```

That's the entire contract. A widget is a value type with a `record()` method. The `Context` carries the current theme and widget state — see [styling.md](styling.md).

## User-Defined Widgets

```cpp
// No inheritance, no macros, no registration
struct Gauge {
    float value;     // 0.0 to 1.0
    Rect  bounds;
    Color background;
    Color fill;

    void record(DrawList& dl, const Context&) const {
        dl.filled_rect(bounds, background);
        dl.filled_rect({bounds.x, bounds.y, bounds.w * value, bounds.h}, fill);
        dl.rect_outline(bounds, fill, 1.0f);
    }
};
```

## Composition

Widgets compose by aggregation. A composite widget calls `record()` on its children:

```cpp
struct LabeledGauge {
    std::string label;
    Gauge       gauge;

    void record(DrawList& dl, const Context& ctx) const {
        dl.text(label, {gauge.bounds.x, gauge.bounds.y - 20}, 12.0f, Color::rgba(0, 0, 0));
        gauge.record(dl, ctx);
    }
};
```

No special container class, no `addWidget()`, no child list management. Composition is just struct fields.

## DrawList as the Drawing API

`DrawList` is both the internal rendering primitive and the user-facing drawing API. There is no separate "public" vs "internal" drawing interface.

Available drawing operations (current and planned):

| Method | Purpose |
|---|---|
| `filled_rect()` | Solid rectangle |
| `rect_outline()` | Rectangle stroke |
| `text()` | Text at a position |
| `clip_push()` / `clip_pop()` | Clip region stack |
| `line()` / `polyline()` | Lines (planned) |
| `rounded_rect()` | Rounded corners (planned) |
| `image()` | Texture blit (planned) |
| `path()` | General vector path (planned) |
| `gradient()` | Linear/radial fill (planned) |

## Value Semantics

Widgets are plain data — stack-allocatable, copyable, movable. No heap allocation to create a widget. No pointer to a parent. No hidden mutable state.

```cpp
auto label = Label { .text = "Hello" };
auto copy  = label;                      // trivial copy
auto moved = std::move(label);           // zero cost
```

## Thread Safety

`record()` writes to a thread-local `DrawList`, receiving the current `Context` for theme/state access. The draw list is then captured into a `SceneSnapshot` and handed off to the render thread via atomic swap. The user never deals with threads or synchronisation when writing widgets.

## Python Bindings

User-defined widgets in Python follow the same pattern — a class with a `record()` method:

```python
class Gauge:
    def __init__(self, value, bounds, bg, fill):
        self.value = value
        self.bounds = bounds
        self.bg = bg
        self.fill = fill

    def record(self, dl, ctx):
        dl.filled_rect(self.bounds, self.bg)
        dl.filled_rect(Rect(self.bounds.x, self.bounds.y,
                            self.bounds.w * self.value, self.bounds.h), self.fill)
```

`DrawList` methods take plain data arguments — trivial to expose via nanobind.

## Open Questions

- How does layout interact with the widget concept? Should widgets declare size constraints, or is layout a separate concern? (For POC: manual `Rect bounds`, layout is Phase 2.)
- Declarative composition syntax (`VStack { .children = { ... } }`) — how to make this work with the Widget concept and designated initialisers?
- Handle system — how do widgets that need mutable state (text fields, checkboxes) expose updates? (Deferred to Phase 3 with reactivity.)
- ~~Event handling — should `record()` be purely visual, with a separate `hit_test()` or event concept?~~ **Decided:** `record()` is purely visual. Input dispatch is a separate concern — see [input-events.md](input-events.md).
