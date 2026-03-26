# App Facade

## Overview

`prism::App` is the zero-boilerplate entry point. It owns all infrastructure (render thread, SDL window, snapshot cell, input queue) and exposes a single `run()` method that takes a user-provided callable.

## Minimal Example

```cpp
int main() {
    prism::App app({.title = "Hello", .width = 800, .height = 600});

    app.run([](prism::Frame& frame) {
        frame.filled_rect({10, 10, 200, 100}, prism::Color::rgba(0, 120, 215));
    });
}
```

The callable receives a `Frame` — a thin wrapper that collects draw commands and builds a `SceneSnapshot` behind the scenes. `run()` blocks until the window is closed.

## App

```cpp
struct AppConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

class App {
public:
    explicit App(AppConfig config);
    ~App() = default;

    void quit();
    void run(std::function<void(Frame&)> on_frame);
};
```

`App` internally:
1. Spawns the render thread (initialises SDL, creates window, enters event-driven frame loop).
2. Waits for SDL to be ready (`sdl_ready_` atomic handshake).
3. Produces an initial frame by calling `on_frame`, publishes the snapshot.
4. Enters event-driven loop: blocks on `input_pending_.wait()` until input arrives, drains input queue, calls `on_frame`, publishes snapshot, wakes render thread via `SDL_PushEvent(USER)`.
5. On window close or `quit()`, sets `running_ = false`, wakes both threads, joins render thread.

## Frame

`Frame` is the user-facing drawing surface. It wraps a `DrawList` and provides the same drawing methods, plus viewport dimensions.

```cpp
class Frame {
public:
    void filled_rect(Rect r, Color c);
    void rect_outline(Rect r, Color c, float thickness = 1.0f);
    void text(std::string s, Point origin, float size, Color c);
    void clip_push(Rect r);
    void clip_pop();

    int width() const;
    int height() const;
};
```

`Frame` is not user-constructible — it is created by `App` and passed to the callable. After the callable returns, `App` wraps the draw list into a `SceneSnapshot` and publishes it.

## Event-Driven Architecture

Both threads sleep at OS level when idle (zero CPU):

- **Render thread** blocks on `SDL_WaitEvent` — woken by OS events or `SDL_EVENT_USER` from the app thread.
- **App thread** blocks on `std::atomic::wait` (kernel futex) — woken by `input_pending_.notify_one()` from the render thread.

This replaces the original polling design. No `SDL_Delay`, no `sleep_for`, no busy loops.

## Advanced Usage

For multi-threaded producers or reactive models, the user can bypass `App` and work directly with the raw primitives:

```cpp
prism::atomic_cell<prism::SceneSnapshot> cell;
prism::mpsc_queue<prism::InputEvent> input_queue;
// ... manage threads, build snapshots, publish manually
```

`App` is sugar, not a cage.

## Resolved Questions

- ~~Should `run()` accept a frame-rate hint?~~ **No.** Event-driven model renders only when snapshots change. Vsync or frame pacing can be layered later.
- ~~Should `Frame` expose the input queue for polling?~~ **No.** Input is drained by `App` before calling `on_frame`. The callback sees a clean frame each time.
- ~~Should `App` support multiple windows?~~ **Deferred.** One App = one window for now.

## Open Questions

- Should `on_frame` receive input events as a parameter (e.g. `on_frame(frame, events)`)?
- Should `App` expose a `request_redraw()` for animation-driven repaints without input?
