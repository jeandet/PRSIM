# Field<T> + Sender/Observer + Persistent Widget Tree

**Date:** 2026-03-27
**Status:** Approved design
**Scope:** Core architecture for PRISM's widget/reactivity layer — replaces the MVU (Ui<State> + UpdateFn) model

## Context

PRISM is a C++26 UI toolkit replacing Qt without its pain points (moc, QObject, string-based connections, single-thread UI). The rendering infrastructure is implemented: DrawList, SceneSnapshot, Backend (software + headless), layout engine (row/column/spacer), hit testing, lock-free threading.

The next layer is the widget/reactivity system. The previous MVU design (per-frame view function, global State struct, UpdateFn callback) was rejected — it's too close to immediate mode and doesn't scale to complex applications with deep widget hierarchies, async data flows, and cross-component communication.

The new architecture is: **persistent widget tree generated from model structs via C++26 reflection, with sender-based observer pattern at the core.**

GCC 16 (available on Fedora 44) supports P2996 reflection (`-freflection`): `^^T`, `[:splice:]`, `std::meta::nonstatic_data_members_of()`, `std::meta::identifier_of()`, `template for`, `std::define_static_array()`. Verified working.

## Design

### Field<T> — The Core Abstraction

`Field<T>` unifies three concerns: data storage, change notification, and UI generation hint.

```cpp
template <typename T>
struct Field {
    const char* label;
    T value{};

    // Sender: emits new value on change
    auto on_change() -> sender_of<const T&>;

    // Mutate and notify
    void set(T new_value);

    // Read
    const T& get() const;
    operator const T&() const;
};
```

**Type-to-widget mapping** (compile-time, via reflection):

| `Field<T>` | Default widget |
|---|---|
| `Field<bool>` | checkbox |
| `Field<std::string>` | text_field |
| `Field<int>`, `Field<float>` | slider / spin_box |
| `Field<Enum>` | dropdown |

`Field<T>` is not a smart pointer, not an observable collection, and not a widget. It's a value with notification that happens to know how to become a widget.

### List<T> — Observable Collection

For vectors with structural mutations (insert, remove, reorder):

```cpp
template <typename T>
struct List {
    auto on_insert() -> sender_of<size_t, const T&>;
    auto on_remove() -> sender_of<size_t>;
    auto on_update() -> sender_of<size_t, const T&>;
};
```

`List<T>` publishes diffs. The backing widget (list view) consumes them incrementally — no full re-render on every mutation.

### Sender/Signal Infrastructure

The observer pattern is built on C++26 `std::execution` senders. Every `Field<T>` change produces a sender that can be composed with schedulers and transforms.

**Connection API:**

```cpp
// Simple — synchronous on current thread
field.on_change().connect([](const std::string& val) { /* ... */ });

// Composed — async with scheduler control
field.on_change()
    | exec::on(io_scheduler)
    | exec::then([](const std::string& val) { return validate(val); })
    | exec::on(ui_scheduler)
    | exec::then([&](bool valid) { status.set(valid ? "OK" : "Error"); });
```

**Connection lifecycle — RAII:**

```cpp
{
    auto conn = field.on_change().connect([&](auto& v) { /* ... */ });
    // connected
}
// automatically disconnected
```

A component's connections are scoped to its lifetime. When a model struct is destroyed, all connections from its fields disconnect. No dangling callbacks.

**Internal mechanism:**

- Each `Field<T>` holds a sender hub (list of receivers)
- `set()` iterates receivers synchronously by default
- Scheduler composition (`exec::on(scheduler)`) is opt-in for async
- The hub is single-threaded (app thread) — no mutex needed
- Cross-thread communication goes through the existing snapshot mechanism to the render thread

### Persistent Widget Tree

Widgets are persistent objects — created once, connected via senders, repainted only when dirty. The framework manages the widget tree; users write model structs.

**Tree lifecycle:**

1. User constructs a model struct
2. Framework reflects over it, creates a widget for each `Field<T>`, recurses into nested structs
3. Widgets connect to their backing `Field<T>` via `on_change()` — when the field changes, the widget marks itself dirty
4. Only dirty widgets re-record their DrawList commands
5. Framework collects dirty draw lists into a new SceneSnapshot, publishes to render thread

**The widget tree is framework-internal.** Users never touch widget objects directly. They interact through the model:

```cpp
Dashboard dashboard;
dashboard.name.set("My App");  // Field notifies widget -> dirty -> repaint
```

**Dirty propagation:**

- `Field<T>::set()` marks the backing widget dirty
- Dirty widget re-records DrawList on next frame
- Parent containers re-layout only if a child's size changed
- SceneSnapshot is rebuilt from the widget tree, but only dirty subtrees re-record

**What persists across frames:** widget identity, signal connections, widget internal state (focused, hovered, scroll position).

**What is rebuilt:** DrawList commands (only for dirty widgets), SceneSnapshot (assembled from current draw lists + geometry).

**Hit testing** stays unchanged — the existing `hit_test()` on SceneSnapshot geometry works as-is. A hit now routes to a widget's sender (e.g., button's `on_click()`) instead of a global `UpdateFn`.

### Reflection-Driven UI Generation

Using P2996 reflection (GCC 16, `-freflection`):

```cpp
struct Dashboard {
    Field<std::string> name{"Name"};
    Field<bool> dark_mode{"Dark Mode"};
    Field<Priority> priority{"Priority"};
};

// Zero boilerplate — reflection generates widget tree
prism::app("Dashboard", Dashboard{});
```

The framework iterates `nonstatic_data_members_of(^^Model)` at compile time, dispatches on the field type to create the appropriate widget, and wires `on_change()` connections automatically.

### Component Hierarchy — Struct Nesting

Nesting model structs = nesting components. Each sub-struct gets its own widget subtree.

```cpp
struct TaskEditor {
    Field<std::string> title{"Title"};
    Field<bool> done{"Done"};
};

struct Sidebar {
    Field<std::string> search{"Search"};
    List<TaskEditor> tasks;
};

struct App {
    Sidebar sidebar;
    Field<int> selected{-1};
};
```

Reflection recurses: a member that is not `Field<T>` or `List<T>` but is a struct containing such members is treated as a sub-component.

### Three Customization Levels

**1. Pure model** — reflection generates everything, default layout (vertical stack):

```cpp
prism::app("Dashboard", Dashboard{});
```

**2. Model + `view()`** — custom layout, mix auto-generated and hand-written:

```cpp
struct Dashboard {
    Field<std::string> name{"Name"};
    Field<bool> dark_mode{"Dark Mode"};

    void view(auto& ui) {
        ui.row([&] {
            ui.widget(name);
            ui.widget(dark_mode);
        });
    }
};
```

**3. `canvas()`** — raw DrawList for fully custom rendering:

```cpp
struct Plot {
    List<DataPoint> data;

    void view(auto& ui) {
        ui.canvas([&](auto& frame) {
            for (auto& pt : data)
                frame.filled_rect({pt.x, pt.y, 2, 2}, color);
        });
    }
};
```

### Cross-Component Wiring

Signal connections between components happen at the app level:

```cpp
App app;
app.sidebar.search.on_change()
    | then([&](auto& q) { filter(app.sidebar.tasks, q); });

prism::app("My App", app);
```

### Input Event Routing

- Hit test resolves click to widget ID (existing infrastructure)
- Framework looks up widget, finds its backing sender (button's `on_click()`, etc.)
- Dispatches through sender graph
- No global event handler, no manual variant dispatch

## What This Replaces

| Old (MVU) | New (Field + Sender) |
|---|---|
| `Ui<State>` per-frame context | Persistent widget tree from model structs |
| `UpdateFn<State>` global callback | Per-widget sender connections |
| `app<State>(title, view, update)` | `app(title, model)` |
| Full snapshot rebuild every frame | Dirty-flag targeted repaint |
| Components as functions | Components as nested model structs |
| Widget identity from call order | Widget identity from model field (stable) |
| `std::holds_alternative` event dispatch | Sender-based event routing |

## What Stays Unchanged

- DrawList command format and recording
- SceneSnapshot structure (geometry + draw_lists + z_order)
- BackendBase vtable, SoftwareBackend, NullBackend, TestBackend
- PixelBuffer, SoftwareRenderer (headless)
- Layout engine (row/column/spacer, measure/arrange/flatten)
- hit_test() z-order scan
- mpsc_queue, atomic wake mechanisms
- Threading model (app thread + render thread, lock-free handoff)

## Build Requirements

- GCC 16+ with `-std=c++26 -freflection`
- Meson build flag for `-freflection`
- `<meta>` standard header
