# Backend Interface Design

## Goal

Extract the render backend behind a runtime-polymorphic interface so that:

- Most of PRISM stays header-only (widgets, draw list, snapshot, concurrency primitives)
- Backends (software/SDL, Vulkan, WebGPU) are isolated compilation units with heavy deps
- Backends can be switched at runtime (fallback chains, user selection)
- Packaging on Linux/Homebrew works naturally (separate .so per backend)

## Architecture

```
Header-only PRISM (compiled with client app)
├── DrawList, SceneSnapshot, InputEvent, Color, Rect, Point
├── atomic_cell<T>, mpsc_queue<T>
├── Widget concept, Context, Theme
├── Frame (draw command recorder)
├── App (event-driven app loop, takes a Backend)
└── Backend (RAII wrapper, static factories)
         │
         │  C++ vtable boundary
         ▼
BackendBase implementations (separate compilation units)
├── SoftwareBackend (.a or .so, links SDL3)
├── VulkanBackend (.so, links Vulkan)       [future]
└── WebGPUBackend (.so, links wgpu)         [future]
```

### What crosses the boundary

| Direction | Data | Mechanism |
|---|---|---|
| App → Backend | `shared_ptr<const SceneSnapshot>` | `submit()` |
| App → Backend | Wake signal | `wake()` |
| Backend → App | `InputEvent` values | Callback (`std::function`) |
| App → Backend | Shutdown request | `quit()` |

## Interface

### BackendBase (abstract class)

```cpp
// prism/core/backend.hpp

namespace prism {

struct BackendConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

class BackendBase {
public:
    virtual ~BackendBase();  // out-of-line, anchors vtable

    // Run event loop on calling thread (blocking).
    // Creates window, pumps OS events, renders frames.
    // Calls event_cb for each input event.
    // Returns when quit() is called or window is closed.
    virtual void run(std::function<void(const InputEvent&)> event_cb) = 0;

    // Submit a new snapshot for rendering.
    // Called from app thread. Backend holds the shared_ptr
    // until the next submit (or shutdown).
    virtual void submit(std::shared_ptr<const SceneSnapshot> snap) = 0;

    // Wake the event loop after submitting a snapshot.
    // Called from app thread.
    virtual void wake() = 0;

    // Request shutdown. Safe from any thread.
    virtual void quit() = 0;
};

} // namespace prism
```

### Backend (RAII wrapper, header-only)

```cpp
namespace prism {

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(BackendConfig cfg);
    static Backend load(const char* so_path, BackendConfig cfg);

    void run(std::function<void(const InputEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(std::shared_ptr<const SceneSnapshot> s) { impl_->submit(std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }

    // move-only
    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism
```

`Backend::software()` is statically linked (no dlopen). `Backend::load()` uses `dlopen` + `dlsym` on an `extern "C"` factory.

### Factory for loadable backends

Each backend .so exports:

```cpp
extern "C" std::unique_ptr<prism::BackendBase>
prism_backend_create(const prism::BackendConfig& config);
```

`extern "C"` for symbol name stability. C++ types in the signature are fine — same compiler is required for a C++26 project.

## SoftwareBackend

The current `RenderLoop` becomes `SoftwareBackend : BackendBase`. It internalises:

- SDL_Init, window creation, SDL_WaitEvent loop
- SoftwareRenderer + PixelBuffer for rasterisation
- SDL surface blit for presentation
- Event mapping (SDL events → InputEvent)

```cpp
// prism/backends/software_backend.hpp

namespace prism {

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(BackendConfig cfg);

    void run(std::function<void(const InputEvent&)> event_cb) override;
    void submit(std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;

private:
    BackendConfig config_;
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> running_{true};
    atomic_cell<SceneSnapshot> snapshot_cell_;  // internal, receives from submit()
};
```

### Snapshot handoff

`submit()` stores into the backend's internal `atomic_cell`. The event loop loads the latest after handling events — same pattern as today but the atomic_cell is now inside the backend rather than shared via reference.

### Event callback

`run()` receives a `std::function<void(const InputEvent&)>` and calls it synchronously from the event loop for each mapped event. The `App` side provides a callback that pushes into `mpsc_queue` and sets `input_pending_` — same as today but the callback wiring replaces the direct reference passing.

## Changes to App

`App` no longer owns `atomic_cell` or `mpsc_queue` references passed to the render loop. Instead:

```cpp
class App {
public:
    explicit App(BackendConfig config);
    explicit App(Backend backend);  // user-provided backend

    void quit();
    void run(std::function<void(Frame&)> on_frame);

private:
    Backend backend_;
    mpsc_queue<InputEvent> input_queue_;   // owned here, fed by backend callback
    std::atomic<bool> running_{true};
    std::atomic<bool> input_pending_{false};
};
```

`App::run()`:

1. Spawns backend on a thread: `backend_.run([this](auto& ev) { input_queue_.push(ev); input_pending_.store(true); input_pending_.notify_one(); })`
2. Produces initial frame, calls `backend_.submit(snap)` + `backend_.wake()`
3. Event loop: `input_pending_.wait()`, drain queue, call `on_frame`, submit + wake
4. On shutdown: `backend_.quit()`, join thread

## Unchanged types

These are unaffected:

- `DrawList`, `DrawCmd`, all command structs
- `SceneSnapshot`, `WidgetId`
- `InputEvent` and all event structs
- `atomic_cell<T>`, `mpsc_queue<T>`
- `Frame` (draw command recorder)
- `SoftwareRenderer`, `PixelBuffer` (headless, used internally by SoftwareBackend and directly in tests)
- `Widget` concept, `Context`, `Theme`

## Build system

### Current

```
prism_lib = library('prism', 'prism.cpp', dependencies: [sdl3_dep])
```

Everything links SDL3 because App includes RenderLoop.

### After

```
# Core: header-only, no SDL dependency
prism_core_dep = declare_dependency(
    include_directories: prism_inc,
)

# Software backend: links SDL3
prism_software_backend_lib = library('prism-software-backend',
    'backends/software_backend.cpp',
    dependencies: [prism_core_dep, sdl3_dep],
)

prism_software_backend_dep = declare_dependency(
    link_with: prism_software_backend_lib,
    dependencies: [prism_core_dep],
)

# Convenience: core + default software backend
prism_dep = declare_dependency(
    dependencies: [prism_core_dep, prism_software_backend_dep],
)
```

Users who only need headless testing link `prism_core_dep` (no SDL). Users who want a window link `prism_dep` (includes software backend). Future GPU backends are additional optional dependencies.

### Packaging (Linux distro / Homebrew)

| Package | Contents |
|---|---|
| `libprism-dev` | Headers (header-only core) + `BackendBase` out-of-line dtor (.a) |
| `libprism-software` | `SoftwareBackend` .so (links SDL3) |
| `libprism-vulkan` | `VulkanBackend` .so (links Vulkan) [future] |

Same-compiler ABI is guaranteed within a distro build. `BackendBase::~BackendBase()` is out-of-line to anchor the vtable in a single .so. Soname versioning on vtable-breaking changes.

## File layout after refactor

```
include/prism/
    core/
        backend.hpp          # BackendBase + Backend (NEW)
        draw_list.hpp        # unchanged
        scene_snapshot.hpp   # unchanged
        input_event.hpp      # unchanged
        atomic_cell.hpp      # unchanged
        mpsc_queue.hpp       # unchanged
        pixel_buffer.hpp     # unchanged
        software_renderer.hpp # unchanged
        app.hpp              # modified (takes Backend)
        context.hpp          # unchanged
        prism.hpp            # updated includes
    backends/
        software_backend.hpp # NEW (replaces render_loop.hpp)
src/
    backend.cpp              # BackendBase::~BackendBase() = default
    backends/
        software_backend.cpp # SoftwareBackend implementation
tests/
    test_software_renderer.cpp  # unchanged (headless)
    test_pixel_buffer.cpp       # unchanged (headless)
    test_app.cpp                # updated to use Backend
    ...
```

`render_loop.hpp` is deleted — its logic moves into `SoftwareBackend`.

## Migration path

1. Add `BackendBase` + `Backend` (new files)
2. Create `SoftwareBackend` by moving `RenderLoop` logic
3. Refactor `App` to take `Backend`
4. Update build system (split core / backend deps)
5. Update tests and example
6. Delete `render_loop.hpp`

All existing tests for headless types (`SoftwareRenderer`, `PixelBuffer`, `DrawList`, etc.) pass unchanged. `test_app.cpp` needs minor updates to construct `App` with a backend.
