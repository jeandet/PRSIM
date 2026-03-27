# Reactivity — State Management & Event Dispatch

## Overview

PRISM uses MVU (Model-View-Update) as its primary architecture. The user's state struct is the single source of truth. The view is a pure function of state. Event handlers mutate state, and the framework re-renders.

The signal/observable/binding system described below is the **internal mechanism** the framework uses to track state changes and optimise re-rendering. It is not the primary user-facing API — users write `.on_X()` handlers on components.

## User-Facing API: .on_X() Handlers

```cpp
ui.button("Save").on_click([](auto& s) {
    s.saved = true;
});

ui.text_field(ui->name).on_change([](auto& s, auto val) {
    s.name = std::move(val);
});

ui.checkbox(ui->agree).on_toggle([](auto& s) {
    s.agree = !s.agree;
});
```

Handlers take `State&` and mutate it. They are stored in the snapshot's event table and dispatched by the framework when hits are resolved. The view lambda is then re-run with the new state.

## State Flow

```
         ┌──────────────────────────────────┐
         │           App Thread             │
         │                                  │
         │  State ──→ view(ui) ──→ Snapshot  │
         │    ▲                       │      │
         │    │                       │      │
         │    └── .on_X() handler ◄───┘      │
         │         (mutates State)    hit    │
         └──────────────────────────────────┘
                                      │
                              atomic swap
                                      │
         ┌──────────────────────────────────┐
         │         Backend Thread            │
         │  Snapshot ──→ rasterise ──→ present│
         │  InputEvent ──→ hit resolve ──→ ↑ │
         └──────────────────────────────────┘
```

## Internal: Signals (Framework Use)

Signals are used internally for framework-level notifications (window events, timer ticks, animation frames). They are not part of the user API.

```cpp
template <typename... Args>
class signal {
    std::vector<std::function<void(Args...)>> receivers_;
public:
    connection connect(std::function<void(Args...)> fn);
    void emit(Args... args) const;
};
```

## Internal: Change Detection

The framework needs to know when state has changed to decide whether to re-run the view. Options:

1. **Always re-run** after any `.on_X()` handler (simplest, sufficient for most apps)
2. **Dirty flag** set by handlers (opt-in optimisation)
3. **Snapshot comparison** — diff old and new state (expensive for large states)
4. **Observable fields** with change signals (fine-grained, future optimisation with reflection)

For Phase 2/3: always re-run. Optimise later if profiling shows it matters.

## Future: Reflection-Derived UI

With C++26 static reflection, the framework can auto-derive UI from state types:

```cpp
struct TaskEditor {
    Field<std::string> title{.label = "Title", .placeholder = "Enter task..."};
    Field<bool> completed{.label = "Done"};
    Field<Priority> priority{.label = "Priority"};
};

auto view = form_view<TaskEditor>(state.editor);
```

`Field<bool>` → checkbox. `Field<std::string>` → text field. `Field<Enum>` → dropdown. No registration, no macros — the type is the specification. This is the Pydantic parallel.

## Future: Observable Collections

For large lists (virtual scrolling, tables), full re-render on every change is too expensive. Observable collections publish diffs:

```cpp
template <typename T>
class observable_collection {
    std::vector<T> items_;
    signal<CollectionDiff<T>> changed_;
public:
    void append(T item);
    void remove(size_t i);
    void update(size_t i, T item);
};
```

The view consumes diffs incrementally. This is a Phase 4+ optimisation.

## Thread Safety

All state mutation happens on the app thread. The view lambda runs on the app thread. `.on_X()` handlers run on the app thread. The only cross-thread data is the immutable `SceneSnapshot`.

## Open Questions

- Should the framework support async state updates (e.g., HTTP response arrives, needs to update state)?
- Undo/redo: snapshot the entire state before each `.on_X()` handler? Or require the user to implement it?
- Computed/derived state: should there be a `computed([](const State& s) { ... })` mechanism for expensive derivations?
