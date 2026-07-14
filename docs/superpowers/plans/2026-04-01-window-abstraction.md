# Window Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the implicit single-window model with a proper `Window` abstraction that separates window config from render config, supports decoration modes, and is ready for multi-window.

**Architecture:** Core defines `Window` (abstract interface), `WindowConfig`, `RenderConfig`, `WindowEvent`. Backends implement `Window` (SdlWindow, HeadlessWindow). `BackendBase` gains `create_window()` and routes `WindowEvent` instead of `InputEvent`. `model_app` and `app<State>` updated to use new types.

**Tech Stack:** C++26, SDL3, doctest, Meson

---

### File Map

**Create:**
- `include/prism/core/window.hpp` — `WindowId`, `DecorationMode`, `WindowConfig`, `RenderConfig`, `Window` interface, `WindowEvent`
- `include/prism/core/headless_window.hpp` — `HeadlessWindow` (in-memory Window implementation for tests)
- `include/prism/backends/sdl_window.hpp` — `SdlWindow` wrapping SDL_Window + SDL_Renderer
- `src/backends/sdl_window.cpp` — `SdlWindow` implementation
- `tests/test_window.cpp` — Tests for `HeadlessWindow` and `WindowConfig`

**Modify:**
- `include/prism/core/backend.hpp` — Replace `BackendConfig` with new types, update `BackendBase` and `Backend`
- `include/prism/core/test_backend.hpp` — Use `HeadlessWindow`, accept `WindowEvent`, add `create_window()`
- `include/prism/core/null_backend.hpp` — Same updates
- `include/prism/backends/software_backend.hpp` — Rename to `SdlBackend`, use `SdlWindow`, new signatures
- `src/backends/software_backend.cpp` — Refactor into `SdlBackend` + delegate rendering to `SdlWindow`
- `include/prism/core/model_app.hpp` — Route `WindowEvent`, create window via backend, add `Window&` to `AppContext`
- `include/prism/core/app.hpp` — Update `App` class to use new types
- `include/prism/core/ui.hpp` — Update `app<State>` overloads
- `include/prism/prism.hpp` — Add `window.hpp` and `headless_window.hpp` includes
- `src/meson.build` — Add `sdl_window.cpp`, rename library
- `tests/meson.build` — Add `test_window.cpp`
- `tests/test_null_backend.cpp` — Update to `WindowEvent`-based API
- `tests/test_test_backend.cpp` — Update to `WindowEvent`-based API
- `tests/test_model_app.cpp` — Update custom backends and `model_app` calls
- `tests/test_ui.cpp` — Update `app<State>` calls
- `tests/test_app.cpp` — Update `App` constructor calls

---

### Task 1: Core Types — Window, WindowConfig, RenderConfig, WindowEvent

**Files:**
- Create: `include/prism/core/window.hpp`
- Create: `tests/test_window.cpp`
- Modify: `tests/meson.build`

This task adds all new types without modifying any existing code. Everything compiles alongside the old types.

- [ ] **Step 1: Write tests for core types**

Create `tests/test_window.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/window.hpp>

TEST_CASE("WindowConfig has sensible defaults") {
    prism::WindowConfig cfg;
    CHECK(std::string(cfg.title) == "PRISM");
    CHECK(cfg.width == 800);
    CHECK(cfg.height == 600);
    CHECK(cfg.resizable == true);
    CHECK(cfg.fullscreen == false);
    CHECK(cfg.decoration == prism::DecorationMode::Native);
}

TEST_CASE("RenderConfig defaults to null font path") {
    prism::RenderConfig cfg;
    CHECK(cfg.font_path == nullptr);
}

TEST_CASE("WindowConfig designated initializer override") {
    prism::WindowConfig cfg{.title = "Test", .width = 320, .height = 240, .fullscreen = true};
    CHECK(std::string(cfg.title) == "Test");
    CHECK(cfg.width == 320);
    CHECK(cfg.height == 240);
    CHECK(cfg.fullscreen == true);
    CHECK(cfg.decoration == prism::DecorationMode::Native);
}

TEST_CASE("WindowEvent wraps InputEvent with WindowId") {
    prism::WindowEvent we{.window = 1, .event = prism::WindowClose{}};
    CHECK(we.window == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(we.event));
}

TEST_CASE("DecorationMode enum values") {
    CHECK(prism::DecorationMode::Native != prism::DecorationMode::Custom);
    CHECK(prism::DecorationMode::Custom != prism::DecorationMode::None);
    CHECK(prism::DecorationMode::Native != prism::DecorationMode::None);
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```
  'window' : files('test_window.cpp'),
```

- [ ] **Step 3: Create window.hpp**

Create `include/prism/core/window.hpp`:

```cpp
#pragma once

#include <prism/core/input_event.hpp>

#include <cstdint>
#include <string_view>
#include <utility>

namespace prism {

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

class Window {
public:
    virtual ~Window() = default;

    virtual WindowId id() const = 0;

    virtual void set_title(std::string_view title) = 0;
    virtual void set_size(int w, int h) = 0;
    virtual std::pair<int, int> size() const = 0;
    virtual void set_position(int x, int y) = 0;
    virtual std::pair<int, int> position() const = 0;

    virtual void set_decoration_mode(DecorationMode mode) = 0;
    virtual DecorationMode decoration_mode() const = 0;

    virtual void set_resizable(bool r) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_fullscreen(bool f) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

    virtual void close() = 0;
};

struct WindowEvent {
    WindowId window;
    InputEvent event;
};

} // namespace prism
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir window --print-errorlogs`

Expected: All 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/window.hpp tests/test_window.cpp tests/meson.build
git commit -m "feat(window): add Window interface, WindowConfig, RenderConfig, WindowEvent core types"
```

---

### Task 2: HeadlessWindow

**Files:**
- Create: `include/prism/core/headless_window.hpp`
- Modify: `tests/test_window.cpp`

- [ ] **Step 1: Write tests for HeadlessWindow**

Append to `tests/test_window.cpp`:

```cpp
#include <prism/core/headless_window.hpp>

TEST_CASE("HeadlessWindow stores creation config") {
    prism::HeadlessWindow w(1, {.title = "Test", .width = 320, .height = 240,
                                .resizable = false, .fullscreen = true,
                                .decoration = prism::DecorationMode::Custom});
    CHECK(w.id() == 1);
    auto [ww, hh] = w.size();
    CHECK(ww == 320);
    CHECK(hh == 240);
    CHECK(w.is_resizable() == false);
    CHECK(w.is_fullscreen() == true);
    CHECK(w.decoration_mode() == prism::DecorationMode::Custom);
}

TEST_CASE("HeadlessWindow set_title updates title") {
    prism::HeadlessWindow w(1, {});
    w.set_title("New Title");
    // HeadlessWindow stores it — no crash, no external effect
}

TEST_CASE("HeadlessWindow set_size updates size") {
    prism::HeadlessWindow w(1, {.width = 100, .height = 100});
    w.set_size(640, 480);
    auto [ww, hh] = w.size();
    CHECK(ww == 640);
    CHECK(hh == 480);
}

TEST_CASE("HeadlessWindow position defaults to 0,0") {
    prism::HeadlessWindow w(1, {});
    auto [x, y] = w.position();
    CHECK(x == 0);
    CHECK(y == 0);
}

TEST_CASE("HeadlessWindow set_position updates position") {
    prism::HeadlessWindow w(1, {});
    w.set_position(100, 200);
    auto [x, y] = w.position();
    CHECK(x == 100);
    CHECK(y == 200);
}

TEST_CASE("HeadlessWindow set_decoration_mode") {
    prism::HeadlessWindow w(1, {});
    CHECK(w.decoration_mode() == prism::DecorationMode::Native);
    w.set_decoration_mode(prism::DecorationMode::None);
    CHECK(w.decoration_mode() == prism::DecorationMode::None);
}

TEST_CASE("HeadlessWindow fullscreen toggle") {
    prism::HeadlessWindow w(1, {});
    CHECK(w.is_fullscreen() == false);
    w.set_fullscreen(true);
    CHECK(w.is_fullscreen() == true);
}

TEST_CASE("HeadlessWindow state methods are no-ops") {
    prism::HeadlessWindow w(1, {});
    w.minimize();
    w.maximize();
    w.restore();
    w.show();
    w.hide();
    w.close();
    // No crash = pass
}
```

- [ ] **Step 2: Run tests — expect FAIL (HeadlessWindow not found)**

Run: `meson test -C builddir window --print-errorlogs`

Expected: Compilation error — `headless_window.hpp` not found.

- [ ] **Step 3: Create headless_window.hpp**

Create `include/prism/core/headless_window.hpp`:

```cpp
#pragma once

#include <prism/core/window.hpp>

#include <string>

namespace prism {

class HeadlessWindow final : public Window {
public:
    HeadlessWindow(WindowId id, WindowConfig cfg)
        : id_(id), title_(cfg.title), w_(cfg.width), h_(cfg.height),
          resizable_(cfg.resizable), fullscreen_(cfg.fullscreen),
          decoration_(cfg.decoration) {}

    WindowId id() const override { return id_; }

    void set_title(std::string_view title) override { title_ = title; }
    void set_size(int w, int h) override { w_ = w; h_ = h; }
    std::pair<int, int> size() const override { return {w_, h_}; }
    void set_position(int x, int y) override { x_ = x; y_ = y; }
    std::pair<int, int> position() const override { return {x_, y_}; }

    void set_decoration_mode(DecorationMode m) override { decoration_ = m; }
    DecorationMode decoration_mode() const override { return decoration_; }

    void set_resizable(bool r) override { resizable_ = r; }
    bool is_resizable() const override { return resizable_; }
    void set_fullscreen(bool f) override { fullscreen_ = f; }
    bool is_fullscreen() const override { return fullscreen_; }

    void minimize() override {}
    void maximize() override {}
    void restore() override {}
    void show() override {}
    void hide() override {}
    void close() override {}

private:
    WindowId id_;
    std::string title_;
    int w_, h_;
    int x_ = 0, y_ = 0;
    bool resizable_;
    bool fullscreen_;
    DecorationMode decoration_;
};

} // namespace prism
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir window --print-errorlogs`

Expected: All 13 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/headless_window.hpp tests/test_window.cpp
git commit -m "feat(window): add HeadlessWindow in-memory implementation"
```

---

### Task 3: Update BackendBase and Backend interfaces

**Files:**
- Modify: `include/prism/core/backend.hpp`
- Modify: `include/prism/core/test_backend.hpp`
- Modify: `include/prism/core/null_backend.hpp`
- Modify: `src/backend.cpp`

This is the breaking change — all backends must update simultaneously.

- [ ] **Step 1: Update backend.hpp**

Replace the entire content of `include/prism/core/backend.hpp` with:

```cpp
#pragma once

#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/window.hpp>

#include <functional>
#include <memory>

namespace prism {

// Kept as a type alias during migration — existing code using BackendConfig
// can switch to WindowConfig at its own pace.
using BackendConfig = WindowConfig;

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

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(RenderConfig cfg);

    Window& create_window(WindowConfig cfg) { return impl_->create_window(std::move(cfg)); }
    void run(std::function<void(const WindowEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(WindowId w, std::shared_ptr<const SceneSnapshot> s) { impl_->submit(w, std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }
    void wait_ready() { impl_->wait_ready(); }

    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism
```

Note: `using BackendConfig = WindowConfig` provides a migration bridge so existing code referencing `BackendConfig` still compiles. `BackendConfig` fields (`title`, `width`, `height`) are the same names in `WindowConfig`. The `font_path` field is gone from `WindowConfig` — code using it will fail to compile, which is intentional (it moved to `RenderConfig`).

- [ ] **Step 2: Update test_backend.hpp**

Replace the entire content of `include/prism/core/test_backend.hpp` with:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/headless_window.hpp>

#include <vector>

namespace prism {

class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
    HeadlessWindow window_{0, {}};

public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(WindowEvent{window_.id(), ev});
        event_cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
```

- [ ] **Step 3: Update null_backend.hpp**

Replace the entire content of `include/prism/core/null_backend.hpp` with:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/headless_window.hpp>

namespace prism {

class NullBackend final : public BackendBase {
    HeadlessWindow window_{0, {}};

public:
    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        event_cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
```

- [ ] **Step 4: Verify backend.cpp still compiles**

`src/backend.cpp` only provides `BackendBase::~BackendBase() = default;` — no changes needed. The virtual destructor definition is unchanged.

- [ ] **Step 5: Do NOT run tests yet** — downstream files (model_app, app, ui, tests) still use old API. They will be updated in Tasks 4-6.

---

### Task 4: Update SoftwareBackend → SdlBackend

**Files:**
- Create: `include/prism/backends/sdl_window.hpp`
- Create: `src/backends/sdl_window.cpp`
- Modify: `include/prism/backends/software_backend.hpp`
- Modify: `src/backends/software_backend.cpp`
- Modify: `src/meson.build`

- [ ] **Step 1: Create SdlWindow header**

Create `include/prism/backends/sdl_window.hpp`:

```cpp
#pragma once

#include <prism/core/window.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

namespace prism {

class SdlWindow final : public Window {
public:
    SdlWindow(WindowId id, WindowConfig cfg);
    ~SdlWindow() override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&& other) noexcept;
    SdlWindow& operator=(SdlWindow&& other) noexcept;

    WindowId id() const override { return id_; }

    void set_title(std::string_view title) override;
    void set_size(int w, int h) override;
    std::pair<int, int> size() const override;
    void set_position(int x, int y) override;
    std::pair<int, int> position() const override;

    void set_decoration_mode(DecorationMode mode) override;
    DecorationMode decoration_mode() const override { return decoration_; }

    void set_resizable(bool r) override;
    bool is_resizable() const override;
    void set_fullscreen(bool f) override;
    bool is_fullscreen() const override;
    void minimize() override;
    void maximize() override;
    void restore() override;
    void show() override;
    void hide() override;
    void close() override;

    // Backend-internal access
    SDL_Window* sdl_window() { return sdl_window_; }
    SDL_Renderer* renderer() { return renderer_; }

    void render_snapshot(const SceneSnapshot& snap, TTF_Font* font);

private:
    WindowId id_;
    SDL_Window* sdl_window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    DecorationMode decoration_;
    std::string title_;
    WindowConfig config_;
    std::vector<SDL_Rect> clip_stack_;

    void create_sdl_window();
    void destroy_sdl_window();

    void render_draw_list(const DrawList& dl, TTF_Font* font);
    void render_cmd(const FilledRect& cmd);
    void render_cmd(const RectOutline& cmd);
    void render_cmd(const TextCmd& cmd, TTF_Font* font);
    void render_cmd(const ClipPush& cmd);
    void render_cmd(const ClipPop& cmd);
};

} // namespace prism
```

- [ ] **Step 2: Create SdlWindow implementation**

Create `src/backends/sdl_window.cpp`:

```cpp
#include <prism/backends/sdl_window.hpp>

#include <cmath>

namespace prism {

namespace {

SDL_FRect to_sdl(Rect r) {
    return {r.origin.x.raw(), r.origin.y.raw(), r.extent.w.raw(), r.extent.h.raw()};
}

SDL_Color to_sdl(Color c) {
    return {c.r, c.g, c.b, c.a};
}

} // namespace

SdlWindow::SdlWindow(WindowId id, WindowConfig cfg)
    : id_(id), decoration_(cfg.decoration), title_(cfg.title), config_(cfg)
{
    create_sdl_window();
}

SdlWindow::~SdlWindow() {
    destroy_sdl_window();
}

SdlWindow::SdlWindow(SdlWindow&& other) noexcept
    : id_(other.id_), sdl_window_(other.sdl_window_), renderer_(other.renderer_),
      decoration_(other.decoration_), title_(std::move(other.title_)),
      config_(other.config_), clip_stack_(std::move(other.clip_stack_))
{
    other.sdl_window_ = nullptr;
    other.renderer_ = nullptr;
}

SdlWindow& SdlWindow::operator=(SdlWindow&& other) noexcept {
    if (this != &other) {
        destroy_sdl_window();
        id_ = other.id_;
        sdl_window_ = other.sdl_window_;
        renderer_ = other.renderer_;
        decoration_ = other.decoration_;
        title_ = std::move(other.title_);
        config_ = other.config_;
        clip_stack_ = std::move(other.clip_stack_);
        other.sdl_window_ = nullptr;
        other.renderer_ = nullptr;
    }
    return *this;
}

void SdlWindow::create_sdl_window() {
    uint64_t flags = 0;
    if (config_.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if (decoration_ == DecorationMode::Custom || decoration_ == DecorationMode::None)
        flags |= SDL_WINDOW_BORDERLESS;

    sdl_window_ = SDL_CreateWindow(title_.c_str(), config_.width, config_.height, flags);
    renderer_ = SDL_CreateRenderer(sdl_window_, nullptr);
}

void SdlWindow::destroy_sdl_window() {
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (sdl_window_) { SDL_DestroyWindow(sdl_window_); sdl_window_ = nullptr; }
}

void SdlWindow::set_title(std::string_view title) {
    title_ = title;
    if (sdl_window_) SDL_SetWindowTitle(sdl_window_, title_.c_str());
}

void SdlWindow::set_size(int w, int h) {
    config_.width = w;
    config_.height = h;
    if (sdl_window_) SDL_SetWindowSize(sdl_window_, w, h);
}

std::pair<int, int> SdlWindow::size() const {
    if (sdl_window_) {
        int w, h;
        SDL_GetWindowSize(sdl_window_, &w, &h);
        return {w, h};
    }
    return {config_.width, config_.height};
}

void SdlWindow::set_position(int x, int y) {
    if (sdl_window_) SDL_SetWindowPosition(sdl_window_, x, y);
}

std::pair<int, int> SdlWindow::position() const {
    if (sdl_window_) {
        int x, y;
        SDL_GetWindowPosition(sdl_window_, &x, &y);
        return {x, y};
    }
    return {0, 0};
}

void SdlWindow::set_decoration_mode(DecorationMode mode) {
    if (decoration_ == mode) return;
    decoration_ = mode;
    // Recreate window with new flags
    auto [w, h] = size();
    config_.width = w;
    config_.height = h;
    destroy_sdl_window();
    create_sdl_window();
}

void SdlWindow::set_resizable(bool r) {
    config_.resizable = r;
    // SDL3 doesn't have a runtime toggle — recreate
    if (sdl_window_) {
        auto [w, h] = size();
        config_.width = w;
        config_.height = h;
        destroy_sdl_window();
        create_sdl_window();
    }
}

bool SdlWindow::is_resizable() const { return config_.resizable; }

void SdlWindow::set_fullscreen(bool f) {
    config_.fullscreen = f;
    if (sdl_window_) SDL_SetWindowFullscreen(sdl_window_, f);
}

bool SdlWindow::is_fullscreen() const { return config_.fullscreen; }

void SdlWindow::minimize()  { if (sdl_window_) SDL_MinimizeWindow(sdl_window_); }
void SdlWindow::maximize()  { if (sdl_window_) SDL_MaximizeWindow(sdl_window_); }
void SdlWindow::restore()   { if (sdl_window_) SDL_RestoreWindow(sdl_window_); }
void SdlWindow::show()      { if (sdl_window_) SDL_ShowWindow(sdl_window_); }
void SdlWindow::hide()      { if (sdl_window_) SDL_HideWindow(sdl_window_); }

void SdlWindow::close() {
    destroy_sdl_window();
}

void SdlWindow::render_snapshot(const SceneSnapshot& snap, TTF_Font* font) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
    SDL_RenderClear(renderer_);
    for (uint16_t idx : snap.z_order) {
        render_draw_list(snap.draw_lists[idx], font);
    }
    if (!snap.overlay.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
        render_draw_list(snap.overlay, font);
    }
    SDL_RenderPresent(renderer_);
}

void SdlWindow::render_draw_list(const DrawList& dl, TTF_Font* font) {
    for (const auto& cmd : dl.commands) {
        std::visit([this, font](const auto& c) {
            if constexpr (std::is_same_v<std::decay_t<decltype(c)>, TextCmd>)
                render_cmd(c, font);
            else
                render_cmd(c);
        }, cmd);
    }
}

void SdlWindow::render_cmd(const FilledRect& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderFillRect(renderer_, &r);
}

void SdlWindow::render_cmd(const RectOutline& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderRect(renderer_, &r);
}

void SdlWindow::render_cmd(const TextCmd& cmd, TTF_Font* font) {
    if (!font || cmd.text.empty()) return;

    float current_size = TTF_GetFontSize(font);
    if (std::abs(current_size - cmd.size) > 0.5f) {
        TTF_SetFontSize(font, cmd.size);
    }

    SDL_Color color = to_sdl(cmd.color);
    SDL_Surface* surface = TTF_RenderText_Blended(font, cmd.text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (texture) {
        SDL_FRect dst = {cmd.origin.x.raw(), cmd.origin.y.raw(),
                         static_cast<float>(surface->w),
                         static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

void SdlWindow::render_cmd(const ClipPush& cmd) {
    SDL_Rect r = {static_cast<int>(cmd.rect.origin.x.raw()), static_cast<int>(cmd.rect.origin.y.raw()),
                  static_cast<int>(cmd.rect.extent.w.raw()), static_cast<int>(cmd.rect.extent.h.raw())};
    clip_stack_.push_back(r);
    SDL_SetRenderClipRect(renderer_, &r);
}

void SdlWindow::render_cmd(const ClipPop&) {
    if (!clip_stack_.empty()) clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        SDL_SetRenderClipRect(renderer_, &clip_stack_.back());
    }
}

} // namespace prism
```

- [ ] **Step 3: Update SoftwareBackend header**

Replace the entire content of `include/prism/backends/software_backend.hpp` with:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/backends/sdl_window.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <atomic>
#include <unordered_map>
#include <memory>

namespace prism {

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(RenderConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    Window& create_window(WindowConfig cfg) override;
    void run(std::function<void(const WindowEvent&)> event_cb) override;
    void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;

private:
    RenderConfig render_config_;
    std::unordered_map<WindowId, std::unique_ptr<SdlWindow>> windows_;
    uint32_t next_id_ = 0;
    TTF_Font* font_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};

    // Per-window snapshot storage
    struct WindowSnapshot {
        std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot;
    };
    std::unordered_map<WindowId, WindowSnapshot> snapshots_;

    WindowId sdl_id_to_prism_id(uint32_t sdl_window_id) const;

    static const char* resolve_font_path(const RenderConfig& cfg);
};

} // namespace prism
```

- [ ] **Step 4: Update SoftwareBackend implementation**

Replace the entire content of `src/backends/software_backend.cpp` with:

```cpp
#include <prism/backends/software_backend.hpp>

#include <cmath>

namespace prism {

const char* SoftwareBackend::resolve_font_path(const RenderConfig& cfg) {
    if (cfg.font_path) return cfg.font_path;
#ifdef PRISM_FONT_PATH
    return PRISM_FONT_PATH;
#else
    return nullptr;
#endif
}

SoftwareBackend::SoftwareBackend(RenderConfig cfg)
    : render_config_(cfg)
{}

SoftwareBackend::~SoftwareBackend() {
    if (font_) TTF_CloseFont(font_);
    TTF_Quit();
    windows_.clear();
    SDL_Quit();
}

Window& SoftwareBackend::create_window(WindowConfig cfg) {
    auto id = ++next_id_;
    auto window = std::make_unique<SdlWindow>(id, cfg);
    auto& ref = *window;
    windows_.emplace(id, std::move(window));
    snapshots_[id]; // default-construct snapshot slot
    return ref;
}

WindowId SoftwareBackend::sdl_id_to_prism_id(uint32_t sdl_window_id) const {
    for (auto& [id, win] : windows_) {
        if (SDL_GetWindowID(win->sdl_window()) == sdl_window_id)
            return id;
    }
    return 0;
}

void SoftwareBackend::run(std::function<void(const WindowEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);

    TTF_Init();
    const char* fpath = resolve_font_path(render_config_);
    if (fpath) {
        font_ = TTF_OpenFont(fpath, 16.0f);
    }

    // Start text input for all existing windows
    for (auto& [id, win] : windows_) {
        SDL_StartTextInput(win->sdl_window());
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        do {
            // Resolve which prism window this event belongs to
            WindowId wid = 0;
            if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) {
                wid = sdl_id_to_prism_id(ev.window.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                wid = sdl_id_to_prism_id(ev.motion.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                wid = sdl_id_to_prism_id(ev.button.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                wid = sdl_id_to_prism_id(ev.wheel.windowID);
            } else if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                wid = sdl_id_to_prism_id(ev.key.windowID);
            } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                wid = sdl_id_to_prism_id(ev.text.windowID);
            }
            // For single-window case, fall back to first window
            if (wid == 0 && windows_.size() == 1)
                wid = windows_.begin()->first;

            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowEvent{wid, WindowClose{}});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                event_cb(WindowEvent{wid, WindowResize{ev.window.data1, ev.window.data2}});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event_cb(WindowEvent{wid, MouseMove{Point{X{ev.motion.x}, Y{ev.motion.y}}}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{ev.button.y}}, ev.button.button, true}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{ev.button.y}}, ev.button.button, false}});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event_cb(WindowEvent{wid, MouseScroll{
                    Point{X{ev.wheel.mouse_x}, Y{ev.wheel.mouse_y}}, DX{ev.wheel.x}, DY{ev.wheel.y}}});
                break;
            case SDL_EVENT_KEY_DOWN:
                event_cb(WindowEvent{wid, KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_KEY_UP:
                event_cb(WindowEvent{wid, KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_TEXT_INPUT:
                event_cb(WindowEvent{wid, TextInput{ev.text.text}});
                break;
            case SDL_EVENT_USER:
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (!running_.load(std::memory_order_relaxed)) break;

        // Render any pending snapshots
        for (auto& [id, snap_slot] : snapshots_) {
            auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
            if (snap) {
                if (auto it = windows_.find(id); it != windows_.end())
                    it->second->render_snapshot(*snap, font_);
            }
        }
    }
}

void SoftwareBackend::submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) {
    if (auto it = snapshots_.find(window); it != snapshots_.end())
        it->second.snapshot.store(std::move(snap), std::memory_order_release);
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

Backend Backend::software(RenderConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
```

- [ ] **Step 5: Update src/meson.build**

In `src/meson.build`, change the library source list to include `sdl_window.cpp`:

Replace:
```meson
prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
```

With:
```meson
prism_software_backend_lib = library('prism-software-backend',
  'backend.cpp',
  'backends/software_backend.cpp',
  'backends/sdl_window.cpp',
```

- [ ] **Step 6: Do NOT run tests yet** — model_app, app, ui, and test files still use old API.

---

### Task 5: Update model_app, App, and Ui

**Files:**
- Modify: `include/prism/core/model_app.hpp`
- Modify: `include/prism/core/app.hpp`
- Modify: `include/prism/core/ui.hpp`

- [ ] **Step 1: Update model_app.hpp**

Replace the entire content of `include/prism/core/model_app.hpp` with:

```cpp
#pragma once

#include <prism/core/animation.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/widget_tree.hpp>

#include <cstdint>
#include <thread>
#include <variant>

namespace prism {

class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w)
        : sched_(s), clock_(&c), window_(&w) {}
    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
};

namespace detail {

inline void route_mouse_move(WidgetTree& tree, const SceneSnapshot& snap,
                             const MouseMove& mm) {
    tree.update_hover(hit_test(snap, mm.position));
    if (tree.in_scrollbar_drag()) {
        tree.update_scrollbar_drag(mm.position.y);
        return;
    }
    if (auto cid = tree.captured_id(); cid != 0) {
        auto rect = find_widget_rect(snap, cid);
        InputEvent ev = mm;
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    }
}

inline void route_mouse_button(WidgetTree& tree, const SceneSnapshot& snap,
                               const InputEvent& ev, const MouseButton& mb) {
    if (!mb.pressed && tree.in_scrollbar_drag()) {
        tree.end_scrollbar_drag();
        tree.set_pressed(tree.captured_id(), false);
        return;
    }
    if (mb.pressed) {
        if (auto oid = hit_test_overlay(snap, mb.position)) {
            tree.begin_scrollbar_drag(*oid, mb.position.y);
            if (tree.in_scrollbar_drag()) return;
        }
    }
    auto id = hit_test(snap, mb.position);
    if (id) {
        tree.set_pressed(*id, mb.pressed);
        if (mb.pressed) {
            if (*id != tree.focused_id())
                tree.close_overlays();
            tree.set_focused(*id);
        }
        auto rect = find_widget_rect(snap, *id);
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
    } else if (mb.pressed) {
        tree.close_overlays();
        tree.clear_focus();
    }
}

inline void route_mouse_scroll(WidgetTree& tree, const SceneSnapshot& snap,
                               const MouseScroll& ms) {
    constexpr float wheel_multiplier = 60.f;
    auto id = hit_test(snap, ms.position);
    if (id)
        tree.scroll_at(*id, DY{ms.dy.raw() * wheel_multiplier});
}

inline void route_key_press(WidgetTree& tree, const InputEvent& ev,
                            const KeyPress& kp) {
    constexpr float page_delta = 200.f;
    if (kp.key == keys::tab) {
        if (kp.mods & mods::shift) tree.focus_prev();
        else tree.focus_next();
    } else if (kp.key == keys::page_up && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{-page_delta});
    } else if (kp.key == keys::page_down && tree.focused_id() != 0) {
        tree.scroll_at(tree.focused_id(), DY{page_delta});
    } else if (tree.focused_id() != 0) {
        tree.dispatch(tree.focused_id(), ev);
    }
}

inline void route_text_input(WidgetTree& tree, const InputEvent& ev) {
    if (tree.focused_id() != 0)
        tree.dispatch(tree.focused_id(), ev);
}

} // namespace detail

template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    WidgetTree tree(model);
    AnimationClock anim_clock;
    bool tick_scheduled = false;
    auto [w, h] = window.size();
    uint64_t version = 0;

    std::shared_ptr<const SceneSnapshot> current_snap;

    auto publish = [&] {
        current_snap = std::shared_ptr<const SceneSnapshot>(
            tree.build_snapshot(w, h, ++version));
        backend.submit(window.id(), current_snap);
        backend.wake();
        tree.clear_dirty();
    };

    std::function<void()> schedule_tick;
    schedule_tick = [&] {
        if (!anim_clock.active() || tick_scheduled) return;
        tick_scheduled = true;
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&] {
                tick_scheduled = false;
                anim_clock.tick(AnimationClock::clock::now());
                if (tree.any_dirty())
                    publish();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        loop.finish();
                        return;
                    }

                    bool needs_publish = false;
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        w = resize->width;
                        h = resize->height;
                        needs_publish = true;
                    }
                    if (current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(tree, *current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(tree, *current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(tree, *current_snap, *ms);
                    }
                    if (auto* kp = std::get_if<KeyPress>(&ev))
                        detail::route_key_press(tree, ev, *kp);
                    if (std::get_if<TextInput>(&ev))
                        detail::route_text_input(tree, ev);

                    if (tree.any_dirty() || needs_publish)
                        publish();
                    schedule_tick();
                })
            );
        });
    });

    backend.wait_ready();
    publish();

    if (setup) {
        auto ctx = AppContext(sched, anim_clock, window);
        setup(ctx);
        schedule_tick();
    }

    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window(WindowConfig{.title = title.data()});
    model_app(backend, window, model, std::move(setup));
}

} // namespace prism
```

Key changes:
- `AppContext` gains `Window& window()` accessor (constructed with 3 args now)
- `model_app` primary overload takes `Backend&` + `Window&` instead of `Backend` + `BackendConfig`
- Event callback receives `WindowEvent`, unwraps `.event` for routing
- `submit()` passes `window.id()`
- Convenience overload creates backend and window, calls primary overload
- Dimensions initialized from `window.size()` instead of config

- [ ] **Step 2: Update app.hpp**

Replace the entire content of `include/prism/core/app.hpp` with:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>

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
    void clip_push(Point origin, Size extent) { dl_.clip_push(origin, extent); }
    void clip_pop() { dl_.clip_pop(); }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    friend class App;
    friend struct AppAccess;
    template <typename> friend class Ui;
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
            snap->geometry.push_back({0, {Point{X{0}, Y{0}}, Size{Width{static_cast<float>(width_)}, Height{static_cast<float>(height_)}}}});
            snap->draw_lists.push_back(std::move(dl_));
            snap->z_order.push_back(0);
        }
        dl_.clear();
        return snap;
    }
};

struct AppAccess {
    static void reset(Frame& f, int w, int h) { f.reset(w, h); }
    static std::shared_ptr<const SceneSnapshot> take_snapshot(Frame& f, uint64_t v) {
        return f.take_snapshot(v);
    }
};

class App {
public:
    explicit App(WindowConfig config)
        : backend_(Backend::software(RenderConfig{})),
          window_(backend_.create_window(config)) {}

    explicit App(Backend& backend, Window& window)
        : backend_(backend), window_(window) {}

    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() {
        quit_requested_ = true;
        if (loop_) loop_->finish();
    }

    void run(std::function<void(Frame&)> on_frame) {
        stdexec::run_loop loop;
        loop_ = &loop;
        auto sched = loop.get_scheduler();

        Frame frame;
        uint64_t version = 0;
        auto [w, h] = window_.size();

        auto publish = [&] {
            frame.reset(w, h);
            on_frame(frame);
            backend_.submit(window_.id(), frame.take_snapshot(++version));
            backend_.wake();
        };

        std::thread backend_thread([&] {
            backend_.run([&](const WindowEvent& we) {
                const auto& ev = we.event;
                exec::start_detached(
                    stdexec::schedule(sched)
                    | stdexec::then([&, ev] {
                        if (std::holds_alternative<WindowClose>(ev)) {
                            loop.finish();
                            return;
                        }
                        if (auto* resize = std::get_if<WindowResize>(&ev)) {
                            w = resize->width;
                            h = resize->height;
                        }
                        publish();
                    })
                );
            });
        });

        backend_.wait_ready();
        publish();
        loop.run();
        loop_ = nullptr;
        backend_.quit();
        backend_thread.join();
    }

private:
    Backend& backend_;
    Window& window_;
    stdexec::run_loop* loop_ = nullptr;
    bool quit_requested_ = false;
};

} // namespace prism
```

Key changes:
- `App(WindowConfig)` constructor creates backend + window internally — BUT we have a problem: `App` stores `Backend&` and `Window&`, yet in the convenience constructor the backend is a local. We need `App` to own the backend when created from config. Let me fix this:

Actually, `App` previously owned a `Backend` (by value, move-only). The new API has `Backend&` for the two-arg constructor but the single-arg constructor needs to own. Let me restructure to use `optional`:

Replace the `App` class section with:

```cpp
class App {
public:
    explicit App(WindowConfig config)
        : owned_backend_(Backend::software(RenderConfig{}))
    {
        backend_ = &*owned_backend_;
        window_ = &backend_->create_window(config);
    }

    explicit App(Backend& backend, Window& window)
        : backend_(&backend), window_(&window) {}

    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() {
        quit_requested_ = true;
        if (loop_) loop_->finish();
    }

    void run(std::function<void(Frame&)> on_frame) {
        stdexec::run_loop loop;
        loop_ = &loop;
        auto sched = loop.get_scheduler();

        Frame frame;
        uint64_t version = 0;
        auto [w, h] = window_->size();

        auto publish = [&] {
            frame.reset(w, h);
            on_frame(frame);
            backend_->submit(window_->id(), frame.take_snapshot(++version));
            backend_->wake();
        };

        std::thread backend_thread([&] {
            backend_->run([&](const WindowEvent& we) {
                const auto& ev = we.event;
                exec::start_detached(
                    stdexec::schedule(sched)
                    | stdexec::then([&, ev] {
                        if (std::holds_alternative<WindowClose>(ev)) {
                            loop.finish();
                            return;
                        }
                        if (auto* resize = std::get_if<WindowResize>(&ev)) {
                            w = resize->width;
                            h = resize->height;
                        }
                        publish();
                    })
                );
            });
        });

        backend_->wait_ready();
        publish();
        loop.run();
        loop_ = nullptr;
        backend_->quit();
        backend_thread.join();
    }

private:
    std::optional<Backend> owned_backend_;
    Backend* backend_ = nullptr;
    Window* window_ = nullptr;
    stdexec::run_loop* loop_ = nullptr;
    bool quit_requested_ = false;
};
```

Add `#include <optional>` to the includes.

- [ ] **Step 3: Update ui.hpp**

In `include/prism/core/ui.hpp`, update the `app<State>` function overloads. The primary changes:
- `run()` callback takes `WindowEvent` — unwrap `.event`
- `submit()` takes `WindowId`
- Convenience overloads use `WindowConfig` instead of `BackendConfig`
- Backend and window creation updated

Replace the entire content of `include/prism/core/ui.hpp` with:

```cpp
#pragma once

#include <prism/core/app.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace prism {

template <typename State>
using UpdateFn = std::function<void(State&, const InputEvent&)>;

template <typename State>
class Ui {
public:
    const State* operator->() const { return state_; }
    const State& state() const { return *state_; }

    Frame& frame() {
        if (node_stack_.empty())
            return *frame_;
        flush_leaf();
        return node_frame_;
    }

    template <typename F>
    void row(F&& children) {
        begin_container(LayoutNode::Kind::Row);
        children();
        end_container();
    }

    template <typename F>
    void column(F&& children) {
        begin_container(LayoutNode::Kind::Column);
        children();
        end_container();
    }

    void spacer() {
        flush_leaf();
        LayoutNode sp;
        sp.kind = LayoutNode::Kind::Spacer;
        sp.id = next_id_++;
        current_children().push_back(std::move(sp));
    }

    std::shared_ptr<const SceneSnapshot> take_snapshot(int w, int h, uint64_t version) {
        if (root_.has_value()) {
            flush_leaf();
            auto& root = *root_;
            LayoutAxis axis = (root.kind == LayoutNode::Kind::Row)
                ? LayoutAxis::Horizontal : LayoutAxis::Vertical;
            layout_measure(root, axis);
            layout_arrange(root, Rect{Point{X{0}, Y{0}}, Size{Width{static_cast<float>(w)}, Height{static_cast<float>(h)}}});

            auto snap = std::make_shared<SceneSnapshot>();
            snap->version = version;
            layout_flatten(root, *snap);
            return snap;
        }
        return AppAccess::take_snapshot(*frame_, version);
    }

private:
    const State* state_;
    Frame* frame_;
    Frame node_frame_;
    std::optional<LayoutNode> root_;
    std::vector<LayoutNode*> node_stack_;
    WidgetId next_id_ = 0;

    Ui(const State& s, Frame& f) : state_(&s), frame_(&f) {}

    void begin_container(LayoutNode::Kind kind) {
        flush_leaf();
        if (!root_) {
            root_ = LayoutNode{};
            root_->kind = kind;
            root_->id = next_id_++;
            node_stack_.push_back(&*root_);
        } else {
            auto& parent = current_children();
            parent.push_back(LayoutNode{});
            auto& node = parent.back();
            node.kind = kind;
            node.id = next_id_++;
            node_stack_.push_back(&node);
        }
    }

    void end_container() {
        flush_leaf();
        node_stack_.pop_back();
    }

    void flush_leaf() {
        if (node_stack_.empty()) return;
        DrawList& dl = node_frame_.dl_;
        if (dl.empty()) return;
        LayoutNode leaf;
        leaf.kind = LayoutNode::Kind::Leaf;
        leaf.id = next_id_++;
        leaf.draws = std::move(dl);
        dl.clear();
        current_children().push_back(std::move(leaf));
    }

    std::vector<LayoutNode>& current_children() {
        return node_stack_.back()->children;
    }

    template <typename S>
    friend void app(Backend&, Window&, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
};

template <typename State>
void app(Backend& backend, Window& window, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    State state = std::move(initial);
    Frame frame;
    auto [w, h] = window.size();
    uint64_t version = 0;

    auto publish = [&] {
        AppAccess::reset(frame, w, h);
        Ui<State> ui(state, frame);
        view(ui);
        backend.submit(window.id(), ui.take_snapshot(w, h, ++version));
        backend.wake();
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        loop.finish();
                        return;
                    }
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        w = resize->width;
                        h = resize->height;
                    }
                    if (update) { update(state, ev); }
                    publish();
                })
            );
        });
    });

    backend.wait_ready();
    publish();
    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window(WindowConfig{.title = title.data()});
    app<State>(backend, window, std::move(initial), std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(title, State{}, std::move(view), std::move(update));
}

} // namespace prism
```

- [ ] **Step 4: Do NOT run tests yet** — test files still use old API.

---

### Task 6: Update all test files

**Files:**
- Modify: `tests/test_null_backend.cpp`
- Modify: `tests/test_test_backend.cpp`
- Modify: `tests/test_model_app.cpp`
- Modify: `tests/test_ui.cpp`
- Modify: `tests/test_app.cpp`

- [ ] **Step 1: Update test_null_backend.cpp**

Replace entire content with:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/null_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

TEST_CASE("NullBackend fires WindowClose immediately") {
    prism::NullBackend nb;
    auto& window = nb.create_window({});
    std::vector<prism::InputEvent> received;

    nb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend submit and wake are no-ops") {
    prism::NullBackend nb;
    auto& window = nb.create_window({});
    nb.submit(window.id(), nullptr);
    nb.wake();
    nb.quit();
}

TEST_CASE("NullBackend works through Backend wrapper") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    auto& window = backend.create_window({});
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend create_window returns valid window") {
    prism::NullBackend nb;
    auto& window = nb.create_window({.title = "Test", .width = 320, .height = 240});
    CHECK(window.id() == 1);
    auto [w, h] = window.size();
    CHECK(w == 320);
    CHECK(h == 240);
}
```

- [ ] **Step 2: Update test_test_backend.cpp**

Replace entire content with:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/test_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

namespace {
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

TEST_CASE("TestBackend fires events then WindowClose") {
    std::vector<prism::InputEvent> events = {
        prism::MouseButton{P(100, 50), 1, true},
        prism::KeyPress{42, 0},
    };
    prism::TestBackend tb{events};
    tb.create_window({});
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 3);
    CHECK(std::holds_alternative<prism::MouseButton>(received[0]));
    CHECK(std::holds_alternative<prism::KeyPress>(received[1]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[2]));
}

TEST_CASE("TestBackend with no events fires only WindowClose") {
    prism::TestBackend tb{{}};
    tb.create_window({});
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("TestBackend works through Backend wrapper") {
    std::vector<prism::InputEvent> events = {
        prism::MouseMove{P(10, 20)},
    };
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    backend.create_window({});
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 2);
    CHECK(std::holds_alternative<prism::MouseMove>(received[0]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[1]));
}
```

- [ ] **Step 3: Update test_model_app.cpp**

Replace entire content with:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/headless_window.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <string>

struct TestModel {
    prism::Field<int> count{42};
    prism::Field<std::string> name{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, name);
    }
};

struct NestedTestModel {
    TestModel inner;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(inner, flag);
    }
};

TEST_CASE("model_app runs and produces a snapshot") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        prism::HeadlessWindow window_{0, {}};
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 2);
}

TEST_CASE("model_app with nested model") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        prism::HeadlessWindow window_{0, {}};
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    NestedTestModel model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>(captured)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 3);
}

struct ClickTestModel {
    prism::Field<bool> toggle{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(toggle);
    }
};

TEST_CASE("model_app routes MouseButton to Field<bool> toggle") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct ClickBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        ClickBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            auto center = rect.center();

            cb(prism::WindowEvent{window_.id(), prism::MouseButton{center, 1, true}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    ClickTestModel model;
    CHECK(model.toggle.get() == false);

    auto backend = prism::Backend{std::make_unique<ClickBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(model.toggle.get() == true);
    CHECK(snap_count.load() >= 2);
}

TEST_CASE("model_app setup callback receives scheduler and window") {
    bool setup_called = false;

    struct SetupBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    auto backend = prism::Backend{std::make_unique<SetupBackend>()};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model,
        [&](prism::AppContext& ctx) {
            setup_called = true;
            auto sched = ctx.scheduler();
            (void)sched;
            CHECK(ctx.window().id() == 1);
        }
    );

    CHECK(setup_called);
}

#include <prism/core/delegate.hpp>

struct SliderClickModel {
    prism::Field<prism::Slider<>> volume{{.value = 0.0}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(volume);
    }
};

TEST_CASE("model_app routes click to Slider and updates value") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct SliderBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        SliderBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        prism::Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            cb(prism::WindowEvent{window_.id(), prism::MouseButton{
                prism::Point{prism::X{rect.origin.x.raw() + 190}, prism::Y{rect.origin.y.raw() + 15}}, 1, true}});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SliderClickModel model;
    auto backend = prism::Backend{std::make_unique<SliderBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(model.volume.get().value > 0.8);
}
```

- [ ] **Step 4: Update test_ui.cpp**

The `app<State>` overloads changed signatures. The test uses three calling patterns:
1. `app<State>(Backend, BackendConfig, initial, view, update)` → `app<State>(Backend&, Window&, initial, view, update)`
2. `app<State>(Backend, BackendConfig, view, update)` → removed (use explicit initial state)
3. `app<State>(title, initial, view, update)` → unchanged
4. `app<State>(title, view, update)` → unchanged

For tests using `Backend` + `BackendConfig`, update to create backend + window, then call `app<State>(backend, window, ...)`.

In `tests/test_ui.cpp`, apply these changes:

Every test that constructs a backend directly (e.g., `prism::Backend{std::make_unique<prism::NullBackend>()}`) and calls `app<State>(backend, cfg, ...)` needs to:
1. Create backend
2. Call `backend.create_window(cfg)`
3. Call `app<State>(backend, window, ...)`

And every test using `prism::BackendConfig{...}` can keep using that name (it's now a type alias for `WindowConfig`).

The callbacks change from `const prism::InputEvent&` to `const prism::WindowEvent&`.

For the `CapturingBackend` in test_ui.cpp that captures snapshots, update to implement the new `BackendBase` interface.

Apply these search-and-replace patterns throughout test_ui.cpp:

1. All custom backend classes need:
   - Add `prism::HeadlessWindow window_{0, {}};` member
   - Add `prism::Window& create_window(prism::WindowConfig cfg) override { window_ = prism::HeadlessWindow{1, cfg}; return window_; }`
   - Change `void run(std::function<void(const prism::InputEvent&)> cb)` to `void run(std::function<void(const prism::WindowEvent&)> cb)`
   - Change `cb(prism::WindowClose{})` to `cb(prism::WindowEvent{window_.id(), prism::WindowClose{}})`
   - Change `void submit(std::shared_ptr<const prism::SceneSnapshot> s)` to `void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot> s)`

2. All `app<State>(Backend{...}, cfg, ...)` calls need to become:
   ```cpp
   auto backend = prism::Backend{std::make_unique<SomeBackend>(...)};
   auto& window = backend.create_window(cfg);
   prism::app<State>(backend, window, ...);
   ```

3. All `UpdateFn` callbacks receiving `InputEvent` stay the same — `UpdateFn` is unchanged.

4. Tests using `app<State>(title, ...)` convenience overloads — unchanged.

Due to the large number of tests in this file, the implementor should apply these patterns systematically to all 12 test cases.

- [ ] **Step 5: Update test_app.cpp**

Replace entire content with:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/core/app.hpp>

namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
}

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
        frame.filled_rect(R(10, 10, 50, 50), prism::Color::rgba(255, 0, 0));
        app.quit();
    });
    CHECK(true);
}
```

Note: `test_app.cpp` uses `App(BackendConfig{...})` which now maps to `App(WindowConfig{...})` via the type alias. The `.title`, `.width`, `.height` fields exist on both. BUT `BackendConfig` had a `.font_path` field that `WindowConfig` does NOT have. Check if test_app.cpp uses `.font_path` — it doesn't, so no change needed beyond what the alias provides. The constructor `App(WindowConfig)` is defined in the updated `app.hpp`.

- [ ] **Step 6: Update umbrella header**

In `include/prism/prism.hpp`, add the new includes:

Add after `#include <prism/core/backend.hpp>`:
```cpp
#include <prism/core/window.hpp>
#include <prism/core/headless_window.hpp>
```

- [ ] **Step 7: Build and run all tests**

Run: `meson compile -C builddir && meson test -C builddir --print-errorlogs`

Expected: Full build succeeds, all tests pass.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(window): Window abstraction, SdlWindow, HeadlessWindow, WindowEvent routing

Replaces implicit single-window model with proper Window interface.
BackendBase gains create_window(), events wrapped in WindowEvent,
submit() takes WindowId. SdlWindow wraps SDL_Window/Renderer.
HeadlessWindow for tests. AppContext exposes Window&."
```

---

### Task 7: Update examples

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Verify the dashboard still compiles**

The dashboard uses the convenience overload `model_app("title", model, setup)` which was updated to create its own backend + window. No changes should be needed.

Run: `meson compile -C builddir model_dashboard`

Expected: Compiles successfully.

If it fails due to the `BackendConfig` removal (the dashboard doesn't use it directly — it uses the title-based convenience overload), fix accordingly.

- [ ] **Step 2: Commit if any changes were needed**

If no changes: skip this step. The dashboard should work as-is since it uses the convenience overload.

---

### Task 8: Clean up migration alias

**Files:**
- Modify: `include/prism/core/backend.hpp`

This task removes the `BackendConfig` type alias once all code has been migrated.

- [ ] **Step 1: Search for remaining BackendConfig usage**

Run: `grep -r "BackendConfig" include/ src/ tests/ examples/ --include="*.cpp" --include="*.hpp" -l`

Expected: No files reference `BackendConfig` (all migrated in previous tasks).

- [ ] **Step 2: Remove the alias**

In `include/prism/core/backend.hpp`, remove this line:
```cpp
using BackendConfig = WindowConfig;
```

- [ ] **Step 3: Build and run all tests**

Run: `meson compile -C builddir && meson test -C builddir --print-errorlogs`

Expected: All pass — no code references `BackendConfig` anymore.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/backend.hpp
git commit -m "refactor: remove BackendConfig migration alias"
```
