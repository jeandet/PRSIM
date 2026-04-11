# State Taxonomy Design Spec

**Date:** 2026-04-11
**Priority:** Strategic #2 — Explicit state handoff API

## Goal

Make state categories explicit in the type system. Today PRISM has `Field<T>` (editable, UI-visible) and `State<T>` (hidden observable). This design adds `Derived<T>` (computed, read-only) and `Shared<T>` (cross-thread, read-only in UI) to complete the taxonomy.

## Taxonomy

| Type | Role | UI visible | Thread safety | User-writable |
|------|------|-----------|---------------|---------------|
| `Field<T>` | Editable model data | Yes (interactive widget) | UI thread only | Yes |
| `State<T>` | Hidden observable | No | UI thread only | Yes |
| `Derived<T>` | Computed from sources | Yes (read-only) | UI thread only | No |
| `Shared<T>` | Cross-thread observable | Yes (read-only) | Any thread → UI | Yes |

`Transient<T>` is explicitly out of scope (YAGNI — no serialization exists yet).

## Derived<T>

### API

```cpp
template <typename T>
struct Derived {
    template <typename Fn, typename... Sources>
    Derived(Fn&& compute, Sources&... sources);

    const T& get() const;
    operator const T&() const;

    SenderHub<const T&>& on_change();
    void observe(std::function<void(const T&)> cb);
};
```

### Semantics

- Signal-graph style: user provides a computation lambda + explicit source list.
- Constructor connects to each source's `on_change()`. Sources can be any type with an `on_change()` method: `Field<T>`, `State<T>`, `Shared<T>`, or another `Derived<T>`.
- On source change: re-evaluates, compares with previous value, emits only if different.
- Owns its `Connection` objects (auto-disconnects on destruction).
- No `set()` — read-only by design.

### Example

```cpp
Field<WaveShape> shape{WaveShape::Sine};
Field<Slider<>>  frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};

Derived<float> rms{[&] { return compute_rms(shape.get(), frequency.get()); },
                   shape, frequency};
```

### Transaction integration

- Recomputation is deferred during active transactions.
- Transaction commit order: (1) fire source callbacks, (2) recompute all dirty `Derived<T>`, (3) fire derived callbacks.
- Diamond dependencies (A → B, A → C, B+C → D) resolve naturally: D recomputes once after both B and C are updated.

## Shared<T>

### API

```cpp
template <typename T>
struct Shared {
    Shared() = default;
    Shared(T init);

    const T& get() const;
    void set(T new_value);

    SenderHub<const T&>& on_change();
    void observe(std::function<void(const T&)> cb);

    void drain_notifications();
};
```

### Semantics

- Dual synchronization: `atomic_cell<T>` for storage + MPSC queue for coalesced notifications.
- `set()` atomically stores the new value, then pushes a notification to the queue. Multiple `set()` calls between drains coalesce to a single notification.
- `get()` always returns the freshest value (atomic read).
- `drain_notifications()` is called by the UI thread's event loop; fires `on_change()` callbacks on the UI thread with the current value.
- No implicit `operator const T&()` — forces explicit `get()` to keep cross-thread reads visible in code.

### Example

```cpp
struct SensorModel {
    Field<float> gain{1.0f};
    Shared<float> temperature{22.5f};  // written from sensor thread
    Derived<std::string> status{[&] {
        return fmt::format("T={:.1f}°C (gain {:.1f})", temperature.get(), gain.get());
    }, temperature, gain};
};
```

## Traits

In `traits.hpp`:

```cpp
template <typename T> inline constexpr bool is_derived_v = false;
template <typename T> inline constexpr bool is_derived_v<Derived<T>> = true;

template <typename T> inline constexpr bool is_shared_v = false;
template <typename T> inline constexpr bool is_shared_v<Shared<T>> = true;
```

## Reflection

### for_each_member

Visits `Derived<T>` and `Shared<T>` in addition to `Field<T>` and components:

```cpp
if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M> || is_component_v<M>) {
    fn(member);
}
```

`for_each_field` stays unchanged (only `Field<T>`).

### check_is_component

A struct is a component if it has any `Field<T>`, `State<T>`, `Derived<T>`, or `Shared<T>` member.

## Delegates & UI Rendering

No new delegate specializations. `Derived<T>` and `Shared<T>` reuse existing `Delegate<T>::record()` for the inner type `T`, but:

- Widget node gets `FocusPolicy::none`.
- No `handle_input` wiring (non-interactive).
- Rendered with `t.text_muted` color to visually signal read-only.

Dirty flagging uses the existing mechanism:
- `Derived<T>`: marks widget node dirty after recomputation if value changed.
- `Shared<T>`: marks widget node dirty when `drain_notifications()` fires `on_change()`.

## Event Loop Integration

`model_app` event loop order becomes:

```
drain input → drain Shared notifications → run schedulers → repaint dirty
```

`Shared<T>` drain callbacks are discovered during widget tree build via reflection. The build step collects `std::vector<std::function<void()>>` of drain functions. The event loop calls them each tick.

## Files

### New files
- `include/prism/core/derived.hpp`
- `include/prism/core/shared.hpp`

### Modified files
- `core/traits.hpp` — add `is_derived_v`, `is_shared_v`
- `core/reflect.hpp` — visit new types in `for_each_member`, update `check_is_component`
- `core/transaction.hpp` — two-phase commit (source callbacks, then derived recomputation)
- `ui/node.hpp` — no structural changes (uses existing type-erased build_widget/on_change)
- `app/widget_tree.hpp` — build read-only nodes for new types, collect drain callbacks
- `app/model_app.hpp` — drain `Shared<T>` notifications each tick
- `prism.hpp` — include new headers

## Tests

- `Derived<T>`: recomputation on source change, no-change suppression, transaction deferral, diamond dependency
- `Shared<T>`: cross-thread set + drain, coalesced notifications, integration with dirty repaint
- Both: reflection discovery, read-only widget node rendering

## Out of scope

- `Transient<T>` (no serialization exists)
- Serialization
- `for_each_observable` convenience function
- `Shared<T>` implicit conversion operator
