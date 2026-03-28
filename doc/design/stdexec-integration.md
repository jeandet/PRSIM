# std::execution Integration

## Overview

PRISM uses stdexec (NVIDIA's P2300 reference implementation) as the foundation for its sender-based observer pattern and threading model. The `run_loop` scheduler drives the application thread's event loop. Sender composition enables type-safe cross-thread pipelines.

## Current State

### Dependency

stdexec is a header-only meson subproject (`subprojects/stdexec.wrap`). All stdexec includes go through `prism/core/exec.hpp`, which works around a GCC 16 constexpr-exceptions incompatibility.

### model_app — run_loop Event Loop

`model_app()` uses `stdexec::run_loop` as the application thread's scheduler:

```cpp
stdexec::run_loop loop;
auto sched = loop.get_scheduler();

// Backend thread schedules events on the app thread
backend.run([&](const InputEvent& ev) {
    exec::start_detached(
        stdexec::schedule(sched)
        | stdexec::then([&, ev] { /* process on app thread */ })
    );
});

loop.run();  // blocks until finish()
```

This replaces the previous manual `atomic_wait` + `input_pending` + `mpsc_queue` drain loop. The `run_loop` handles wake/sleep natively — the app thread sleeps at OS level when no work is scheduled.

**What was removed:** `mpsc_queue`, `std::atomic<bool> input_pending`, `std::atomic<bool> running`, manual drain loop.

**What was added:** `stdexec::run_loop`, `exec::start_detached`, `schedule | then` composition.

## Implemented

### Pipe Composition (`prism::then`)

`SenderHub` supports pipe syntax via `prism::then`. This is NOT a P2300 sender — it's syntactic sugar over `.connect()`:

```cpp
// These are equivalent:
auto conn = field.on_change().connect([](const T& val) { /* ... */ });
auto conn = field.on_change() | prism::then([](const T& val) { /* ... */ });
```

Defined in `include/prism/core/connection.hpp`. Returns `[[nodiscard]] Connection` (RAII).

### Scheduler Adaptor (`prism::on`)

`prism::on(scheduler)` wraps the downstream callback to execute on a specific scheduler's thread:

```cpp
auto conn = field.on_change()
          | prism::on(sched)
          | prism::then([&](const T& val) { /* runs on scheduler thread */ });
```

Defined in `include/prism/core/on.hpp`. Uses `exec::start_detached(schedule(sched) | then(...))` internally.

### App Scheduler Exposure (`AppContext`)

`model_app()` accepts an optional setup callback that receives an `AppContext`:

```cpp
prism::model_app("Dashboard", dashboard, [&](prism::AppContext& ctx) {
    auto sched = ctx.scheduler();
    dashboard.search.on_change()
        | prism::on(sched)
        | prism::then([&](auto& q) { filter(dashboard.tasks, q); });
});
```

### All Event Loops Migrated

`model_app`, `App::run()`, and `prism::app<State>()` all use `run_loop`. No manual `atomic_wait` / `mpsc_queue` drain loops remain.

## Roadmap

### Next: Cross-Thread Pipelines

Add an `io_scheduler` for background work, composable with the app scheduler:

```cpp
field.on_change()
    | prism::on(io_scheduler)
    | prism::then([](const std::string& path) { return load_file(path); })
    | prism::on(app_scheduler)
    | prism::then([&](auto data) { result.set(std::move(data)); });
```

## Design Decisions

### Why stdexec, not hand-rolled

- P2300 is voted into C++26 — stdexec is the reference implementation
- `run_loop` is a battle-tested scheduler with proper wake/sleep semantics
- Sender composition is type-safe and composable
- When `std::execution` lands in libstdc++, migration is mechanical (namespace change)

### Why multi-shot SenderHub stays

P2300 senders are single-shot (complete once). PRISM's `SenderHub` is multi-shot (emits on every Field change). These serve different purposes:

- **SenderHub**: multi-shot signal hub for Field changes, List mutations, input events
- **stdexec senders**: single-shot work scheduling (schedule task, run once, done)

The pipe syntax on SenderHub is sugar over `.connect()`, not a true P2300 sender. This avoids a conceptual mismatch and keeps the multi-shot semantics that Field<T> needs.

### GCC 16 Workaround

stdexec's constexpr-exceptions code path uses an incomplete type (`completion_signatures<>`) as a return type, which GCC 16 rejects. `exec.hpp` disables `__cpp_constexpr_exceptions` via `push_macro`/`undef`/`pop_macro` and marks itself as a system header to suppress the resulting warnings. This is temporary — remove when stdexec is fixed upstream.

## Open Questions

- Should the app scheduler be a `run_loop` or a custom scheduler with batching (process all pending events, then publish once)?
- Should `Field<T>` pipe syntax be `prism::then` or `stdexec::then` with a custom sender adapter?
- How does `run_loop::finish()` interact with pending scheduled work during shutdown? (Currently: unprocessed items are dropped.)
