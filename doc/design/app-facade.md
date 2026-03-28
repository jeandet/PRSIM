# App Facade

> **Status: Implemented — secondary API.** The primary entry point is now `prism::model_app()` which drives the MVB architecture. `prism::app<State>()` remains available as a lower-level alternative.

## Overview

`prism::app<State>()` is a zero-boilerplate entry point. The user provides a state type and a view function. The framework handles everything else: threading, event loop, snapshot building, hit testing, event dispatch.

## Target API

```cpp
#include <prism/prism.hpp>

struct Task { std::string title; bool done = false; };

struct State {
    std::vector<Task> tasks = {{"Buy milk"}, {"Write code"}};
    std::string draft;
};

int main() {
    prism::app<State>("Todo", [](auto& ui) {
        task_input(ui);
        for (auto&& [i, task] : ui->tasks | enumerate) {
            task_row(ui, task, i);
        }
    });
}
```

The view lambda receives `Ui<State>&` — read-only state access via `ui->`, component methods on `ui`, event handlers via `.on_X()` chaining. `app()` blocks until the window is closed.

## prism::app<State>()

```cpp
template <typename State>
void app(std::string_view title, std::function<void(Ui<State>&)> view);

template <typename State>
void app(std::string_view title, State initial, std::function<void(Ui<State>&)> view);
```

Internally:
1. Creates a `Backend` (defaults to software).
2. Creates the `Ui<State>` context with initial state.
3. Runs the view lambda to build the initial snapshot.
4. Spawns the backend thread.
5. Event loop: backend forwards InputEvents → framework resolves hits → dispatches `.on_X()` handlers (mutating state) → re-runs view lambda → submits new snapshot.
6. On window close, joins backend thread and returns.

## Low-Level API (prism::App)

For advanced use cases, `prism::App` provides direct control:

```cpp
prism::App app({.title = "Hello", .width = 800, .height = 600});

app.run([](prism::Frame& frame) {
    frame.filled_rect({10, 10, 200, 100}, prism::Color::rgba(0, 120, 215));
});
```

This bypasses the MVU architecture — no state management, no event dispatch, no built-in components. The user works directly with `Frame` (DrawList wrapper). Useful for raw rendering, benchmarks, and tests.

## Ui<State>

```cpp
template <typename State>
class Ui {
public:
    const State* operator->() const;  // read-only state access
    const State& state() const;

    // Built-in components (return handles for .on_X())
    auto button(std::string_view label) -> WidgetHandle;
    auto label(std::string_view text) -> WidgetHandle;
    auto checkbox(bool value) -> WidgetHandle;
    auto text_field(std::string_view value) -> WidgetHandle;

    // Layout
    void row(auto&& children);
    void column(auto&& children);
    void spacer();

    // Escape hatch
    Frame& frame();
};
```

## Event-Driven Architecture

Both threads sleep at OS level when idle (zero CPU):

- **Backend thread** blocks on `SDL_WaitEvent` — woken by OS events or `SDL_EVENT_USER` from the app thread.
- **App thread** blocks on `std::atomic::wait` (kernel futex) — woken by `input_pending_.notify_one()` from the backend.

## Event Dispatch Flow

```
User clicks at (x, y)
    → Backend: SDL_EVENT_MOUSE_BUTTON_DOWN
    → Backend → App: InputEvent via callback
    → Framework: resolve hit region at (x, y) from snapshot
    → Framework: look up .on_click() handler in event table
    → Framework: call handler with State& (mutation happens here)
    → Framework: re-run view lambda with new state
    → Framework: submit new snapshot to backend
    → Backend: renders new frame
```

The view lambda is functionally pure — same state produces same UI. Mutation is isolated in the `.on_X()` handlers.

## Resolved Questions

- ~~Should `run()` accept a frame-rate hint?~~ **No.** Event-driven model renders only when state changes.
- ~~Should `Frame` expose the input queue?~~ **No.** Events are resolved by the framework, not polled by the user.
- ~~Should `App` support multiple windows?~~ **Deferred.**
- ~~Should `on_frame` receive events?~~ **No.** In MVU, events go through the framework's hit resolution, not to the view.

## Open Questions

- Should `prism::app<State>()` accept a `BackendConfig` for window size/title customisation?
- Animation: how does the framework handle animation-driven repaints (no input, but state changing over time)?
- Should there be a `prism::app()` variant that takes an `on_update(State&, Event)` function for centralised event handling (Elm-style `update` function) as an alternative to inline `.on_X()`?
