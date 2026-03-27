# Update Callback + Interactive Example — Design Spec

## Goal

Add an optional `update` callback to `prism::app<State>()` so input events can mutate state. Update the example to demonstrate input-driven UI: click changes color, arrow keys move a rectangle.

## Scope

**In scope:**
- `update` parameter added to all `app()` overloads (optional, default no-op)
- Framework calls `update(State&, InputEvent)` for every event except `WindowClose`
- `WindowResize` bookkeeping (updating w/h) happens before user's update
- Rewrite `hello_rect` example as interactive demo
- Tests proving input events mutate state and view re-runs with new state
- A `TestBackend` (or extended `NullBackend`) that fires a sequence of synthetic events

**Out of scope:**
- `.on_X()` handlers, WidgetHandle, hit testing
- Any widget-level event dispatch

## API Change

```cpp
// Core overload — update parameter added with default empty function
template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view,
         std::function<void(State&, const InputEvent&)> update = {});

// Backend + config, default state
template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view,
         std::function<void(State&, const InputEvent&)> update = {});

// Title + initial state → software backend
template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view,
         std::function<void(State&, const InputEvent&)> update = {});

// Title only → software backend + default state
template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view,
         std::function<void(State&, const InputEvent&)> update = {});
```

When `update` is empty (default), behavior is identical to before — no breaking change. Existing tests and code continue to work unmodified.

## Event Loop Change

In the core `app()` overload, the event drain loop becomes:

```cpp
while (auto ev = input_queue.pop()) {
    if (std::holds_alternative<WindowClose>(*ev)) {
        running.store(false, std::memory_order_relaxed);
        break;
    }
    if (auto* resize = std::get_if<WindowResize>(&*ev)) {
        w = resize->width;
        h = resize->height;
    }
    if (update) {
        update(state, *ev);
    }
}
```

`WindowResize` bookkeeping runs first, then the user's update. `WindowClose` is never forwarded — it's framework-internal.

## TestBackend

A backend that fires a configurable sequence of events then `WindowClose`:

```cpp
class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    void run(std::function<void(const InputEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(ev);
        event_cb(WindowClose{});
    }
    void submit(std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};
```

This replaces the need for a modified `NullBackend`. `NullBackend` stays as-is (fires only `WindowClose`).

## Example: Interactive Rect

```cpp
#include <prism/prism.hpp>

struct State {
    float x = 300, y = 250;
    uint8_t color_index = 0;
};

constexpr prism::Color colors[] = {
    prism::Color::rgba(0, 120, 215),
    prism::Color::rgba(215, 50, 50),
    prism::Color::rgba(50, 180, 50),
    prism::Color::rgba(200, 150, 0),
};

int main() {
    prism::app<State>("Interactive PRISM", State{},
        [](auto& ui) {
            auto& f = ui.frame();
            f.filled_rect({0, 0, 800, 600}, prism::Color::rgba(30, 30, 40));
            f.filled_rect({ui->x, ui->y, 200, 100}, colors[ui->color_index]);
        },
        [](State& s, const prism::InputEvent& ev) {
            if (auto* click = std::get_if<prism::MouseButton>(&ev);
                click && click->pressed) {
                s.color_index = (s.color_index + 1) % 4;
            }
            if (auto* key = std::get_if<prism::KeyPress>(&ev)) {
                constexpr float step = 20.f;
                switch (key->key) {
                case 1073741903: s.x += step; break; // SDL right
                case 1073741904: s.x -= step; break; // SDL left
                case 1073741905: s.y += step; break; // SDL down
                case 1073741906: s.y -= step; break; // SDL up
                default: break;
                }
            }
        }
    );
}
```

Note: SDL key codes for arrows are magic numbers. For the POC this is acceptable — proper `Key` enums are a future task.

## Tests

1. **Mouse click updates state** — `TestBackend` fires `MouseButton{pressed=true}`, verify state mutated in view.
2. **Key press updates state** — `TestBackend` fires `KeyPress`, verify state mutated.
3. **Multiple events accumulate** — Fire several events, verify all mutations applied.
4. **No update callback = no crash** — Existing tests still pass (update defaults to empty).
5. **WindowResize reaches update** — Verify `WindowResize` is forwarded to user's update.

## File Changes

| File | Change |
|---|---|
| `include/prism/core/ui.hpp` | Add `update` param to all 4 `app()` overloads |
| `include/prism/core/test_backend.hpp` | **New.** `TestBackend` for synthetic event sequences |
| `tests/test_ui.cpp` | Add input-driven state mutation tests |
| `examples/hello_rect.cpp` | Rewrite as interactive rect demo |
| `include/prism/prism.hpp` | Add `#include <prism/core/test_backend.hpp>` |
