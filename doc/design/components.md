# Components: Reusable UI + Logic Bundles

## Overview

A `Component` is an inheritance-based base class that lets a struct bundle both its **Model** (Fields) and its **Behavior** (setup logic) in MVB terms. Components are self-contained, composable, and reusable — analogous to FastAPI sub-apps or VHDL entities with ports.

Today, models are plain structs: reflection builds the widget tree (the **View** is automatic), but all **Behavior** wiring lives in the top-level `model_app` setup lambda. Components move the Behavior into the struct itself, enabling library-grade reusable pieces (hex editor, plot widget, sensor dashboard panel).

## Motivation

Without Component, a reusable "gas sensor panel" requires the consumer to know and reproduce its internal Behavior wiring. With Component, the consumer just nests it and connects to its external-facing Fields:

```cpp
// Without Component — consumer must wire internals
prism::model_app("Dashboard", dashboard, [&](AppContext& ctx) {
    // repeated per sensor instance, must know GasSensor internals
    dashboard.sensor_a.pressure.on_change() | prism::on(ctx.scheduler())
        | prism::then([&](const Slider<>& s) {
            if (s.value > 0.9) dashboard.sensor_a.status.set({"WARNING"});
        });
    // ... same for sensor_b, sensor_c, ...
});

// With Component — internals are self-wired
struct Dashboard : prism::Component {
    GasSensor sensor_a;  // wires itself
    GasSensor sensor_b;  // wires itself
    Field<Label<>> summary{{"All OK"}};

    void setup(AppContext& ctx) override {
        // only wire the interface — connect child outputs
        connect(sensor_a.status.on_change() | prism::on(ctx.scheduler())
            | prism::then([this](const Label<>& l) {
                if (l.text == "WARNING") summary.set({"Check sensor A!"});
            }));
    }
};
```

Components also split complexity: even with a single instance, encapsulating logic + UI together makes the code easier to reason about.

## Base Class

```cpp
namespace prism {

class Component {
public:
    virtual ~Component() = default;

    // Self-wiring hook — called bottom-up after tree is built
    virtual void setup(AppContext& ctx) {}

    // Custom rendering surface (future — requires canvas infrastructure)
    virtual void canvas(DrawList& dl, const Rect& bounds, const WidgetNode& node) {}

    // Custom input handling for canvas components (future)
    virtual void handle_input(const InputEvent& ev, WidgetNode& node) {}

protected:
    void connect(Connection conn) { connections_.push_back(std::move(conn)); }

private:
    std::vector<Connection> connections_;
};

} // namespace prism
```

- `setup()` defaults to no-op so pure containers don't need to override
- `canvas()` and `handle_input()` default to no-op — a Component with neither is a pure Field container, with both is a custom rendering surface
- `connect()` is protected — only the component itself stores its wiring
- Connections are destroyed with the Component

## Detection and Tree Integration

`build_container` currently detects components via reflection (any struct with Fields/States). With the Component base class, detection splits:

- **Plain struct** (current behavior): detected by reflection, no `setup()` called, parent wires manually
- **Component subclass**: detected by `std::is_base_of_v<Component, T>`, tree built recursively from Fields the same way, instance registered for `setup()` invocation

During tree construction, `build_container` collects `Component*` pointers. After the full tree is built and indexed, `setup()` is called **bottom-up** — children before parents. This guarantees a parent's `setup()` can rely on nested Components being fully wired.

```cpp
// In WidgetTree constructor (pseudocode)
WidgetTree(Model& model) {
    root_ = build_container(model);  // builds tree, collects Component pointers
    build_index(root_);

    // Bottom-up: children registered first, called first
    for (auto* comp : components_)
        comp->setup(app_context_);

    clear_dirty();
}
```

## Composition Patterns

### Container of Fields — bundles UI + logic

The simplest and most common pattern. The Component owns Fields, wires them in `setup()`, and the parent interacts only through the public Fields (its "ports"):

```cpp
struct GasSensor : prism::Component {
    Field<Label<>> name{{""}};
    Field<Slider<>> pressure{{}};
    Field<Label<>> status{{"OK"}};

    void setup(AppContext& ctx) override {
        connect(pressure.on_change() | prism::on(ctx.scheduler())
            | prism::then([this](const Slider<>& s) {
                if (s.value > 0.9) status.set({"WARNING"});
            }));
    }
};
```

### Custom rendering surface (future, requires canvas)

The Component IS the visual — no child Fields, just a custom-drawn area with custom input:

```cpp
struct PlotView : prism::Component {
    Field<std::vector<float>> data{{}};

    void canvas(DrawList& dl, const Rect& bounds, const WidgetNode& node) override {
        // custom plot rendering — pan, grid, series
    }
    void handle_input(const InputEvent& ev, WidgetNode& node) override {
        // mouse drag for pan, scroll for zoom
    }
};
```

### Hybrid — custom surface + child Fields

Fields provide standard widgets (toolbar, search bar); the canvas provides the custom area:

```cpp
struct HexEditor : prism::Component {
    Field<TextField<>> search_bar{{}};
    Field<Label<>> offset_display{{"0x0000"}};

    void canvas(DrawList& dl, const Rect& bounds, const WidgetNode& node) override {
        // hex grid rendering
    }
    void handle_input(const InputEvent& ev, WidgetNode& node) override {
        // cursor movement, selection, editing
    }
    void setup(AppContext& ctx) override {
        // wire search bar to scroll position
    }
};
```

### Layered composition — widgets inside components

A `TextEdit` delegate (atomic widget) can be used inside a `TextEditor` component that adds line numbers, syntax highlighting, file I/O, and a search toolbar. Delegates handle atomic rendering; Components handle orchestration.

### Top-level app as Component

`model_app` just becomes "run this Component as a window." If the model is a plain struct, current behavior is preserved (setup lambda still works):

```cpp
int main() {
    Dashboard dashboard;
    prism::model_app("Dashboard", dashboard);  // setup() called automatically
}
```

## Backward Compatibility

- Plain structs (no Component base) work exactly as today — no migration needed
- The `model_app` setup lambda remains available for top-level wiring
- A model can mix plain nested structs and Component subclasses in the same tree
- Existing delegates, layout, rendering, and input routing are unchanged

## Design Decisions

### Why inheritance, not concepts

Three approaches were considered:

1. **Inheritance** (`struct Foo : Component`) — explicit, familiar (Qt-like), provides `connect()` helper and connection storage for free
2. **Concept-based** (detect `setup()` method via reflection) — no base class, but requires manual connection storage in every component
3. **CRTP** (`struct Foo : Component<Foo>`) — static polymorphism, but `canvas()` and `handle_input()` need virtual dispatch anyway for the tree to call them uniformly

Inheritance was chosen for simplicity and alignment with the Qt-like SDK design principles. The vtable cost is negligible: `setup()` runs once at init, `handle_input()` at event rate.

### Why bottom-up setup order

Children wire first, parents wire second. A parent's `setup()` can assume nested Components are fully operational. This matches VHDL elaboration order and avoids ordering bugs.

### Why Component owns connections

The alternative (tree owns connections, Component returns them) adds complexity for no benefit in the static case. Self-contained ownership means the Component is a single unit of deployment — move it between projects and it just works.

## Deferred (separate designs)

| Topic | Why separate |
|-------|-------------|
| `ComponentList<T>` (dynamic instances) | Touches list/table widgets, partial tree rebuild, virtual scrolling — entirely different design space |
| `canvas()` / `handle_input()` implementation | Depends on canvas rendering infrastructure not yet built |
| Layout hints (min/max size, stretch) | Layout system concern, needed for canvas components but independent |
| Theming / style context | Needed for library components to match host app appearance |
| Field visibility (internal vs port) | "Private" Fields hidden from parent tree — interesting but not blocking |
| Component lifecycle (teardown, suspend) | Only needed with dynamic add/remove |

## Open Questions

- Should `setup()` receive the Component's own `WidgetNode&` in addition to `AppContext`? This would let it inspect its tree position, but adds coupling.
- For hybrid Components (Fields + canvas), how does layout decide the canvas area's size relative to the Fields? This overlaps with the layout hints design.
- Should there be a way to declare a Field as "internal" (no widget generated, but still observable)? `State<T>` fills this role partially, but a Field that renders only within the component's canvas would be different.
