# Reactivity — Signals, Observables & Bindings

## Overview

PRISM's reactivity system has three layers:

1. **Signals** — typed multi-cast notification (shared by input events and model changes).
2. **Observables** — properties that emit change signals automatically.
3. **Bindings** — declarative connections from model properties to widget properties.

All three are plain data + functions. No moc, no macros, no string-keyed lookups.

## Signals

A signal is a typed multi-cast sender. Multiple receivers can connect to the same signal. All receivers fire synchronously in connection order on the calling thread.

```cpp
template <typename... Args>
class signal {
    std::vector<std::function<void(Args...)>> receivers_;

public:
    connection connect(std::function<void(Args...)> fn);
    void emit(Args... args) const;
};
```

Connections are RAII — automatically disconnected on destruction:

```cpp
class connection {
    // Points back to the signal's receiver list.
    // Destructor removes the entry.
};
```

### Signals vs Qt

| Qt | PRISM |
|---|---|
| `Q_OBJECT` + moc | Plain template |
| `signals:` / `slots:` sections | `signal<Args...>` member |
| `emit` keyword | `signal.emit(args...)` |
| `connect(sender, &S::sig, receiver, &R::slot)` | `sender.sig.connect(fn)` |
| Runtime string introspection | Compile-time typed |
| Cross-thread via event loop | Cross-thread via scheduler (future) |

## Observables

An observable wraps a value and emits a change signal when modified. With C++26 reflection, all public `observable<T>` fields are automatically discoverable — no `Q_PROPERTY` macro.

```cpp
template <typename T>
class observable {
    T value_;
    signal<const T&> changed_;

public:
    const T& get() const { return value_; }

    void set(T new_value) {
        if (new_value != value_) {
            value_ = std::move(new_value);
            changed_.emit(value_);
        }
    }

    connection on_change(std::function<void(const T&)> fn) {
        return changed_.connect(std::move(fn));
    }
};
```

### Model Declaration

```cpp
struct UserModel {
    observable<std::string> username;
    observable<std::string> password;
    observable<bool>        logged_in;
};
```

No base class, no macro, no registration. With C++26 reflection, the framework can enumerate all observable fields for serialisation, undo/redo, and binding support.

## Bindings

A binding connects a model observable to a widget property. When the observable changes, the widget property updates automatically.

### One-Way Binding

```cpp
Label {
    .text = bind(model.username),
}
```

`bind()` returns a binding object that:
- Reads the current value for initial display.
- Connects to `on_change` to receive updates.
- Publishes changes through the snapshot/diff pipeline.

### Transformed Binding

```cpp
Label {
    .text = bind(model.username)
              | transform([](const std::string& s) { return "Hello, " + s; }),
}
```

### Two-Way Binding

```cpp
TextField {
    .value = bind_two_way(model.search_query),
}
```

Updates flow both directions: model change → widget update, user input → model update.

### Binding Implementation

A binding is a value type — no heap allocation for the binding itself:

```cpp
template <typename T>
struct Binding {
    observable<T>* source;
    std::function<T(const T&)> transform;  // identity if no transform

    T get() const {
        return transform ? transform(source->get()) : source->get();
    }

    connection observe(std::function<void(const T&)> fn) {
        if (transform) {
            auto xform = transform;
            return source->on_change([=](const T& v) { fn(xform(v)); });
        }
        return source->on_change(std::move(fn));
    }
};
```

## Thread Safety

Signals and observables live on the application side. They fire on the calling thread. Cross-thread notification goes through the existing snapshot/diff pipeline — never through direct signal dispatch across threads.

```
App thread: model.username.set("Alice")
                → changed_.emit("Alice")
                → binding updates widget property
                → publishes SceneDiff to diff queue
                                │
                                ▼
Render thread: drains diff queue, applies to snapshot, renders
```

The signal never crosses the thread boundary. The diff queue does.

## Future: std::execution Integration

When P2300 is available, signal connections carry scheduler annotations:

```cpp
model.username.on_change()
    | exec::on(render_scheduler)
    | exec::then([&](const std::string& name) { ... });
```

This makes the threading explicit in the type rather than implicit. The user-facing `bind()` API stays the same.

## Observable Collections

For lists (tables, virtual lists), individual-element signals would be too noisy. Instead, observable collections publish diffs:

```cpp
template <typename T>
class observable_collection {
    std::vector<T> items_;
    signal<CollectionDiff<T>> changed_;

public:
    void append(T item);    // emits Append diff
    void remove(size_t i);  // emits Remove diff
    void clear();           // emits Reset diff
    void update(size_t i, T item);  // emits Update diff
};
```

The view (VirtualList, Table) consumes diffs incrementally — no full rebuild on every change.

## Open Questions

- Should `observable::set()` be callable from any thread, or only the owning thread?
- Dependency tracking: should bindings auto-detect which observables they read (like Vue/MobX), or require explicit `bind()`?
- Computed observables: `auto full_name = computed([&] { return first.get() + " " + last.get(); });` — is this worth the complexity?
- Undo/redo: should observables record change history, or is that a separate layer?
