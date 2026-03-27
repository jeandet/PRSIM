# Widget Model

## Overview

PRISM's widget model has two layers:

1. **User-facing:** Components are plain functions that call methods on a `Ui<State>` context. No base class, no registration, no lifecycle hooks.
2. **Internal:** Built-in components use the low-level `DrawList` + `Widget` concept. Users can drop to this level for custom rendering.

## Components as Functions

A component is a function that takes a `ui` context and data:

```cpp
void task_row(auto& ui, const Task& task, size_t index) {
    ui.row([&] {
        ui.checkbox(task.done).on_toggle([=](auto& s) {
            s.tasks[index].done = !s.tasks[index].done;
        });
        ui.label(task.title);
        ui.spacer();
        ui.button("x").on_click([=](auto& s) {
            s.tasks.erase(s.tasks.begin() + index);
        });
    });
}
```

Components compose by calling each other — just function calls. A sidebar calls selectable items, a form calls text fields and buttons.

## The Ui Context

`Ui<State>` is the interface between user code and the framework:

```cpp
template <typename State>
class Ui {
public:
    // Read-only access to current state
    const State* operator->() const;
    const State& state() const;

    // Built-in components (return handles for event chaining)
    auto button(std::string_view label) -> WidgetHandle;
    auto label(std::string_view text) -> WidgetHandle;
    auto checkbox(bool value) -> WidgetHandle;
    auto text_field(std::string_view value) -> WidgetHandle;
    auto slider(float value, float min, float max) -> WidgetHandle;

    // Layout containers
    void row(auto&& children);
    void column(auto&& children);
    void spacer();

    // Low-level (drop to DrawList)
    Frame& frame();
};
```

## Event Handlers — Retained, Not Immediate

Components return lightweight handles for chaining `.on_X()`:

```cpp
ui.button("Save").on_click([](auto& s) {
    s.saved = true;
});
```

The lambda is **not** called during the view pass. It is stored in the snapshot's event table. When the framework resolves a hit (user clicks the button region), it dispatches the handler on the app thread, then re-runs the view.

This is the key difference from immediate mode: components never return "was I clicked?" — they declare "when clicked, do this."

## Widget Identity

The framework assigns stable IDs from call-site position (source location + loop index), similar to React hooks. Users never manage widget IDs manually.

For dynamic lists, `ui.key(value)` provides explicit identity:

```cpp
for (auto& task : ui->tasks) {
    ui.key(task.id);  // stable identity across reorders
    task_row(ui, task);
}
```

## Low-Level Widget Concept (Internal)

The `Widget` concept from the POC remains as the internal primitive:

```cpp
template <typename T>
concept Widget = requires(const T w, DrawList& dl, const Context& ctx) {
    { w.record(dl, ctx) } -> std::same_as<void>;
};
```

Built-in components (`ui.button()`, etc.) use this internally. Users can drop to it for custom rendering via `ui.frame()`:

```cpp
void custom_gauge(auto& ui, float value, Rect bounds) {
    auto& frame = ui.frame();
    frame.filled_rect(bounds, Color::rgba(40, 40, 50));
    frame.filled_rect({bounds.x, bounds.y, bounds.w * value, bounds.h},
                      Color::rgba(0, 180, 80));
}
```

## DrawList as the Drawing API

`DrawList` remains both the internal rendering primitive and the escape hatch for custom rendering. There is no separate "public" vs "internal" drawing interface.

| Method | Purpose |
|---|---|
| `filled_rect()` | Solid rectangle |
| `rect_outline()` | Rectangle stroke |
| `text()` | Text at a position |
| `clip_push()` / `clip_pop()` | Clip region stack |
| `line()` / `polyline()` | Lines (planned) |
| `rounded_rect()` | Rounded corners (planned) |
| `image()` | Texture blit (planned) |

## Value Semantics

State and data passed to components are plain values. Components read state via `const State&` (through `ui->`). Mutation only happens through `.on_X()` handlers, which receive `State&` and execute outside the view pass.

## Python Bindings

Components in Python follow the same function pattern:

```python
def task_row(ui, task, index):
    with ui.row():
        ui.checkbox(task.done).on_toggle(lambda s: toggle_task(s, index))
        ui.label(task.title)
```

## Resolved Questions

- ~~How does layout interact with the widget concept?~~ Layout is built into `Ui` via `row()`, `column()`, `spacer()`.
- ~~Handle system for mutable state?~~ `.on_X()` handlers take `State&` — no per-widget mutable state needed.
- ~~Event handling — should `record()` be purely visual?~~ **Yes.** View is pure. Events are retained via `.on_X()`.
- ~~Immediate mode vs retained?~~ **Retained.** Components declare event handlers, they don't return interaction results.

## Open Questions

- Hit region resolution: how does the framework map a mouse click to the correct `.on_click()` handler when regions overlap? Z-order from snapshot?
- Keyboard focus: tab order, focus ring rendering, focus-related widget state.
- Accessibility: how do components declare labels, roles, and relationships for screen readers?
