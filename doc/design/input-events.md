# Input Events

## Overview

Input events flow in the opposite direction of draw commands: from the OS/presenter into the application. The render thread never interprets or dispatches input — it only forwards raw events into a queue. All hit testing, dispatch, and callback execution happen on the application side.

In MVB terms, input events are the bridge between the OS and the **View** layer (delegates). Delegates translate raw input into field mutations; the **Behavior** layer (user-written observers) reacts to those mutations.

## Event Types

Input events are plain data — no polymorphism, no inheritance:

```cpp
struct MouseMove   { Point position; };
struct MouseButton { Point position; uint8_t button; bool pressed; };
struct MouseScroll { Point position; float dx, dy; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct TextInput   { std::string text; };
struct WindowResize { int width, height; };
struct WindowClose {};

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput,
    WindowResize, WindowClose
>;
```

**Implementation notes:**
- `MouseButton` uses raw `uint8_t button` + `bool pressed`. Core headers define their own key/modifier constants (matching SDL values) to stay SDL-free.
- `TextInput` carries UTF-8 character input from the OS (SDL_EVENT_TEXT_INPUT). Used by TextField/Password delegates for text editing.
- `KeyPress`/`KeyRelease` use `int32_t key` + `uint16_t mods`. Key constants defined in `prism/core/keys.hpp` (not SDL headers).

Synthetic events can be pushed for testing.

## Data Flow

```
OS / SDL3
    │
    │  SDL_WaitEvent (render thread blocks here)
    ▼
Backend callback
    │
    ├→ map SDL event → InputEvent
    ├→ schedule on app thread via stdexec::run_loop
    │
    ▼
Application thread (run_loop scheduler)
    │
    ├→ hit_test() — overlay geometry first, then normal z-order
    ├→ update_hover() / set_pressed() / advance_focus()
    ├→ localize_mouse(ev, widget_rect) — convert to widget-local coords
    ├→ Delegate<T>::handle_input() — View mechanics (click → toggle, key → edit)
    ├→ Field<T>::set() triggers on_change() — Behavior reactions
    ├→ rebuild SceneSnapshot (dirty widgets only)
    ├→ publish snapshot via atomic_cell
    └→ SDL_PushEvent(USER) — wake render thread
```

The render thread forwards events via `exec::start_detached(schedule(sched) | then(...))`. Both threads sleep at OS level when idle. The previous `mpsc_queue` + `atomic_wait` mechanism was replaced by stdexec `run_loop`.

## Hit Testing

Hit testing runs on the application side using the geometry from the latest `SceneSnapshot` — a flat list of (widget_id, rect) pairs:

```cpp
std::optional<WidgetId> hit_test(const SceneSnapshot& snap, Point pos) {
    // Walk z_order back-to-front, return first hit
    for (auto i = snap.z_order.rbegin(); i != snap.z_order.rend(); ++i) {
        auto& [id, rect] = snap.geometry[*i];
        if (rect.contains(pos))
            return id;
    }
    return std::nullopt;
}
```

No separate GeometrySnapshot type — `SceneSnapshot::geometry` is the single source of truth. No tree traversal — just a flat scan in z-order. Fast, simple, testable.

## Signals

Input dispatch uses the same signal mechanism as the rest of PRISM's reactivity. A signal supports multiple receivers — same pattern as Qt's signal/slot but without moc or QObject:

```cpp
template <typename... Args>
class signal {
    std::vector<std::function<void(Args...)>> receivers_;

public:
    connection connect(std::function<void(Args...)> fn) {
        receivers_.push_back(std::move(fn));
        return connection{ /* handle for disconnection */ };
    }

    void emit(Args... args) const {
        for (auto& fn : receivers_)
            fn(args...);
    }
};
```

Multiple receivers on the same signal:

```cpp
button.on_click.connect([&] { model.submit(); });
button.on_click.connect([&] { analytics.log("submit_clicked"); });
button.on_click.connect([&] { status.set_text("Submitting..."); });
```

All receivers fire synchronously on the app thread, in connection order.

## Widget-Level Ergonomics

At the declarative level, `on_click` is sugar for connecting a receiver during widget tree construction:

```cpp
Button {
    .label    = "OK",
    .on_click = [&] { model.submit(); },
}
```

Additional connections are possible via handles:

```cpp
prism::Handle<Button> btn_handle;

Button {
    .label    = "OK",
    .on_click = [&] { model.submit(); },
    .handle   = &btn_handle,
}

// Later — add a second receiver
btn_handle.on_click.connect([&] { logger.log("clicked"); });
```

## Connection Lifecycle

Connections are RAII — they disconnect automatically when destroyed:

```cpp
{
    auto conn = button.on_click.connect([&] { ... });
    // connected
}
// automatically disconnected here
```

When a widget is removed from the tree, all connections scoped to it are cleaned up. No dangling callbacks, no manual `disconnect()` needed.

## Focus Management — **Implemented**

Focus is managed by the `WidgetTree` itself, not a separate `FocusManager` struct. Each `Delegate<T>` declares a compile-time `FocusPolicy`:

```cpp
enum class FocusPolicy : uint8_t { none, tab_and_click };
```

The WidgetTree builds a focus chain from all widgets whose delegate has `focus_policy == tab_and_click`:

- **Tab / Shift+Tab** — `advance_focus(+1)` / `advance_focus(-1)` cycles through focusable widgets (wrapping)
- **Click** on a focusable widget sets focus via `set_focus(id)`
- **Space / Enter** on a focused widget dispatches to the delegate's `handle_input()`
- Focused widgets render a blue focus ring (`RectOutline`, Color::rgba(80,160,240), 2px)

Keyboard events (KeyPress, TextInput) are dispatched to the focused widget's delegate. Delegates that need text editing (TextField, Password) consume TextInput events for character insertion and KeyPress for navigation (arrows, Home/End, Backspace, Delete).

## Testing

Input is fully testable without a window:

```cpp
TEST_CASE("button click dispatches on_click") {
    auto geo = GeometrySnapshot{ ... };  // known layout
    auto input_queue = mpsc_queue<InputEvent>{};
    bool clicked = false;

    button.on_click.connect([&] { clicked = true; });

    // Inject synthetic events
    input_queue.push(MouseButton{ .position = {50, 25}, .button = Button::Left, .action = Action::Press });
    input_queue.push(MouseButton{ .position = {50, 25}, .button = Button::Left, .action = Action::Release });

    // Drain and dispatch
    process_input(input_queue, geo);

    CHECK(clicked);
}
```

No OS, no window, no presenter. Just data in, state out.

## std::execution Integration — **Implemented**

Event dispatch uses stdexec's `run_loop` scheduler. The **Behavior** layer (user-written reactions) uses `prism::on(scheduler)` + `prism::then()` pipe syntax:

```cpp
prism::model_app("Dashboard", dashboard, [&](prism::AppContext& ctx) {
    auto sched = ctx.scheduler();
    // Behavior: domain logic reacting to field changes
    connections.push_back(
        dashboard.volume.on_change()
        | prism::on(sched)
        | prism::then([&](const Slider<>& s) {
              if (s.value > 0.9) dashboard.status.set({"Volume is very high!"});
          })
    );
});
```

## Open Questions

- Event bubbling: should unhandled events propagate to parent widgets? If so, opt-in or opt-out?
- Gesture recognition (drag, pinch, long press): built into the input layer or a separate composable layer?
- Input event coalescing: should multiple mouse moves between frames be coalesced into one?
- Touch input: separate event types or unified pointer events?
