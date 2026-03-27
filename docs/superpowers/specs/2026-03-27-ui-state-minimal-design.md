# Minimal Ui<State> + prism::app<State>() — Design Spec

## Goal

Add the MVU entry point `prism::app<State>()` and the per-frame view context `Ui<State>`. This is the minimal shell: state access + frame escape hatch. No components, no `.on_X()` handlers, no layout — those build on top in subsequent steps.

## Scope

**In scope:**
- `Ui<State>` template class with `operator->()`, `state()`, `frame()`
- `prism::app<State>()` free function (two overloads: default-constructed and explicit initial state)
- `NullBackend` for headless testing of the MVU loop
- Tests proving the entry point works end-to-end

**Out of scope:**
- `WidgetHandle`, `.on_X()` handlers, event dispatch
- Built-in components (`button()`, `label()`, etc.)
- Layout (`row()`, `column()`, `spacer()`)
- Hit testing, widget identity

## Architecture Decision: Parallel Entry Points

`prism::app<State>()` uses `Backend` directly — it does **not** wrap `App`. `App` remains as the low-level API for users who don't want MVU. They are parallel entry points sharing the same backend infrastructure.

```
prism::app<State>()  — high-level MVU (owns state, view, event loop)
prism::App           — low-level (user manages everything via Frame)
```

Rationale: when `.on_X()` handlers arrive, `prism::app()` needs to own the event loop to interpose state mutation and re-render. Wrapping `App` would require duplicating or hooking its loop.

## Ui<State>

```cpp
// include/prism/core/ui.hpp
namespace prism {

template <typename State>
class Ui {
public:
    const State* operator->() const { return state_; }
    const State& state() const { return *state_; }
    Frame& frame() { return *frame_; }

private:
    const State* state_;
    Frame* frame_;

    Ui(const State& s, Frame& f) : state_(&s), frame_(&f) {}

    template <typename S>
    friend void app(std::string_view, std::function<void(Ui<S>&)>);
    template <typename S>
    friend void app(std::string_view, S, std::function<void(Ui<S>&)>);
};

} // namespace prism
```

- Lightweight value: two pointers, no heap allocation, no lifecycle.
- Constructed per-frame by `app()`, not user-constructible.
- `operator->()` gives read-only state access.
- `frame()` is the escape hatch to `DrawList` for custom rendering.

## prism::app<State>()

```cpp
// Also in include/prism/core/ui.hpp (template, must be header-only)
namespace prism {

template <typename State>
void app(std::string_view title, std::function<void(Ui<State>&)> view) {
    app<State>(title, State{}, std::move(view));
}

template <typename State>
void app(std::string_view title, State initial, std::function<void(Ui<State>&)> view) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    auto backend = Backend::software(cfg);
    mpsc_queue<InputEvent> input_queue;
    std::atomic<bool> running{true};
    std::atomic<bool> input_pending{false};

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
            input_queue.push(ev);
            input_pending.store(true, std::memory_order_release);
            input_pending.notify_one();
        });
    });

    backend.wait_ready();

    State state = std::move(initial);
    Frame frame;
    int w = cfg.width, h = cfg.height;

    // Initial frame
    frame.reset(w, h);
    uint64_t version = 0;
    Ui<State> ui(state, frame);
    view(ui);
    backend.submit(frame.take_snapshot(++version));
    backend.wake();

    // Event loop
    while (running.load(std::memory_order_relaxed)) {
        input_pending.wait(false, std::memory_order_acquire);
        input_pending.store(false, std::memory_order_relaxed);

        while (auto ev = input_queue.pop()) {
            if (std::holds_alternative<WindowClose>(*ev)) {
                running.store(false, std::memory_order_relaxed);
                break;
            }
            if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                w = resize->width;
                h = resize->height;
            }
        }

        if (!running.load(std::memory_order_relaxed)) break;

        frame.reset(w, h);
        Ui<State> ui(state, frame);
        view(ui);
        backend.submit(frame.take_snapshot(++version));
        backend.wake();
    }

    backend.quit();
    backend_thread.join();
}

} // namespace prism
```

This is structurally identical to `App::run()` with `State` and `Ui<State>` added. The duplication is intentional — `app()` will diverge from `App::run()` as event dispatch and state mutation are added.

## NullBackend

A headless backend for testing the MVU loop without SDL:

```cpp
// include/prism/core/null_backend.hpp
namespace prism {

class NullBackend final : public BackendBase {
public:
    void run(std::function<void(const InputEvent&)> event_cb) override {
        event_cb(WindowClose{});
    }
    void submit(std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
```

Fires `WindowClose` immediately — the MVU loop runs the view once (initial frame), then exits. Useful for all future headless MVU tests.

To use it, `prism::app()` needs a way to accept a custom backend. Add a `BackendConfig`-accepting overload or take a `Backend` directly:

```cpp
template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view);
```

The convenience overloads (`title`-only) create `Backend::software()` internally.

## File Changes

| File | Change |
|---|---|
| `include/prism/core/ui.hpp` | **New.** `Ui<State>` + `app<State>()` |
| `include/prism/core/null_backend.hpp` | **New.** `NullBackend` for headless MVU tests |
| `include/prism/prism.hpp` | Add `#include <prism/core/ui.hpp>` and `#include <prism/core/null_backend.hpp>` |
| `tests/test_ui.cpp` | **New.** Tests for `Ui<State>` + `app<State>()` |
| `tests/meson.build` | Add `test_ui.cpp` to headless tests |

No changes to `App`, `Frame`, `Backend`, `BackendBase`, or any existing test.

## Tests

1. **`app<State>()` runs view and exits on WindowClose** — Use `NullBackend`. Verify the view lambda is called at least once.
2. **`ui->` gives read-only state access** — Construct a state with known values, verify `ui->field` reads them.
3. **`ui.frame()` records draw commands** — Call `ui.frame().filled_rect(...)` inside the view, verify snapshot is non-empty (via a capturing backend or by inspecting the snapshot).
4. **Default state construction** — Use the overload without explicit initial state, verify `State{}` is used.
