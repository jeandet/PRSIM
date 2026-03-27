# Update Callback + Interactive Example Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional `update(State&, InputEvent)` callback to `prism::app<State>()` and rewrite the example as an interactive demo (click = color change, arrows = move rect).

**Architecture:** The core `app()` overload gains an optional `update` parameter. The event drain loop calls it for every event except `WindowClose`. A new `TestBackend` fires synthetic event sequences for testing. The example is rewritten to use `app<State>()` with both `view` and `update`.

**Tech Stack:** C++26, Meson, doctest, SDL3 (keycodes in example only)

---

### Task 1: TestBackend for synthetic event sequences

**Files:**
- Create: `include/prism/core/test_backend.hpp`
- Create: `tests/test_test_backend.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_test_backend.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/test_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

TEST_CASE("TestBackend fires events then WindowClose") {
    std::vector<prism::InputEvent> events = {
        prism::MouseButton{{100, 50}, 1, true},
        prism::KeyPress{42, 0},
    };
    prism::TestBackend tb{events};
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 3);
    CHECK(std::holds_alternative<prism::MouseButton>(received[0]));
    CHECK(std::holds_alternative<prism::KeyPress>(received[1]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[2]));
}

TEST_CASE("TestBackend with no events fires only WindowClose") {
    prism::TestBackend tb{{}};
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("TestBackend works through Backend wrapper") {
    std::vector<prism::InputEvent> events = {
        prism::MouseMove{{10, 20}},
    };
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 2);
    CHECK(std::holds_alternative<prism::MouseMove>(received[0]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[1]));
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add `'test_backend'` to the test group that links `prism_software_backend_dep` (the same group that `null_backend` is in — it needs `BackendBase::~BackendBase()`):

```meson
  'test_backend' : files('test_test_backend.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C builddir`
Expected: Compilation error — `prism/core/test_backend.hpp` not found.

- [ ] **Step 4: Create TestBackend**

Create `include/prism/core/test_backend.hpp`:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

namespace prism {

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

} // namespace prism
```

- [ ] **Step 5: Build and run tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests pass (9 old + 1 new executable).

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/test_backend.hpp tests/test_test_backend.cpp tests/meson.build
git commit -m "feat: add TestBackend for synthetic event sequences"
```

---

### Task 2: Add update callback to app<State>()

**Files:**
- Modify: `include/prism/core/ui.hpp`
- Modify: `tests/test_ui.cpp`

- [ ] **Step 1: Write failing tests for update callback**

Append to `tests/test_ui.cpp` (add `#include <prism/core/test_backend.hpp>` at the top alongside existing includes):

```cpp
TEST_CASE("update callback mutates state on mouse click") {
    struct ClickState { int clicks = 0; };

    std::vector<prism::InputEvent> events = {
        prism::MouseButton{{50, 50}, 1, true},
    };

    int last_seen_clicks = -1;
    prism::app<ClickState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
        ClickState{},
        [&](prism::Ui<ClickState>& ui) {
            last_seen_clicks = ui->clicks;
        },
        [](ClickState& s, const prism::InputEvent& ev) {
            if (std::holds_alternative<prism::MouseButton>(ev))
                ++s.clicks;
        }
    );

    CHECK(last_seen_clicks == 1);
}

TEST_CASE("update callback mutates state on key press") {
    struct MoveState { float x = 0; };

    std::vector<prism::InputEvent> events = {
        prism::KeyPress{123, 0},
        prism::KeyPress{123, 0},
    };

    float last_x = -1;
    prism::app<MoveState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
        MoveState{},
        [&](prism::Ui<MoveState>& ui) {
            last_x = ui->x;
        },
        [](MoveState& s, const prism::InputEvent& ev) {
            if (std::holds_alternative<prism::KeyPress>(ev))
                s.x += 10.f;
        }
    );

    CHECK(last_x == 20.f);
}

TEST_CASE("multiple events accumulate state changes") {
    struct CountState { int n = 0; };

    std::vector<prism::InputEvent> events = {
        prism::MouseButton{{0, 0}, 1, true},
        prism::KeyPress{1, 0},
        prism::MouseMove{{10, 20}},
    };

    int last_n = -1;
    prism::app<CountState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
        CountState{},
        [&](prism::Ui<CountState>& ui) {
            last_n = ui->n;
        },
        [](CountState& s, const prism::InputEvent&) {
            ++s.n;
        }
    );

    CHECK(last_n == 3);
}

TEST_CASE("no update callback is safe (existing behavior)") {
    // This test verifies existing tests still work — app() with no update param.
    int called = 0;
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{},
        [&](prism::Ui<TestState>& /*ui*/) {
            ++called;
        }
    );
    CHECK(called >= 1);
}

TEST_CASE("WindowResize is forwarded to update callback") {
    struct ResizeState { int last_w = 0; };

    std::vector<prism::InputEvent> events = {
        prism::WindowResize{1024, 768},
    };

    int last_w = -1;
    prism::app<ResizeState>(
        prism::Backend{std::make_unique<prism::TestBackend>(events)},
        {},
        ResizeState{},
        [&](prism::Ui<ResizeState>& ui) {
            last_w = ui->last_w;
        },
        [](ResizeState& s, const prism::InputEvent& ev) {
            if (auto* r = std::get_if<prism::WindowResize>(&ev))
                s.last_w = r->width;
        }
    );

    CHECK(last_w == 1024);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C builddir`
Expected: Compilation error — `app()` doesn't accept 5 arguments yet.

- [ ] **Step 3: Add update parameter to all app() overloads**

Replace the entire content of `include/prism/core/ui.hpp` with:

```cpp
#pragma once

#include <prism/core/app.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace prism {

template <typename State>
using UpdateFn = std::function<void(State&, const InputEvent&)>;

template <typename State>
class Ui {
public:
    const State* operator->() const { return state_; }
    const State& state() const { return *state_; }
    Frame& frame() { return *frame_; }

private:
    const State* state_;
    Frame* frame_;

    Ui(const State& s, Frame& f) : state_(&s), frame_(&f) {}

    template <typename S>
    friend void app(Backend, BackendConfig, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(Backend, BackendConfig,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
};

// Core overload
template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view,
         UpdateFn<State> update = {}) {
    mpsc_queue<InputEvent> input_queue;
    std::atomic<bool> running{true};
    std::atomic<bool> input_pending{false};

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
            input_queue.push(ev);
            input_pending.store(true, std::memory_order_release);
            input_pending.notify_one();
        });
    });

    backend.wait_ready();

    State state = std::move(initial);
    Frame frame;
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    AppAccess::reset(frame, w, h);
    Ui<State> ui(state, frame);
    view(ui);
    backend.submit(AppAccess::take_snapshot(frame, ++version));
    backend.wake();

    while (running.load(std::memory_order_relaxed)) {
        input_pending.wait(false, std::memory_order_acquire);
        input_pending.store(false, std::memory_order_relaxed);

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

        if (!running.load(std::memory_order_relaxed)) break;

        AppAccess::reset(frame, w, h);
        Ui<State> ui2(state, frame);
        view(ui2);
        backend.submit(AppAccess::take_snapshot(frame, ++version));
        backend.wake();
    }

    backend.quit();
    backend_thread.join();
}

// Backend + config, default state
template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view,
         UpdateFn<State> update = {}) {
    app<State>(std::move(backend), cfg, State{}, std::move(view), std::move(update));
}

// Title + initial state → software backend
template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view,
         UpdateFn<State> update = {}) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    app<State>(Backend::software(cfg), cfg, std::move(initial),
              std::move(view), std::move(update));
}

// Title only → software backend + default state
template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view,
         UpdateFn<State> update = {}) {
    app<State>(title, State{}, std::move(view), std::move(update));
}

} // namespace prism
```

- [ ] **Step 4: Build and run all tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests pass (old tests unchanged, new tests pass).

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/ui.hpp tests/test_ui.cpp
git commit -m "feat: add update callback to prism::app<State>() for input-driven state"
```

---

### Task 3: Rewrite example as interactive demo

**Files:**
- Modify: `examples/hello_rect.cpp`

- [ ] **Step 1: Rewrite the example**

Replace `examples/hello_rect.cpp` with:

```cpp
#include <prism/prism.hpp>

#include <SDL3/SDL_keycode.h>

#include <array>

struct State {
    float x = 300, y = 250;
    uint8_t color_index = 0;
};

static constexpr std::array colors = {
    prism::Color::rgba(0, 120, 215),
    prism::Color::rgba(215, 50, 50),
    prism::Color::rgba(50, 180, 50),
    prism::Color::rgba(200, 150, 0),
};

int main() {
    prism::app<State>("Interactive PRISM", State{},
        [](auto& ui) {
            auto& f = ui.frame();
            f.filled_rect(
                {0, 0, static_cast<float>(f.width()), static_cast<float>(f.height())},
                prism::Color::rgba(30, 30, 40));
            f.filled_rect({ui->x, ui->y, 200, 100}, colors[ui->color_index]);
        },
        [](State& s, const prism::InputEvent& ev) {
            if (auto* click = std::get_if<prism::MouseButton>(&ev);
                click && click->pressed) {
                s.color_index = (s.color_index + 1) % colors.size();
            }
            if (auto* key = std::get_if<prism::KeyPress>(&ev)) {
                constexpr float step = 20.f;
                switch (key->key) {
                case SDLK_RIGHT: s.x += step; break;
                case SDLK_LEFT:  s.x -= step; break;
                case SDLK_DOWN:  s.y += step; break;
                case SDLK_UP:    s.y -= step; break;
                default: break;
                }
            }
        }
    );
}
```

- [ ] **Step 2: Build**

Run: `meson compile -C builddir`
Expected: Compiles without errors. (The example links `prism_dep` which includes SDL3.)

- [ ] **Step 3: Commit**

```bash
git add examples/hello_rect.cpp
git commit -m "feat: rewrite example as interactive rect demo using app<State>()"
```

---

### Task 4: Update umbrella header

**Files:**
- Modify: `include/prism/prism.hpp`

- [ ] **Step 1: Add test_backend.hpp to umbrella header**

Add this line to `include/prism/prism.hpp` in alphabetical order (after `software_renderer.hpp`, before `ui.hpp`):

```cpp
#include <prism/core/test_backend.hpp>
```

- [ ] **Step 2: Build and run all tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add include/prism/prism.hpp
git commit -m "chore: add test_backend.hpp to umbrella header"
```
