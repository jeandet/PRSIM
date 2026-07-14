# Window Abstraction Design

## Goal

Introduce a proper `Window` abstraction to PRISM, replacing the implicit single-window model where `BackendConfig` mixes window and rendering concerns and `SDL_CreateWindow` is called directly inside the backend. The design supports native, custom-drawn, and no decorations, and is structured for multi-window support in a future phase.

## Core Types

### WindowId, WindowConfig, RenderConfig

```cpp
// prism/core/window.hpp

using WindowId = uint32_t;

enum class DecorationMode { Native, Custom, None };

struct WindowConfig {
    const char* title      = "PRISM";
    int         width      = 800;
    int         height     = 600;
    bool        resizable  = true;
    bool        fullscreen = false;
    DecorationMode decoration = DecorationMode::Native;
};

struct RenderConfig {
    const char* font_path = nullptr;
};
```

`BackendConfig` is removed. `WindowConfig` holds per-window creation parameters. `RenderConfig` holds backend-wide rendering resources (fonts).

### Window Interface

```cpp
class Window {
public:
    virtual ~Window() = default;

    virtual WindowId id() const = 0;

    // Properties
    virtual void set_title(std::string_view title) = 0;
    virtual void set_size(int w, int h) = 0;
    virtual std::pair<int,int> size() const = 0;
    virtual void set_position(int x, int y) = 0;
    virtual std::pair<int,int> position() const = 0;

    // Decoration
    virtual void set_decoration_mode(DecorationMode mode) = 0;
    virtual DecorationMode decoration_mode() const = 0;

    // State
    virtual void set_resizable(bool r) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_fullscreen(bool f) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

    // Lifecycle
    virtual void close() = 0;
};
```

Defined in core, no backend dependencies. Backends provide concrete implementations.

### WindowEvent

```cpp
struct WindowEvent {
    WindowId window;
    InputEvent event;
};
```

Wrapper around existing `InputEvent`. The backend produces `WindowEvent`s, `model_app` routes by `WindowId`. Existing `InputEvent` types are unchanged.

## BackendBase Changes

```cpp
class BackendBase {
public:
    virtual ~BackendBase();
    virtual Window& create_window(WindowConfig cfg) = 0;
    virtual void run(std::function<void(const WindowEvent&)> event_cb) = 0;
    virtual void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
    virtual void wait_ready() {}
};
```

Changes from current interface:
- `create_window(WindowConfig)` replaces implicit window creation in `run()`
- `run()` callback delivers `WindowEvent` instead of `InputEvent`
- `submit()` takes a `WindowId` to target a specific window

The `Backend` wrapper class mirrors these changes.

## SDL Backend

### SdlWindow

```cpp
// prism/backends/sdl_window.hpp

class SdlWindow final : public Window {
    WindowId id_;
    SDL_Window* sdl_window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    DecorationMode decoration_;

public:
    SdlWindow(WindowId id, WindowConfig cfg);
    ~SdlWindow();

    // Window interface implementation — forwards to SDL_SetWindowTitle, etc.

    // Backend-internal access
    SDL_Renderer* renderer() { return renderer_; }
    SDL_Window* sdl_window() { return sdl_window_; }
};
```

Window creation by decoration mode:
- **Native**: `SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE)` — OS-provided decorations
- **Custom**: `SDL_CreateWindow(title, w, h, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE)` + SDL hit-test callback for drag/resize
- **None**: `SDL_WINDOW_BORDERLESS` without hit-test

`set_decoration_mode()` at runtime recreates the SDL window and renderer with updated flags (SDL doesn't support toggling borderless on all platforms). The scene snapshot is re-submitted after recreation so no visual discontinuity occurs.

### SdlBackend

Renamed from `SoftwareBackend`. Owns a map of windows:

```cpp
class SdlBackend final : public BackendBase {
    RenderConfig render_config_;
    std::unordered_map<WindowId, std::unique_ptr<SdlWindow>> windows_;
    uint32_t next_id_ = 1;
    TTF_Font* font_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};

    Window& create_window(WindowConfig cfg) override;
    void run(std::function<void(const WindowEvent&)> event_cb) override;
    void submit(WindowId id, std::shared_ptr<const SceneSnapshot> snap) override;
    // ...
};
```

The event loop maps `SDL_GetWindowID()` to `WindowId` when constructing `WindowEvent`s. Font and other rendering resources are shared across windows.

Factory: `Backend::software(RenderConfig)` replaces `Backend::software(BackendConfig)`.

## Custom Decorations — Built-in Chrome

When `DecorationMode::Custom` is active, PRISM draws a default title bar and the backend handles drag/resize via OS-native hit-testing.

### Hit-test geometry (core)

```cpp
// prism/core/window_chrome.hpp

struct WindowChrome {
    static constexpr float title_bar_h = 32.f;
    static constexpr float resize_edge = 6.f;

    enum class HitZone {
        Client,
        TitleBar,
        Close, Minimize, Maximize,
        ResizeN, ResizeS, ResizeE, ResizeW,
        ResizeNE, ResizeNW, ResizeSE, ResizeSW
    };

    static HitZone hit_test(int x, int y, int window_w, int window_h);
};
```

The hit-test function is pure geometry — no backend dependencies. It checks resize edges first (6px border), then button zones in the title bar, then the title bar body (draggable), then client area.

### SDL integration

The SDL backend registers an `SDL_SetWindowHitTest` callback that:
1. Calls `WindowChrome::hit_test()` to get the zone
2. Maps `HitZone` to SDL constants (`SDL_HITTEST_DRAGGABLE`, `SDL_HITTEST_RESIZE_TOPLEFT`, etc.)

SDL handles the actual OS-level drag/resize interaction — native feel, no flickering.

### Rendering

The widget tree reserves `title_bar_h` pixels at the top when the window has `DecorationMode::Custom`. A built-in delegate draws:
- Background bar
- Window title text (centered or left-aligned)
- Close / minimize / maximize buttons (right-aligned)

Content area starts below the chrome.

## Headless and Test Backends

### HeadlessWindow

In-memory implementation of `Window` that stores all values as fields. No OS window. Used by test and null backends.

```cpp
class HeadlessWindow final : public Window {
    WindowId id_;
    std::string title_;
    int w_, h_, x_ = 0, y_ = 0;
    bool resizable_, fullscreen_ = false;
    DecorationMode decoration_;
    bool visible_ = true;
    // All methods read/write these fields
};
```

### TestBackend / NullBackend

Adapted to new interface:
- `create_window()` creates and returns a `HeadlessWindow`
- `run()` delivers `WindowEvent` (wrapping events with window ID)
- `submit()` takes `WindowId` (ignored in test backends)

## model_app Integration

```cpp
// Convenience overload (single window)
template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window({.title = title.data()});
    model_app(backend, window, model, std::move(setup));
}
```

`AppContext` gains a `Window&` reference so user code can modify the window at runtime:

```cpp
model_app("Dashboard", dashboard, [&](AppContext& ctx) {
    ctx.window().set_title("Dashboard - " + username);
    ctx.window().set_fullscreen(true);
});
```

Event routing unwraps `WindowEvent`, dispatches by `WindowId` to the associated widget tree.

## Migration Order

1. Add new types: `WindowConfig`, `RenderConfig`, `DecorationMode`, `WindowId`, `WindowEvent`, `Window` interface
2. Implement `HeadlessWindow`
3. Update `BackendBase` interface (new signatures)
4. Implement `SdlWindow`, refactor `SoftwareBackend` → `SdlBackend` with window map
5. Update `model_app` to route `WindowEvent`, create windows through backend, expose `Window&` via `AppContext`
6. Update `TestBackend` / `NullBackend` to use `HeadlessWindow` and `WindowEvent`
7. Fix tests (wrap `InputEvent` in `WindowEvent{1, ...}`)
8. Add `WindowChrome` (hit-test geometry + SDL callback + title bar rendering) — independent follow-up

Steps 1-7 are one atomic change. Step 8 is a separate addition.
