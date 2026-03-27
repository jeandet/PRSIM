# Backend Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the render backend behind a runtime-polymorphic `BackendBase` vtable so backends can be switched at runtime and compiled separately from the header-only core.

**Architecture:** `BackendBase` is an abstract class with 4 methods (`run`, `submit`, `wake`, `quit`). `Backend` is a move-only RAII wrapper with static factories. The current `RenderLoop` becomes `SoftwareBackend : BackendBase`. `App` takes a `Backend` instead of directly creating a `RenderLoop`. The core library becomes header-only (no SDL dependency); SDL is only linked by the software backend.

**Tech Stack:** C++26, Meson, SDL3, doctest

**Spec:** `docs/superpowers/specs/2026-03-27-backend-interface-design.md`

---

## File Structure

| Action | File | Responsibility |
|---|---|---|
| Create | `include/prism/core/backend.hpp` | `BackendBase` abstract class + `BackendConfig` struct |
| Create | `include/prism/backends/software_backend.hpp` | `SoftwareBackend` class declaration |
| Create | `src/backend.cpp` | `BackendBase::~BackendBase()` out-of-line definition (vtable anchor) |
| Create | `src/backends/software_backend.cpp` | `SoftwareBackend` implementation + `Backend::software()` factory |
| Modify | `include/prism/core/app.hpp` | Remove RenderLoop dependency, take `Backend`, own `mpsc_queue`/atomics |
| Modify | `include/prism/prism.hpp` | Replace `render_loop.hpp` include with `backend.hpp` + `software_backend.hpp` |
| Modify | `src/meson.build` | Split into core dep (header-only) + software backend lib |
| Modify | `meson.build` | Move `sdl3_dep` into `src/meson.build` (only backend needs it) |
| Modify | `tests/test_app.cpp` | Update to use new `App` constructor |
| Modify | `tests/meson.build` | Headless tests use `prism_core_dep`, App test uses `prism_dep` |
| Modify | `examples/hello_rect.cpp` | No changes needed (uses `prism::App` + `prism::Frame`, API unchanged) |
| Delete | `include/prism/core/render_loop.hpp` | Logic moved to `SoftwareBackend` |
| Delete | `src/prism.cpp` | No longer needed (core is header-only, backend has its own .cpp) |

---

### Task 1: Add BackendBase and BackendConfig

**Files:**
- Create: `include/prism/core/backend.hpp`
- Create: `src/backend.cpp`

- [ ] **Step 1: Create `include/prism/core/backend.hpp`**

```cpp
#pragma once

#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <functional>
#include <memory>

namespace prism {

struct BackendConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

class BackendBase {
public:
    virtual ~BackendBase();

    virtual void run(std::function<void(const InputEvent&)> event_cb) = 0;
    virtual void submit(std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;

    // Block until the backend is ready to receive submit()/wake() calls.
    // Called from app thread after spawning the backend thread.
    // Default implementation is a no-op (backend ready immediately).
    virtual void wait_ready() {}
};

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(BackendConfig cfg);

    void run(std::function<void(const InputEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(std::shared_ptr<const SceneSnapshot> s) { impl_->submit(std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }
    void wait_ready() { impl_->wait_ready(); }

    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism
```

- [ ] **Step 2: Create `src/backend.cpp`**

```cpp
#include <prism/core/backend.hpp>

namespace prism {

BackendBase::~BackendBase() = default;

} // namespace prism
```

- [ ] **Step 3: Verify it compiles**

Run: `cd builddir && meson compile`
Expected: compilation succeeds (files exist but aren't wired into build yet — this is just a syntax check via include from a later step). Skip if no builddir yet; the build will be wired in Task 4.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/backend.hpp src/backend.cpp
git commit -m "feat: add BackendBase abstract class and Backend wrapper"
```

---

### Task 2: Create SoftwareBackend from RenderLoop

**Files:**
- Create: `include/prism/backends/software_backend.hpp`
- Create: `src/backends/software_backend.cpp`
- Reference: `include/prism/core/render_loop.hpp` (source of logic to move)

- [ ] **Step 1: Create `include/prism/backends/software_backend.hpp`**

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/software_renderer.hpp>

#include <atomic>

struct SDL_Window;

namespace prism {

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(BackendConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    void run(std::function<void(const InputEvent&)> event_cb) override;
    void submit(std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;

private:
    BackendConfig config_;
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot_;

    void present();
};

} // namespace prism
```

- [ ] **Step 2: Create `src/backends/software_backend.cpp`**

This is the RenderLoop logic moved into the BackendBase interface. The key differences from `render_loop.hpp`:
- `run()` takes an `event_cb` callback instead of references to shared atomics
- Snapshot comes via `submit()` into an internal atomic, not a shared `atomic_cell` reference
- `wake()` pushes `SDL_EVENT_USER` (was `App::wake_render_thread()`)
- `quit()` sets `running_` to false and wakes the event loop

```cpp
#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <cstring>

namespace prism {

SoftwareBackend::SoftwareBackend(BackendConfig cfg)
    : config_(cfg)
    , renderer_(cfg.width, cfg.height)
{}

SoftwareBackend::~SoftwareBackend() {
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SoftwareBackend::run(std::function<void(const InputEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(config_.title, config_.width, config_.height, 0);
    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        // Process all pending events
        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer_.resize(ev.window.data1, ev.window.data2);
                event_cb(WindowResize{ev.window.data1, ev.window.data2});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event_cb(MouseMove{{ev.motion.x, ev.motion.y}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, true});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, false});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event_cb(MouseScroll{
                    {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
                break;
            case SDL_EVENT_USER:
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (!running_.load(std::memory_order_relaxed)) break;

        auto snap = snapshot_.load(std::memory_order_acquire);
        if (snap) {
            renderer_.render_frame(*snap);
            present();
        }
    }
}

void SoftwareBackend::submit(std::shared_ptr<const SceneSnapshot> snap) {
    snapshot_.store(std::move(snap), std::memory_order_release);
}

void SoftwareBackend::wake() {
    SDL_Event wake_ev{};
    wake_ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake_ev);
}

void SoftwareBackend::wait_ready() {
    ready_.wait(false, std::memory_order_acquire);
}

void SoftwareBackend::quit() {
    running_.store(false, std::memory_order_relaxed);
    wake();
}

void SoftwareBackend::present() {
    SDL_Surface* surface = SDL_GetWindowSurface(window_);
    if (!surface) return;

    auto& buf = renderer_.buffer();
    if (surface->w != buf.width() || surface->h != buf.height()) {
        renderer_.resize(surface->w, surface->h);
        return;
    }

    SDL_LockSurface(surface);
    auto* dst = static_cast<uint8_t*>(surface->pixels);
    auto* src = reinterpret_cast<const uint8_t*>(buf.data());
    for (int y = 0; y < buf.height(); ++y) {
        std::memcpy(dst + y * surface->pitch, src + y * buf.pitch(), buf.pitch());
    }
    SDL_UnlockSurface(surface);
    SDL_UpdateWindowSurface(window_);
}

Backend Backend::software(BackendConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
```

- [ ] **Step 3: Commit**

```bash
git add include/prism/backends/software_backend.hpp src/backends/software_backend.cpp
git commit -m "feat: add SoftwareBackend implementing BackendBase"
```

---

### Task 3: Refactor App to use Backend

**Files:**
- Modify: `include/prism/core/app.hpp`

- [ ] **Step 1: Rewrite `include/prism/core/app.hpp`**

Key changes from the current version:
- No `#include <prism/core/render_loop.hpp>`, no `#include <SDL3/SDL.h>`
- `App` takes a `Backend` (or constructs default software one from `BackendConfig`)
- `App` owns `mpsc_queue<InputEvent>` and `input_pending_` atomic (same as before)
- No more `atomic_cell` in App — the backend owns snapshot storage
- No more `sdl_ready_` — the backend handles its own init
- No more `wake_render_thread()` calling `SDL_PushEvent` — uses `backend_.wake()`
- The backend thread callback pushes events into `input_queue_` and notifies

Replace the full content of `include/prism/core/app.hpp` with:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace prism {

class Frame {
public:
    void filled_rect(Rect r, Color c) { dl_.filled_rect(r, c); }
    void rect_outline(Rect r, Color c, float thickness = 1.0f) { dl_.rect_outline(r, c, thickness); }
    void text(std::string s, Point origin, float size, Color c) { dl_.text(std::move(s), origin, size, c); }
    void clip_push(Rect r) { dl_.clip_push(r); }
    void clip_pop() { dl_.clip_pop(); }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    friend class App;
    DrawList dl_;
    int width_ = 0;
    int height_ = 0;

    void reset(int w, int h) {
        dl_.clear();
        width_ = w;
        height_ = h;
    }

    std::shared_ptr<const SceneSnapshot> take_snapshot(uint64_t version) {
        auto snap = std::make_shared<SceneSnapshot>();
        snap->version = version;
        if (!dl_.empty()) {
            snap->geometry.push_back({0, {0, 0, static_cast<float>(width_), static_cast<float>(height_)}});
            snap->draw_lists.push_back(std::move(dl_));
            snap->z_order.push_back(0);
        }
        dl_.clear();
        return snap;
    }
};

class App {
public:
    explicit App(BackendConfig config)
        : backend_(Backend::software(config)), config_(config) {}

    explicit App(Backend backend, BackendConfig config = {})
        : backend_(std::move(backend)), config_(config) {}

    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() {
        running_.store(false, std::memory_order_relaxed);
        input_pending_.store(true, std::memory_order_release);
        input_pending_.notify_one();
        backend_.quit();
    }

    void run(std::function<void(Frame&)> on_frame) {
        running_.store(true, std::memory_order_relaxed);

        std::thread backend_thread([this] {
            backend_.run([this](const InputEvent& ev) {
                input_queue_.push(ev);
                input_pending_.store(true, std::memory_order_release);
                input_pending_.notify_one();
            });
        });

        backend_.wait_ready();

        Frame frame;
        uint64_t version = 0;
        int w = config_.width;
        int h = config_.height;

        frame.reset(w, h);
        on_frame(frame);
        backend_.submit(frame.take_snapshot(++version));
        backend_.wake();

        while (running_.load(std::memory_order_relaxed)) {
            input_pending_.wait(false, std::memory_order_acquire);
            input_pending_.store(false, std::memory_order_relaxed);

            while (auto ev = input_queue_.pop()) {
                if (std::holds_alternative<WindowClose>(*ev)) {
                    running_.store(false, std::memory_order_relaxed);
                    break;
                }
                if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                    w = resize->width;
                    h = resize->height;
                }
            }

            if (!running_.load(std::memory_order_relaxed)) break;

            frame.reset(w, h);
            on_frame(frame);
            backend_.submit(frame.take_snapshot(++version));
            backend_.wake();
        }

        backend_.quit();
        backend_thread.join();
    }

private:
    Backend backend_;
    BackendConfig config_;
    mpsc_queue<InputEvent> input_queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> input_pending_{false};
};

} // namespace prism
```

- [ ] **Step 2: Commit**

```bash
git add include/prism/core/app.hpp
git commit -m "refactor: App takes Backend instead of creating RenderLoop"
```

---

### Task 4: Update build system

**Files:**
- Modify: `meson.build`
- Modify: `src/meson.build`
- Modify: `tests/meson.build`
- Modify: `examples/meson.build`

- [ ] **Step 1: Update root `meson.build`**

Move `sdl3_dep` out of root — it's only needed by the backend. Replace full content:

```meson
project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

prism_inc = include_directories('include')

subdir('src')
subdir('tests')
subdir('examples')
```

- [ ] **Step 2: Update `src/meson.build`**

Split into header-only core + software backend library:

```meson
# Core: header-only, no external dependencies
prism_core_dep = declare_dependency(
  include_directories : prism_inc,
)

# Software backend: links SDL3
sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])

prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  dependencies : [prism_core_dep, sdl3_dep],
  install : true,
)

prism_software_backend_dep = declare_dependency(
  link_with : prism_software_backend_lib,
  dependencies : [prism_core_dep],
)

# Convenience: core + default software backend
prism_dep = declare_dependency(
  dependencies : [prism_core_dep, prism_software_backend_dep],
)
```

- [ ] **Step 3: Update `tests/meson.build`**

Headless tests (no SDL) use `prism_core_dep`. The app test uses `prism_dep` (needs backend):

```meson
doctest_dep = dependency('doctest', fallback : ['doctest', 'doctest_dep'])

# Headless tests — no SDL, no backend
headless_tests = {
  'mpsc_queue' : files('test_mpsc_queue.cpp'),
  'atomic_cell' : files('test_atomic_cell.cpp'),
  'draw_list' : files('test_draw_list.cpp'),
  'input_event' : files('test_input_event.cpp'),
  'pixel_buffer' : files('test_pixel_buffer.cpp'),
  'software_renderer' : files('test_software_renderer.cpp'),
}

foreach name, src : headless_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_core_dep, doctest_dep],
  )
  test(name, exe)
endforeach

# Tests that need a backend (SDL)
backend_tests = {
  'app' : files('test_app.cpp'),
}

foreach name, src : backend_tests
  exe = executable('test_' + name, src,
    dependencies : [prism_dep, doctest_dep],
  )
  test(name, exe)
endforeach
```

- [ ] **Step 4: Update `examples/meson.build`**

No content change needed — `prism_dep` still exists and includes the software backend. The file stays:

```meson
executable('hello_rect', 'hello_rect.cpp',
  dependencies : [prism_dep],
)
```

- [ ] **Step 5: Commit**

```bash
git add meson.build src/meson.build tests/meson.build examples/meson.build
git commit -m "build: split core (header-only) from software backend"
```

---

### Task 5: Update public header and delete RenderLoop

**Files:**
- Modify: `include/prism/prism.hpp`
- Delete: `include/prism/core/render_loop.hpp`
- Delete: `src/prism.cpp`

- [ ] **Step 1: Update `include/prism/prism.hpp`**

Replace full content:

```cpp
#pragma once

#include <prism/core/app.hpp>
#include <prism/core/atomic_cell.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/context.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/pixel_buffer.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/software_renderer.hpp>
```

- [ ] **Step 2: Delete `include/prism/core/render_loop.hpp`**

```bash
git rm include/prism/core/render_loop.hpp
```

- [ ] **Step 3: Delete `src/prism.cpp`**

```bash
git rm src/prism.cpp
```

- [ ] **Step 4: Commit**

```bash
git add include/prism/prism.hpp
git commit -m "refactor: remove RenderLoop, update public header"
```

---

### Task 6: Update test_app.cpp

**Files:**
- Modify: `tests/test_app.cpp`

- [ ] **Step 1: Check current test_app.cpp still compiles**

The existing tests use `prism::App({.title = ..., .width = ..., .height = ...})` which now constructs via `App(BackendConfig)` — the field names match (`BackendConfig` has the same `title`, `width`, `height` fields as the old `AppConfig`). The API for `Frame` and `app.quit()` is unchanged.

Update the include to use the new public header (no more `render_loop.hpp` transitive include):

Replace full content of `tests/test_app.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/core/app.hpp>

TEST_CASE("App runs and stops on quit") {
    int frame_count = 0;

    prism::App app({.title = "Test", .width = 100, .height = 100});
    app.run([&](prism::Frame&) {
        ++frame_count;
        app.quit();
    });

    CHECK(frame_count == 1);
}

TEST_CASE("Frame exposes width and height") {
    prism::App app({.title = "Test", .width = 320, .height = 240});
    app.run([&](prism::Frame& frame) {
        CHECK(frame.width() == 320);
        CHECK(frame.height() == 240);
        app.quit();
    });
}

TEST_CASE("Frame records draw commands into snapshot") {
    prism::App app({.title = "Test", .width = 100, .height = 100});
    app.run([&](prism::Frame& frame) {
        frame.filled_rect({10, 10, 50, 50}, prism::Color::rgba(255, 0, 0));
        app.quit();
    });
    CHECK(true);
}
```

- [ ] **Step 2: Commit**

```bash
git add tests/test_app.cpp
git commit -m "test: update test_app for new Backend-based App"
```

---

### Task 7: Build and run all tests

**Files:** None (verification only)

- [ ] **Step 1: Create build directory and configure**

```bash
rm -rf builddir && meson setup builddir
```

Expected: configuration succeeds, SDL3 fetched via wrap.

- [ ] **Step 2: Build**

```bash
meson compile -C builddir
```

Expected: all targets compile — `libprism-software-backend`, tests, `hello_rect`.

- [ ] **Step 3: Run tests**

```bash
meson test -C builddir
```

Expected: all 7 test suites pass (mpsc_queue, atomic_cell, draw_list, input_event, pixel_buffer, software_renderer, app).

- [ ] **Step 4: Run hello_rect manually to verify it works**

```bash
./builddir/examples/hello_rect
```

Expected: window appears with dark background and colored rectangles. Close window to exit.

- [ ] **Step 5: Commit (if any fixes were needed)**

If any fixes were required in earlier tasks, commit them here.

---

### Task 8: Update design doc

**Files:**
- Modify: `doc/design/render-backend.md`

- [ ] **Step 1: Update `doc/design/render-backend.md`**

Add a section at the top reflecting the new backend interface. The existing content about SoftwareRenderer/PixelBuffer internals remains valid. Add after the "## Interface" section:

Add to the top after the overview, replacing the old concept-based interface:

```markdown
## Interface

`BackendBase` is an abstract class — runtime-polymorphic so backends can be loaded or switched at runtime:

\```cpp
class BackendBase {
public:
    virtual ~BackendBase();
    virtual void run(std::function<void(const InputEvent&)> event_cb) = 0;
    virtual void submit(std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
};
\```

`Backend` is a move-only RAII wrapper with static factories:
- `Backend::software(cfg)` — built-in, statically linked
- `Backend::load(path, cfg)` — dlopen a backend .so (future)

The backend owns: window, event loop, rendering, presentation. Communication:
- App → Backend: `submit()` + `wake()`
- Backend → App: `event_cb` callback with `InputEvent` values
```

- [ ] **Step 2: Commit**

```bash
git add doc/design/render-backend.md
git commit -m "docs: update render-backend design for BackendBase interface"
```
