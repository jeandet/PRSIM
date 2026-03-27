<p align="center">
  <img src="doc/logo/logo.png" alt="PRISM logo" width="500"/>
</p>

# PRISM — Persistent Rendering & Interactive Scene Model

**This is an R&D experiment** — exploring what a 2D UI toolkit could look like if built from scratch in C++26 with no legacy constraints. Nothing here is production-ready.

> Take Qt's persistent widget tree, strip away moc and QObject, replace signal/slot strings with C++26 senders, and let P2996 reflection generate the UI from plain structs.

## The Problem

Every major UI toolkit — Qt, GTK, wxWidgets — couples the application and the renderer on a single thread. Rendering competes with business logic for CPU time, and if any step exceeds the frame budget, the UI freezes.

```mermaid
graph TD
    subgraph "Traditional single-thread event loop"
        A[Process input events] --> B[Fire signal/slot callbacks<br/><i>your model logic runs here</i>]
        B --> C[Execute layout pass<br/><i>sequential, blocks frame</i>]
        C --> D["Call paintEvent() on each widget<br/><i>rendering happens here</i>"]
        D --> E[Present frame]
        E -->|"If ANY step > 16ms"| F["Frame drop / frozen UI"]
    end
```

This is an **architectural problem**, not a tuning problem.

## Architecture

PRISM decouples the application from the renderer through a versioned, immutable scene snapshot exchanged via atomic pointer swap. Both threads sleep at OS level when idle — zero CPU when nothing changes.

```mermaid
graph TB
    subgraph app["Application Thread"]
        Model["Model structs<br/>(Field&lt;T&gt; observables)"]
        WT["Persistent WidgetTree<br/>(dirty tracking)"]
        Model -->|"Field::set() triggers sender"| WT
    end

    subgraph snap["Scene Description"]
        SS["SceneSnapshot<br/>(immutable, versioned, plain data)<br/>{widget_id → DrawList, rect, z_order, ...}"]
    end

    subgraph render["Backend Thread"]
        SDL["SDL event wait"]
        Raster["Rasterise + present"]
        SDL --> Raster
    end

    WT -->|"atomic pointer swap"| SS
    SS -->|"load latest"| Raster
    SDL -->|"InputEvent callback"| Model
```

**The frame contract:** the renderer guarantees frame delivery independent of application state. The application never blocks the renderer. The renderer never calls into application code.

## Core Abstractions: `Field<T>` and `State<T>`

Two observable types share the same core (`.get()`, `.set()`, `.on_change()`):

- **`Field<T>`** — data + observable + **widget** (reflection generates UI for it)
- **`State<T>`** — data + observable + **no widget** (backend state, synchronisation)

```cpp
struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};  // → text input
    prism::Field<bool>        dark_mode{"Dark Mode", true};      // → checkbox
    prism::State<std::string> session_token{""};                 // → no widget, still observable
};
```

Both support equality-guarded `set()` (no spurious notifications) and RAII `Connection` lifetime on `on_change()`.

## Sentinel Types & Delegates

The type inside `Field<T>` determines which **delegate** renders it. Sentinel types are templated wrappers that encode presentation semantics:

```cpp
struct Editor {
    prism::Field<std::string>         title{"Title"};              // → text input (default for StringLike)
    prism::Field<prism::Label<>>      status{"Status", {"OK"}};    // → read-only label
    prism::Field<prism::Password<>>   secret{"Password"};          // → masked input
    prism::Field<prism::Slider<>>     volume{"Volume", {.value = 0.8}};           // → continuous slider
    prism::Field<prism::Slider<int>>  quality{"Quality", {.value = 3, .min = 1,
                                               .max = 5, .step = 1}};            // → discrete slider
};
```

Delegates are resolved at compile time via **concepts**, not concrete types. A delegate matches on traits (`StringLike`, `Numeric`, `SliderRenderable`), so custom types work automatically if they satisfy the right concept:

```cpp
// Your own string type works in Label<> if it satisfies StringLike
prism::Field<prism::Label<MyString>> info{"Info", {my_string}};
```

## Component Model

Components are plain model structs. Compose by nesting — no inheritance, no macros:

```cpp
struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};
    prism::Field<bool>        dark_mode{"Dark Mode", true};
};

struct Dashboard {
    Settings settings;                        // nested component
    prism::Field<int> counter{"Counter", 0};
    prism::State<int> request_count{0};       // observable, no widget
};
```

```mermaid
graph TD
    D["Dashboard"] --> S["Settings"]
    D --> C["counter : Field&lt;int&gt;"]
    D --> R["request_count : State&lt;int&gt;<br/><i>(no widget)</i>"]
    S --> U["username : Field&lt;string&gt;"]
    S --> DM["dark_mode : Field&lt;bool&gt;"]
```

C++26 reflection (`P2996`) walks the struct members at compile time — `Field<T>` gets a widget, `State<T>` is skipped, nested structs recurse. No registration, no moc, no string-based identity.

## Three Entry Points

```mermaid
graph LR
    subgraph "Entry Points"
        MA["model_app(title, model)<br/><b>Model-driven</b><br/>Reflection generates UI"]
        MVU["app&lt;State&gt;(title, view, update)<br/><b>Retained MVU</b><br/>Manual layout"]
        RAW["App + Frame<br/><b>Raw DrawList</b><br/>No state management"]
    end
    MA -->|primary| W[WidgetTree + SceneSnapshot]
    MVU --> W
    RAW --> W
```

**1. Model-driven** (primary API) — define model structs, reflection does the rest:
```cpp
Dashboard dashboard;
prism::model_app("My App", dashboard);
```

**2. Retained layout** — manual `row()`/`column()`/`spacer()` composition:
```cpp
prism::app<State>("App", State{},
    [](auto& ui) { ui.column([&] { /* ... */ }); },
    [](State& s, const prism::InputEvent& ev) { /* ... */ }
);
```

**3. Raw DrawList** — direct rendering, no state management:
```cpp
prism::App app({.title = "Hello", .width = 800, .height = 600});
app.run([](prism::Frame& frame) {
    frame.filled_rect({10, 10, 200, 100}, prism::Color::rgba(0, 120, 215));
});
```

## Threading Model

```mermaid
sequenceDiagram
    participant App as Application Thread
    participant Snap as SceneSnapshot (atomic)
    participant Back as Backend Thread (SDL)

    Note over App: Sleeps until event or field change
    Note over Back: Blocks on SDL_WaitEvent

    Back->>App: InputEvent (mouse click)
    App->>App: field.set(new_value)
    App->>App: Sender fires → mark widget dirty
    App->>App: Rebuild dirty DrawLists only
    App->>Snap: Publish new snapshot (atomic swap)
    App->>Back: Wake (SDL_PushEvent)
    Back->>Snap: Load latest snapshot
    Back->>Back: Rasterise + present
```

Both threads sleep at OS level when idle (futex / SDL event wait). Zero CPU when nothing changes.

## C++26 Features

| Feature | Used for |
|---|---|
| Static Reflection (P2996) | Walk model structs, map `Field<T>` to widgets, generate UI |
| `std::execution` (P2300) | Signal/slot scheduling, async pipeline (future) |
| Senders/receivers | Observer pattern — `Field<T>::on_change()` + `SenderHub` |
| Concepts & Constraints | Delegate resolution (`StringLike`, `Numeric`, `SliderRenderable`), composability rules |
| `std::expected` | Fallible API operations — no exceptions at API boundary |
| Designated initialisers | Named-parameter widget construction |

## Building

Requires **GCC 16+** with C++26 reflection support and **Meson >= 1.5**.

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

The build automatically passes `-freflection` for P2996 support. Dependencies (SDL3, doctest) are fetched via Meson wraps.

## Roadmap

```mermaid
graph LR
    P1["Phase 1<br/>Core Infrastructure<br/><b>DONE</b>"]
    P2["Phase 2<br/>Layout + Observers<br/><b>DONE</b>"]
    P3["Phase 3<br/>Widgets + Rendering<br/><i>next</i>"]
    P4["Phase 4<br/>Advanced Features"]
    P5["Phase 5<br/>GPU Backend"]

    P1 --> P2 --> P3 --> P4 --> P5
```

- **Phase 1** (done) — MPSC queue, DrawList, SceneSnapshot, SDL3 backend, event-driven loop
- **Phase 2** (done) — Layout engine, hit testing, `Connection`/`SenderHub`, `Field<T>`, `List<T>`, P2996 reflection, `WidgetTree`, `model_app()`
- **Phase 3** (next) — Real widget rendering, hit_test→sender routing, concept-based delegate dispatch, `State<T>`, sentinel types (`Label<T>`, `Slider<T>`), built-in widgets (button, label, text field, checkbox, slider)
- **Phase 4** — Async sender composition, animation, accessibility, data widgets (plot, table)
- **Phase 5** — Vulkan/WebGPU backend, SDF text, tile compositing, Python bindings

## Design Documents

Detailed design rationale for each subsystem lives in [`doc/design/`](doc/design/):

- [Threading Model](doc/design/threading-model.md) — lock-free snapshot handoff, thread roles, input flow
- [Scene Snapshot](doc/design/scene-snapshot.md) — structure, versioning, dirty repaint model
- [Draw List](doc/design/draw-list.md) — command set, extensibility, serialisation
- [Render Backend](doc/design/render-backend.md) — BackendBase vtable, software vs GPU path
- [Input Events](doc/design/input-events.md) — input queue, event forwarding, hit testing
- [Layout Engine](docs/superpowers/specs/2026-03-27-layout-hit-regions-design.md) — row/column/spacer, two-pass solver, hit testing
- [Field/Sender/Widget Spec](docs/superpowers/specs/2026-03-27-field-sender-widget-design.md) — Field<T>, observer pattern, persistent widget tree
- [Delegates & Sentinels](doc/design/delegates-and-sentinels.md) — concept-driven delegates, templated sentinel types, Field vs State
- [Styling](doc/design/styling.md) — theme as data, context propagation (draft)

## License

MIT
