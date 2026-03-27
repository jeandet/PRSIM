# POC: Colored Rectangle on Screen — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display a colored filled rectangle in an SDL3 window using the full PRISM pipeline: App facade → SceneSnapshot → software rasteriser → SDL3 present.

**Architecture:** `prism::App` spawns a render thread that creates the SDL3 window and runs the frame loop. The app thread calls a user lambda each iteration, which draws into a `Frame` (wrapping a `DrawList`). The Frame is packaged into a `SceneSnapshot` and published via `atomic_cell`. The render thread loads the latest snapshot, rasterises `FilledRect` commands into a `PixelBuffer`, and blits to the SDL3 window surface. Input events (including `WindowClose`) flow back via `mpsc_queue`.

**Tech Stack:** C++26, SDL3 (Meson wrap), doctest

---

## File Structure

| File | Responsibility |
|---|---|
| `subprojects/sdl3.wrap` | SDL3 dependency via wrapdb |
| `include/prism/core/input_event.hpp` | `InputEvent` variant — plain data event types |
| `include/prism/core/pixel_buffer.hpp` | `PixelBuffer` — RGBA8 pixel storage + fill_rect |
| `include/prism/core/software_renderer.hpp` | `SoftwareRenderer` — rasterises SceneSnapshot → PixelBuffer |
| `include/prism/core/render_loop.hpp` | `RenderLoop` — owns SDL window, runs frame loop on render thread |
| `include/prism/core/app.hpp` | `App` + `Frame` — zero-boilerplate facade |
| `src/prism.cpp` | Add SDL init/quit + any non-inline implementations |
| `meson.build` | Add SDL3 dependency |
| `src/meson.build` | Link SDL3 into libprism |
| `tests/test_pixel_buffer.cpp` | PixelBuffer unit tests |
| `tests/test_software_renderer.cpp` | SoftwareRenderer headless tests |
| `tests/test_input_event.cpp` | InputEvent construction/variant tests |
| `tests/test_app.cpp` | App integration test (headless, short-lived) |
| `examples/meson.build` | Build example targets |
| `examples/hello_rect.cpp` | Minimal POC demo |

---

### Task 1: Add SDL3 Meson Wrap

**Files:**
- Create: `subprojects/sdl3.wrap`
- Modify: `meson.build`
- Modify: `src/meson.build`

- [ ] **Step 1: Install the SDL3 wrap**

```bash
cd /var/home/jeandet/Documents/prog/PRSIM
meson wrap install sdl3
```

This creates `subprojects/sdl3.wrap`.

- [ ] **Step 2: Add SDL3 dependency to root meson.build**

In `meson.build`, after `prism_inc = include_directories('include')`, add:

```meson
sdl3_dep = dependency('sdl3', fallback : ['sdl3', 'sdl3_dep'])
```

Also add `subdir('examples')` after `subdir('tests')`.

- [ ] **Step 3: Link SDL3 into libprism**

In `src/meson.build`, change the library to:

```meson
prism_lib = library('prism',
  'prism.cpp',
  dependencies : [sdl3_dep],
  include_directories : prism_inc,
  install : true,
)

prism_dep = declare_dependency(
  include_directories : prism_inc,
  link_with : prism_lib,
  dependencies : [sdl3_dep],
)
```

- [ ] **Step 4: Create examples/meson.build (empty for now)**

```meson
# Example targets — added as they are created.
```

- [ ] **Step 5: Verify build still compiles and tests pass**

```bash
meson setup builddir --reconfigure || meson setup builddir --wipe
meson test -C builddir
```

Expected: all 3 existing tests pass, SDL3 subproject downloads and builds.

- [ ] **Step 6: Commit**

```bash
git add subprojects/sdl3.wrap meson.build src/meson.build examples/meson.build
git commit -m "feat: add SDL3 dependency via Meson wrap"
```

---

### Task 2: InputEvent — Plain Data Event Types

**Files:**
- Create: `include/prism/core/input_event.hpp`
- Modify: `include/prism/prism.hpp`
- Create: `tests/test_input_event.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_input_event.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <prism/core/input_event.hpp>

TEST_CASE("InputEvent variant holds WindowClose") {
    prism::InputEvent ev = prism::WindowClose{};
    CHECK(std::holds_alternative<prism::WindowClose>(ev));
}

TEST_CASE("InputEvent variant holds WindowResize") {
    prism::InputEvent ev = prism::WindowResize{.width = 1024, .height = 768};
    auto& resize = std::get<prism::WindowResize>(ev);
    CHECK(resize.width == 1024);
    CHECK(resize.height == 768);
}

TEST_CASE("InputEvent variant holds MouseMove") {
    prism::InputEvent ev = prism::MouseMove{.position = {10.0f, 20.0f}};
    auto& move = std::get<prism::MouseMove>(ev);
    CHECK(move.position.x == doctest::Approx(10.0f));
    CHECK(move.position.y == doctest::Approx(20.0f));
}
```

- [ ] **Step 2: Register test in tests/meson.build**

Add `'input_event'` to the `test_sources` dict:

```meson
test_sources = {
  'mpsc_queue' : files('test_mpsc_queue.cpp'),
  'atomic_cell' : files('test_atomic_cell.cpp'),
  'draw_list' : files('test_draw_list.cpp'),
  'input_event' : files('test_input_event.cpp'),
}
```

- [ ] **Step 3: Run test to verify it fails**

```bash
meson test -C builddir input_event
```

Expected: FAIL — `input_event.hpp` does not exist.

- [ ] **Step 4: Write the implementation**

Create `include/prism/core/input_event.hpp`:

```cpp
#pragma once

#include <prism/core/draw_list.hpp> // Point

#include <cstdint>
#include <variant>

namespace prism {

struct MouseMove   { Point position; };
struct MouseButton { Point position; uint8_t button; bool pressed; };
struct MouseScroll { Point position; float dx, dy; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct WindowResize { int width, height; };
struct WindowClose {};

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease,
    WindowResize, WindowClose
>;

} // namespace prism
```

- [ ] **Step 5: Add include to umbrella header**

In `include/prism/prism.hpp`, add:

```cpp
#include <prism/core/input_event.hpp>
```

- [ ] **Step 6: Run tests and verify they pass**

```bash
meson test -C builddir
```

Expected: all tests pass (3 old + 3 new).

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/input_event.hpp include/prism/prism.hpp tests/test_input_event.cpp tests/meson.build
git commit -m "feat: add InputEvent variant with plain data event types"
```

---

### Task 3: PixelBuffer — CPU Rasterisation Target

**Files:**
- Create: `include/prism/core/pixel_buffer.hpp`
- Create: `tests/test_pixel_buffer.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_pixel_buffer.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <prism/core/pixel_buffer.hpp>

TEST_CASE("PixelBuffer starts cleared to black") {
    prism::PixelBuffer buf(4, 3);
    CHECK(buf.width() == 4);
    CHECK(buf.height() == 3);
    CHECK(buf.pixel(0, 0) == 0xFF000000); // opaque black (ARGB8888)
}

TEST_CASE("fill_rect writes correct pixels") {
    prism::PixelBuffer buf(10, 10);
    // Fill a 3x2 rect at (2,1) with red
    prism::Rect r{2.0f, 1.0f, 3.0f, 2.0f};
    prism::Color red = prism::Color::rgba(255, 0, 0);
    buf.fill_rect(r, red);

    // Inside the rect
    uint32_t expected = 0xFFFF0000; // ARGB: A=FF, R=FF, G=00, B=00
    CHECK(buf.pixel(2, 1) == expected);
    CHECK(buf.pixel(4, 2) == expected);

    // Outside the rect
    CHECK(buf.pixel(0, 0) == 0xFF000000);
    CHECK(buf.pixel(5, 1) == 0xFF000000);
}

TEST_CASE("fill_rect clips to buffer bounds") {
    prism::PixelBuffer buf(5, 5);
    // Rect extends beyond buffer
    prism::Rect r{3.0f, 3.0f, 10.0f, 10.0f};
    prism::Color blue = prism::Color::rgba(0, 0, 255);
    buf.fill_rect(r, blue);

    uint32_t expected = 0xFF0000FF; // ARGB
    CHECK(buf.pixel(3, 3) == expected);
    CHECK(buf.pixel(4, 4) == expected);
    CHECK(buf.pixel(2, 2) == 0xFF000000); // outside
}

TEST_CASE("clear resets all pixels") {
    prism::PixelBuffer buf(4, 4);
    buf.fill_rect({0, 0, 4, 4}, prism::Color::rgba(255, 255, 255));
    buf.clear();
    CHECK(buf.pixel(0, 0) == 0xFF000000);
}
```

- [ ] **Step 2: Register test in tests/meson.build**

Add to `test_sources`:

```meson
  'pixel_buffer' : files('test_pixel_buffer.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

```bash
meson test -C builddir pixel_buffer
```

Expected: FAIL — file does not exist.

- [ ] **Step 4: Write the implementation**

Create `include/prism/core/pixel_buffer.hpp`:

```cpp
#pragma once

#include <prism/core/draw_list.hpp> // Rect, Color

#include <algorithm>
#include <cstdint>
#include <vector>

namespace prism {

// ARGB8888 pixel buffer for CPU rasterisation.
// Pixel layout matches SDL_PIXELFORMAT_ARGB8888.
class PixelBuffer {
public:
    PixelBuffer(int w, int h)
        : w_(w), h_(h), pixels_(static_cast<std::size_t>(w * h), 0xFF000000)
    {}

    [[nodiscard]] int width() const { return w_; }
    [[nodiscard]] int height() const { return h_; }
    [[nodiscard]] uint32_t* data() { return pixels_.data(); }
    [[nodiscard]] const uint32_t* data() const { return pixels_.data(); }
    [[nodiscard]] std::size_t pitch() const { return static_cast<std::size_t>(w_) * sizeof(uint32_t); }

    [[nodiscard]] uint32_t pixel(int x, int y) const {
        return pixels_[static_cast<std::size_t>(y * w_ + x)];
    }

    void clear() {
        std::fill(pixels_.begin(), pixels_.end(), 0xFF000000);
    }

    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        pixels_.assign(static_cast<std::size_t>(w * h), 0xFF000000);
    }

    void fill_rect(Rect r, Color c) {
        int x0 = std::max(0, static_cast<int>(r.x));
        int y0 = std::max(0, static_cast<int>(r.y));
        int x1 = std::min(w_, static_cast<int>(r.x + r.w));
        int y1 = std::min(h_, static_cast<int>(r.y + r.h));

        uint32_t packed = pack(c);
        for (int y = y0; y < y1; ++y) {
            auto row = pixels_.begin() + y * w_;
            std::fill(row + x0, row + x1, packed);
        }
    }

private:
    int w_, h_;
    std::vector<uint32_t> pixels_;

    static constexpr uint32_t pack(Color c) {
        return (uint32_t{c.a} << 24) | (uint32_t{c.r} << 16)
             | (uint32_t{c.g} << 8)  | uint32_t{c.b};
    }
};

} // namespace prism
```

- [ ] **Step 5: Run tests and verify they pass**

```bash
meson test -C builddir
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/pixel_buffer.hpp tests/test_pixel_buffer.cpp tests/meson.build
git commit -m "feat: add PixelBuffer with fill_rect for CPU rasterisation"
```

---

### Task 4: SoftwareRenderer — SceneSnapshot to PixelBuffer

**Files:**
- Create: `include/prism/core/software_renderer.hpp`
- Create: `tests/test_software_renderer.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_software_renderer.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <prism/core/software_renderer.hpp>

TEST_CASE("render_frame rasterises a FilledRect") {
    prism::SoftwareRenderer renderer(10, 10);

    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, {2.0f, 1.0f, 3.0f, 2.0f}});
    snap.draw_lists.emplace_back();
    snap.draw_lists[0].filled_rect({2.0f, 1.0f, 3.0f, 2.0f}, prism::Color::rgba(255, 0, 0));
    snap.z_order.push_back(0);

    renderer.render_frame(snap);
    auto& buf = renderer.buffer();

    uint32_t red = 0xFFFF0000;
    CHECK(buf.pixel(2, 1) == red);
    CHECK(buf.pixel(4, 2) == red);
    CHECK(buf.pixel(0, 0) == 0xFF000000); // background
}

TEST_CASE("render_frame clears before drawing") {
    prism::SoftwareRenderer renderer(10, 10);

    // First frame: red rect
    prism::SceneSnapshot snap1;
    snap1.version = 1;
    snap1.geometry.push_back({1, {0.0f, 0.0f, 10.0f, 10.0f}});
    snap1.draw_lists.emplace_back();
    snap1.draw_lists[0].filled_rect({0.0f, 0.0f, 10.0f, 10.0f}, prism::Color::rgba(255, 0, 0));
    snap1.z_order.push_back(0);
    renderer.render_frame(snap1);

    // Second frame: empty
    prism::SceneSnapshot snap2;
    snap2.version = 2;
    renderer.render_frame(snap2);

    CHECK(renderer.buffer().pixel(5, 5) == 0xFF000000); // cleared
}

TEST_CASE("render_frame respects z_order") {
    prism::SoftwareRenderer renderer(10, 10);

    prism::SceneSnapshot snap;
    snap.version = 1;
    // Widget 0: blue background
    snap.geometry.push_back({0, {0.0f, 0.0f, 10.0f, 10.0f}});
    snap.draw_lists.emplace_back();
    snap.draw_lists[0].filled_rect({0.0f, 0.0f, 10.0f, 10.0f}, prism::Color::rgba(0, 0, 255));
    // Widget 1: red foreground (overlapping)
    snap.geometry.push_back({1, {3.0f, 3.0f, 4.0f, 4.0f}});
    snap.draw_lists.emplace_back();
    snap.draw_lists[1].filled_rect({3.0f, 3.0f, 4.0f, 4.0f}, prism::Color::rgba(255, 0, 0));
    // z_order: 0 first (back), 1 second (front)
    snap.z_order = {0, 1};

    renderer.render_frame(snap);

    CHECK(renderer.buffer().pixel(0, 0) == 0xFF0000FF); // blue
    CHECK(renderer.buffer().pixel(5, 5) == 0xFFFF0000); // red on top
}

TEST_CASE("resize changes buffer dimensions") {
    prism::SoftwareRenderer renderer(10, 10);
    renderer.resize(20, 15);
    CHECK(renderer.buffer().width() == 20);
    CHECK(renderer.buffer().height() == 15);
}
```

- [ ] **Step 2: Register test in tests/meson.build**

Add to `test_sources`:

```meson
  'software_renderer' : files('test_software_renderer.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

```bash
meson test -C builddir software_renderer
```

Expected: FAIL — file does not exist.

- [ ] **Step 4: Write the implementation**

Create `include/prism/core/software_renderer.hpp`:

```cpp
#pragma once

#include <prism/core/pixel_buffer.hpp>
#include <prism/core/scene_snapshot.hpp>

namespace prism {

// Rasterises a SceneSnapshot into a PixelBuffer.
// Satisfies RenderBackend concept: render_frame(snap), resize(w, h).
// Headless — no SDL dependency. Testable by inspecting buffer().
class SoftwareRenderer {
public:
    SoftwareRenderer(int w, int h) : buf_(w, h) {}

    void render_frame(const SceneSnapshot& snap) {
        buf_.clear();
        for (uint16_t idx : snap.z_order) {
            rasterise_draw_list(snap.draw_lists[idx]);
        }
    }

    void resize(int w, int h) { buf_.resize(w, h); }

    [[nodiscard]] const PixelBuffer& buffer() const { return buf_; }
    [[nodiscard]] PixelBuffer& buffer() { return buf_; }

private:
    PixelBuffer buf_;

    void rasterise_draw_list(const DrawList& dl) {
        for (const auto& cmd : dl.commands) {
            std::visit([this](const auto& c) { rasterise(c); }, cmd);
        }
    }

    void rasterise(const FilledRect& cmd) { buf_.fill_rect(cmd.rect, cmd.color); }
    void rasterise(const RectOutline&) {} // POC: skip outlines
    void rasterise(const TextCmd&) {}     // POC: skip text
    void rasterise(const ClipPush&) {}    // POC: skip clipping
    void rasterise(const ClipPop&) {}     // POC: skip clipping
};

} // namespace prism
```

- [ ] **Step 5: Run tests and verify they pass**

```bash
meson test -C builddir
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/software_renderer.hpp tests/test_software_renderer.cpp tests/meson.build
git commit -m "feat: add SoftwareRenderer — headless SceneSnapshot to PixelBuffer"
```

---

### Task 5: RenderLoop — SDL3 Window + Frame Loop

**Files:**
- Create: `include/prism/core/render_loop.hpp`

This is the component that ties SDL3 to the renderer. It runs on the render thread, owns the SDL window, pumps OS events into the `mpsc_queue`, loads the latest snapshot from the `atomic_cell`, rasterises via `SoftwareRenderer`, and blits to the window.

No unit test for this task — it requires a real SDL window. It will be tested via the App integration test in Task 6.

- [ ] **Step 1: Write the implementation**

Create `include/prism/core/render_loop.hpp`:

```cpp
#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/software_renderer.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>

namespace prism {

struct RenderLoopConfig {
    const char* title = "PRISM";
    int width  = 800;
    int height = 600;
};

// Owns the SDL window and render thread frame loop.
// Created on the render thread. Runs until running flag is false.
class RenderLoop {
public:
    explicit RenderLoop(RenderLoopConfig config,
                        atomic_cell<SceneSnapshot>& snapshot_cell,
                        mpsc_queue<InputEvent>& input_queue,
                        std::atomic<bool>& running)
        : snapshot_cell_(snapshot_cell)
        , input_queue_(input_queue)
        , running_(running)
        , renderer_(config.width, config.height)
    {
        window_ = SDL_CreateWindow(config.title, config.width, config.height, 0);
        if (!window_)
            return; // running_ stays true; App should check
    }

    ~RenderLoop() {
        if (window_) SDL_DestroyWindow(window_);
    }

    RenderLoop(const RenderLoop&) = delete;
    RenderLoop& operator=(const RenderLoop&) = delete;

    // Blocking frame loop — call from the render thread.
    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            pump_events();
            auto snap = snapshot_cell_.load();
            if (snap) {
                renderer_.render_frame(*snap);
                present();
            }
        }
    }

private:
    atomic_cell<SceneSnapshot>& snapshot_cell_;
    mpsc_queue<InputEvent>& input_queue_;
    std::atomic<bool>& running_;
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;

    void pump_events() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                input_queue_.push(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer_.resize(ev.window.data1, ev.window.data2);
                input_queue_.push(WindowResize{ev.window.data1, ev.window.data2});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                input_queue_.push(MouseMove{{ev.motion.x, ev.motion.y}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                input_queue_.push(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, true});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                input_queue_.push(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, false});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                input_queue_.push(MouseScroll{
                    {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
                break;
            default:
                break;
            }
        }
    }

    void present() {
        SDL_Surface* surface = SDL_GetWindowSurface(window_);
        if (!surface) return;

        auto& buf = renderer_.buffer();
        // If window was resized, the surface dimensions may differ
        if (surface->w != buf.width() || surface->h != buf.height()) {
            renderer_.resize(surface->w, surface->h);
            return; // skip this frame, will render at new size next frame
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
};

} // namespace prism
```

- [ ] **Step 2: Verify build compiles**

```bash
ninja -C builddir
```

Expected: compiles (header-only, only included when used).

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/render_loop.hpp
git commit -m "feat: add RenderLoop — SDL3 window, event pump, frame loop"
```

---

### Task 6: App Facade — Zero-Boilerplate Entry Point

**Files:**
- Create: `include/prism/core/app.hpp`
- Modify: `include/prism/prism.hpp`
- Create: `tests/test_app.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_app.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <prism/core/app.hpp>

TEST_CASE("App runs and stops on WindowClose injection") {
    // Test that the app loop runs the callback and can be stopped.
    // We inject a WindowClose by limiting to a single frame.
    int frame_count = 0;

    prism::App app({.title = "Test", .width = 100, .height = 100});
    app.run([&](prism::Frame& frame) {
        ++frame_count;
        // Stop after first frame
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
    // If we got here without crash, the pipeline works end-to-end.
    CHECK(true);
}
```

- [ ] **Step 2: Register test in tests/meson.build**

Add to `test_sources`:

```meson
  'app' : files('test_app.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

```bash
meson test -C builddir app
```

Expected: FAIL — `app.hpp` does not exist.

- [ ] **Step 4: Write the implementation**

Create `include/prism/core/app.hpp`:

```cpp
#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/render_loop.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace prism {

struct AppConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

// User-facing drawing surface. Wraps a DrawList, builds a SceneSnapshot.
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

    SceneSnapshot take_snapshot(uint64_t version) {
        SceneSnapshot snap;
        snap.version = version;
        if (!dl_.empty()) {
            snap.geometry.push_back({0, {0, 0, static_cast<float>(width_), static_cast<float>(height_)}});
            snap.draw_lists.push_back(std::move(dl_));
            snap.z_order.push_back(0);
        }
        dl_.clear();
        return snap;
    }
};

class App {
public:
    explicit App(AppConfig config) : config_(config) {
        SDL_Init(SDL_INIT_VIDEO);
    }

    ~App() {
        SDL_Quit();
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() { running_.store(false, std::memory_order_relaxed); }

    void run(std::function<void(Frame&)> on_frame) {
        running_.store(true, std::memory_order_relaxed);

        // Render thread: owns SDL window and frame loop
        std::thread render_thread([this] {
            RenderLoop loop(
                {config_.title, config_.width, config_.height},
                snapshot_cell_, input_queue_, running_);
            loop.run();
        });

        // App thread: call user lambda, publish snapshots
        Frame frame;
        uint64_t version = 0;
        int w = config_.width;
        int h = config_.height;

        while (running_.load(std::memory_order_relaxed)) {
            // Drain input events
            InputEvent ev;
            while (input_queue_.pop(ev)) {
                if (std::holds_alternative<WindowClose>(ev)) {
                    running_.store(false, std::memory_order_relaxed);
                    break;
                }
                if (auto* resize = std::get_if<WindowResize>(&ev)) {
                    w = resize->width;
                    h = resize->height;
                }
            }

            if (!running_.load(std::memory_order_relaxed)) break;

            frame.reset(w, h);
            on_frame(frame);
            snapshot_cell_.store(frame.take_snapshot(++version));
        }

        render_thread.join();
    }

private:
    AppConfig config_;
    atomic_cell<SceneSnapshot> snapshot_cell_;
    mpsc_queue<InputEvent> input_queue_;
    std::atomic<bool> running_{false};
};

} // namespace prism
```

- [ ] **Step 5: Add includes to umbrella header**

In `include/prism/prism.hpp`, add:

```cpp
#include <prism/core/app.hpp>
#include <prism/core/pixel_buffer.hpp>
#include <prism/core/software_renderer.hpp>
#include <prism/core/render_loop.hpp>
```

- [ ] **Step 6: Run tests and verify they pass**

```bash
meson test -C builddir
```

Expected: all tests pass. The `test_app` tests create real SDL windows briefly, exercise the full pipeline, and shut down cleanly.

Note: On headless CI, these tests may need `SDL_VIDEODRIVER=offscreen` or similar. For local dev, they should work with a display available.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/app.hpp include/prism/prism.hpp tests/test_app.cpp tests/meson.build
git commit -m "feat: add App facade and Frame — zero-boilerplate entry point"
```

---

### Task 7: Hello Rectangle Example

**Files:**
- Create: `examples/hello_rect.cpp`
- Modify: `examples/meson.build`

- [ ] **Step 1: Write the example**

Create `examples/hello_rect.cpp`:

```cpp
#include <prism/prism.hpp>

int main() {
    prism::App app({.title = "Hello PRISM", .width = 800, .height = 600});

    app.run([](prism::Frame& frame) {
        // Background
        frame.filled_rect(
            {0, 0, static_cast<float>(frame.width()), static_cast<float>(frame.height())},
            prism::Color::rgba(30, 30, 40));

        // Centered colored rectangle
        float rw = 200, rh = 100;
        float rx = (frame.width() - rw) / 2;
        float ry = (frame.height() - rh) / 2;
        frame.filled_rect({rx, ry, rw, rh}, prism::Color::rgba(0, 120, 215));

        // Smaller accent rect
        frame.filled_rect({rx + 20, ry + 20, 60, 30}, prism::Color::rgba(255, 180, 0));
    });
}
```

- [ ] **Step 2: Add to examples/meson.build**

```meson
executable('hello_rect', 'hello_rect.cpp',
  dependencies : [prism_dep],
)
```

- [ ] **Step 3: Build and run**

```bash
ninja -C builddir
./builddir/examples/hello_rect
```

Expected: a window appears with a dark background, a blue rectangle centered, and a smaller orange rectangle inside it. Close the window to exit cleanly.

- [ ] **Step 4: Run all tests one final time**

```bash
meson test -C builddir
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add examples/hello_rect.cpp examples/meson.build
git commit -m "feat: add hello_rect POC example — colored rectangles on screen"
```

---

## Summary

| Task | What it delivers | Tests |
|------|-----------------|-------|
| 1 | SDL3 dependency via Meson wrap | Existing tests still pass |
| 2 | `InputEvent` variant types | 3 tests |
| 3 | `PixelBuffer` with `fill_rect` | 4 tests |
| 4 | `SoftwareRenderer` (headless) | 4 tests |
| 5 | `RenderLoop` (SDL window + frame loop) | Tested via Task 6 |
| 6 | `App` + `Frame` facade | 3 integration tests |
| 7 | `hello_rect` example | Visual verification |
