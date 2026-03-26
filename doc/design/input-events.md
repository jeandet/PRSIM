# Input Events

## Overview

Input events flow in the opposite direction of draw commands: from the OS/presenter into the application. The render thread never interprets or dispatches input — it only forwards raw events into a queue. All hit testing, dispatch, and callback execution happen on the application side.

## Event Types

Input events are plain data — no polymorphism, no inheritance:

```cpp
struct MouseMove   { Point position; };
struct MouseButton { Point position; Button button; Action action; };
struct MouseScroll { Point position; float dx, dy; };
struct KeyPress    { Key key; Modifiers mods; };
struct KeyRelease  { Key key; Modifiers mods; };
struct TextInput   { std::string text; };
struct FocusGained { WidgetId target; };
struct FocusLost   { WidgetId target; };
struct WindowResize { int width, height; };
struct WindowClose {};

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput,
    FocusGained, FocusLost,
    WindowResize, WindowClose
>;
```

All events are timestamped, ordered, serialisable. Synthetic events can be pushed for testing.

## Data Flow

```
OS / SDL3 / wasm
    │
    │  raw OS events
    ▼
Presenter → mpsc_queue<InputEvent> → Application thread
                                          │
                                          ├→ hit test (point-in-rect on GeometrySnapshot)
                                          ├→ dispatch to widget-scoped signals (on_click, on_hover, ...)
                                          ├→ focus manager → route keyboard to focused widget
                                          └→ global handlers (gesture recognizer, logging, ...)
                                          │
                                          ▼
                                     update model → publish new SceneSnapshot
```

The render thread only touches the input queue to push events from the OS. It never reads from it.

## Hit Testing

Hit testing runs on the application side using the latest `GeometrySnapshot` — a flat list of (widget_id, rect) pairs produced by the layout phase:

```cpp
std::optional<WidgetId> hit_test(const GeometrySnapshot& geo, Point pos) {
    // Walk z_order back-to-front, return first hit
    for (auto i = geo.z_order.rbegin(); i != geo.z_order.rend(); ++i) {
        auto& [id, rect] = geo.geometry[*i];
        if (rect.contains(pos))
            return id;
    }
    return std::nullopt;
}
```

No tree traversal — just a flat scan in z-order. Fast, simple, testable.

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

## Focus Management

Keyboard input routes to the focused widget. Focus is a simple ordered chain of focusable widget IDs:

- Tab / Shift-Tab navigates the chain.
- Click on a focusable widget sets focus.
- The focus chain is a flat list maintained on the app side.

```cpp
struct FocusManager {
    std::vector<WidgetId> focus_chain;
    std::optional<WidgetId> current;

    void next();
    void prev();
    void set(WidgetId id);
};
```

Keyboard events are dispatched to `current` widget's key signal. If the widget doesn't handle the event, it can propagate to the parent (opt-in, not default).

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

## Future: std::execution

When std::execution (P2300) is available, signals become sender factories and connections carry scheduler annotations:

```cpp
auto conn = button.on_click
    | exec::on(app_scheduler)
    | exec::then([&] { model.submit(); });
```

The user-facing API (`on_click`, `connect()`) stays the same. The scheduling becomes explicit in the types rather than implicit.

## Open Questions

- Event bubbling: should unhandled events propagate to parent widgets? If so, opt-in or opt-out?
- Gesture recognition (drag, pinch, long press): built into the input layer or a separate composable layer?
- Input event coalescing: should multiple mouse moves between frames be coalesced into one?
- Touch input: separate event types or unified pointer events?
