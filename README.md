<p align="center">
  <img src="doc/logo/logo.png" alt="PRISM logo" width="500"/>
</p>

# PRISM — Persistent Rendering & Interactive Scene Model

**This is an R&D experiment** — exploring what a 2D UI toolkit could look like if built from scratch in modern C++ with no legacy constraints. Nothing here is production-ready.

> Take Qt's persistent widget tree, strip away moc and QObject, replace signal/slot strings with C++26 senders, and let model structs generate the UI — via P2996 reflection on C++26, or manual `view()` on C++23.

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

## Architecture — Model-View-Behavior (MVB)

PRISM follows a **Model-View-Behavior** pattern:

- **Model** — plain structs with `Field<T>` members. Data + change notification. Knows nothing about rendering or input.
- **View** — `Delegate<T>` specializations. Per-type rendering and widget-level input mechanics, automatic via P2996 reflection. PRISM-internal.
- **Behavior** — user-written `on_change()` / `observe()` chains. Business logic that reacts to field mutations.

The application and renderer are decoupled through a versioned, immutable scene snapshot exchanged via atomic pointer swap. Both threads sleep at OS level when idle — zero CPU when nothing changes.

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
    prism::Field<std::string> username{"jeandet"};  // → text input
    prism::Field<bool>        dark_mode{true};       // → checkbox
    prism::State<std::string> session_token{""};     // → no widget, still observable
};
```

`Field<T>` holds only the value — no display label. The member name via P2996 reflection provides identity. Display labels are a form-layout concern.

Both support equality-guarded `set()` (no spurious notifications) and RAII `Connection` lifetime on `on_change()`.

## Sentinel Types & Delegates

The type inside `Field<T>` determines which **delegate** (View layer) renders it. Sentinel types are templated wrappers that encode presentation semantics:

```cpp
enum class Theme { Light, Dark, System };

struct Editor {
    prism::Field<std::string>         title{""};                                    // → read-only text (default for StringLike)
    prism::Field<prism::Label<>>      status{{"OK"}};                               // → read-only label
    prism::Field<prism::TextField<>>  search{{.placeholder = "Search..."}};          // → editable text field
    prism::Field<prism::Password<>>   secret{{.placeholder = "API key"}};            // → masked input
    prism::Field<prism::Slider<>>     volume{{.value = 0.8}};                       // → continuous slider
    prism::Field<prism::Button>       save{{"Save"}};                               // → clickable button
    prism::Field<bool>                dark_mode{true};                              // → checkbox
    prism::Field<prism::Checkbox>     notify{{.checked = true, .label = "Enable"}}; // → checkbox with label
    prism::Field<Theme>               theme{Theme::Dark};                           // → auto-dropdown via reflection
    prism::Field<prism::Slider<int>>  quality{{.value = 3, .min = 1,
                                               .max = 5, .step = 1}};              // → discrete slider
};
```

Delegates are resolved at compile time via **concepts**, not concrete types. A delegate matches on traits (`StringLike`, `Numeric`, `ScopedEnum`), so custom types work automatically if they satisfy the right concept:

```cpp
// Your own string type works in Label<> if it satisfies StringLike
prism::Field<prism::Label<MyString>> info{{my_string}};
// Any scoped enum gets an auto-dropdown via P2996 enumerators_of
prism::Field<MyEnum> mode{MyEnum::Default};
```

## Composition by Nesting

Models are plain structs. Compose by nesting — no inheritance, no macros:

```cpp
struct Settings {
    prism::Field<std::string> username{"jeandet"};
    prism::Field<bool>        dark_mode{true};
};

struct Dashboard {
    Settings settings;                   // nested group
    prism::Field<int> counter{0};
    prism::State<int> request_count{0};  // observable, no widget
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

On C++26, P2996 reflection walks the struct members automatically — `Field<T>` gets a widget, `State<T>` is skipped, nested structs recurse. On C++23, each model provides a `view()` method using `vstack`/`hstack`:

```cpp
struct Dashboard {
    Settings settings;
    prism::Field<int> counter{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(settings, counter);   // Fields and nested components in one call
    }
};
```

Both paths produce identical output through an intermediate `Node` layer. No registration, no moc, no string-based identity.

## Three Entry Points

```mermaid
graph LR
    subgraph "Entry Points"
        MA["model_app(title, model)<br/><b>Model-driven</b><br/>Reflection generates UI"]
        MVU["app&lt;State&gt;(title, view, update)<br/><b>Retained layout</b><br/>Manual composition"]
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
using namespace prism::literals;
prism::App app({.title = "Hello", .width = 800, .height = 600});
app.run([](prism::Frame& frame) {
    frame.filled_rect({prism::Point{10_x, 10_y}, prism::Size{200_w, 100_h}},
                      prism::Color::rgba(0, 120, 215));
});
```

## Threading Model

```mermaid
sequenceDiagram
    participant App as Application Thread
    participant Snap as SceneSnapshot (atomic)
    participant Back as Backend Thread (SDL)

    Note over App: Sleeps on stdexec run_loop
    Note over Back: Blocks on SDL_WaitEvent

    Back->>App: InputEvent via run_loop scheduler
    App->>App: Delegate handle_input (View)
    App->>App: field.set() → on_change (Behavior)
    App->>App: Rebuild dirty DrawLists only
    App->>Snap: Publish new snapshot (atomic swap)
    App->>Back: Wake (SDL_PushEvent)
    Back->>Snap: Load latest snapshot
    Back->>Back: Rasterise + present
```

Both threads sleep at OS level when idle (futex / SDL event wait). Zero CPU when nothing changes.

## C++ Features

| Feature | Used for | Required |
|---|---|---|
| Static Reflection (P2996) | Auto-generate UI from model structs | **Optional** — `view()` method is the fallback |
| `std::execution` (P2300) | `run_loop` event loop, `prism::then` / `prism::on` pipe adaptors | Yes (via stdexec) |
| Senders/receivers | Observer pattern — `Field<T>::on_change()` + `SenderHub` | Yes |
| Concepts & Constraints | Delegate resolution (`StringLike`, `Numeric`, `SliderRenderable`), strong type algebra | Yes (C++20) |
| `std::expected` | Fallible API operations — no exceptions at API boundary | Yes (C++23) |
| Designated initialisers | Named-parameter widget construction | Yes (C++20) |
| magic_enum | Enum introspection fallback when P2996 is unavailable | Auto — only on pre-C++26 |

## Building

**Minimum:** C++23 compiler (GCC 14+, Clang 17+) and **Meson >= 1.5**.

**With reflection:** GCC 16+ with `-freflection` (auto-detected by the build system). When available, model structs don't need a `view()` method — P2996 generates the UI automatically.

**Without reflection:** All model structs must provide a `view()` method that describes the widget tree via `ViewBuilder`. Enum dropdowns use magic_enum (fetched automatically as a Meson wrap).

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

The build auto-detects `-freflection` support and conditionally enables P2996. Dependencies (SDL3, stdexec, doctest, magic_enum) are fetched via Meson wraps.

## Roadmap

```mermaid
graph LR
    P1["Phase 1<br/>Core Infrastructure<br/><b>DONE</b>"]
    P2["Phase 2<br/>Layout + Observers<br/><b>DONE</b>"]
    P3["Phase 3<br/>Widgets + Rendering<br/><b>DONE</b>"]
    P35["Phase 3.5<br/>stdexec<br/><b>DONE</b>"]
    P36["Phase 3.6<br/>Node Tree<br/><b>DONE</b>"]
    P4["Phase 4<br/>Advanced Features"]
    P5["Phase 5<br/>GPU Backend"]

    P1 --> P2 --> P3 --> P35 --> P36 --> P4 --> P5
```

- **Phase 1** (done) — DrawList, SceneSnapshot, SDL3 backend, event-driven loop
- **Phase 2** (done) — Layout engine, hit testing, `Connection`/`SenderHub`, `Field<T>`, `List<T>`, P2996 reflection, `WidgetTree`, `model_app()`
- **Phase 3** (done) — Delegate dispatch, SDL_Renderer + SDL3_ttf, all built-in widgets (Label, TextField, Password, TextArea, Slider, Button, Checkbox, Dropdown), overlay/popup system, keyboard focus (Tab/Shift+Tab), custom `view()` override, `canvas()` escape hatch, strong coordinate types, `clip_push` local coordinate system
- **Phase 3.5** (done) — stdexec `run_loop` event loops, `prism::then`/`prism::on` pipe adaptors, `AppContext`
- **Phase 3.6** (done) — `Node` intermediate layer (type-erased `Field<T>` + children), pre-C++26 support via `view()` methods, magic_enum enum fallback, `#if __cpp_impl_reflection` guards (only 2 locations)
- **Phase 4** — Animation, accessibility, scroll areas, data widgets (plot, table)
- **Phase 5** — Vulkan/WebGPU backend, SDF text, tile compositing, Python bindings

## Design Documents

Detailed design rationale for each subsystem lives in [`doc/design/`](doc/design/):

- [Threading Model](doc/design/threading-model.md) — lock-free snapshot handoff, thread roles, input flow
- [Scene Snapshot](doc/design/scene-snapshot.md) — structure, versioning, dirty repaint model
- [Draw List](doc/design/draw-list.md) — command set, strong coordinate types, local coordinate system, serialisation
- [Render Backend](doc/design/render-backend.md) — BackendBase vtable, software vs GPU path
- [Input Events](doc/design/input-events.md) — input queue, event forwarding, hit testing
- [Layout Engine](docs/superpowers/specs/2026-03-27-layout-hit-regions-design.md) — row/column/spacer, two-pass solver, hit testing
- [Field/Sender/Widget Spec](docs/superpowers/specs/2026-03-27-field-sender-widget-design.md) — Field<T>, observer pattern, persistent widget tree
- [Delegates & Sentinels](doc/design/delegates-and-sentinels.md) — concept-driven delegates, all sentinel types (incl. TextArea), overlay system, focus policy
- [Input Routing](docs/superpowers/specs/2026-03-27-input-routing-design.md) — hit_test → dispatch → delegate handle_input → field mutation
- [stdexec Integration](doc/design/stdexec-integration.md) — run_loop event loops, prism::then/on pipe adaptors, AppContext
- [SDL_Renderer Migration](docs/superpowers/specs/2026-03-28-sdl-renderer-migration-design.md) — SDL_Renderer + SDL3_ttf replaces PixelBuffer surface-blit
- [Dynamic Node Tree](doc/design/dynamic-node-tree.md) — `Node` intermediate layer, pre-C++26 support, ViewBuilder→Node→WidgetNode pipeline
- [Styling](doc/design/styling.md) — theme as data, context propagation (draft)
- [Components](doc/design/components.md) — `prism::Component` base class, self-wiring reusable UI + logic bundles (design only)

## License

MIT
