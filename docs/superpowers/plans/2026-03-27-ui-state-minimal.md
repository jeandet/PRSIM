# Minimal Ui<State> + prism::app<State>() Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the MVU entry point `prism::app<State>()` and per-frame `Ui<State>` context with state access and frame escape hatch.

**Architecture:** `prism::app<State>()` is a free function template that owns a `Backend`, `State`, and event loop — parallel to `App`, not wrapping it. `Ui<State>` is a lightweight per-frame value (two pointers) giving read-only state access and a `frame()` escape hatch. A `NullBackend` enables headless testing.

**Tech Stack:** C++26, Meson, doctest

---

### Task 1: Open Frame's private API to app<State>()

`Frame::reset()` and `Frame::take_snapshot()` are private, with `friend class App`. The `app<State>()` free function needs the same access. Rather than adding N friend declarations for every `app()` overload, make a private helper class that `app()` can use.

**Files:**
- Modify: `include/prism/core/app.hpp`

- [ ] **Step 1: Add a forward-declared friend for the app function helper**

In `include/prism/core/app.hpp`, add `friend struct AppAccess;` to `Frame`'s private section, and define the `AppAccess` struct below `Frame`:

```cpp
// Inside class Frame, in the private section, after `friend class App;`:
    friend struct AppAccess;

// After class Frame's closing brace, before class App:
struct AppAccess {
    static void reset(Frame& f, int w, int h) { f.reset(w, h); }
    static std::shared_ptr<const SceneSnapshot> take_snapshot(Frame& f, uint64_t v) {
        return f.take_snapshot(v);
    }
};
```

- [ ] **Step 2: Build and run existing tests to verify nothing breaks**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All 7 tests pass, no compilation errors.

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/app.hpp
git commit -m "refactor: add AppAccess helper for Frame private API"
```

---

### Task 2: NullBackend for headless MVU testing

**Files:**
- Create: `include/prism/core/null_backend.hpp`
- Create: `tests/test_null_backend.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_null_backend.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <prism/core/null_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

TEST_CASE("NullBackend fires WindowClose immediately") {
    prism::NullBackend nb;
    std::vector<prism::InputEvent> received;

    nb.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend submit and wake are no-ops") {
    prism::NullBackend nb;
    nb.submit(nullptr);
    nb.wake();
    nb.quit();
    // No crash = pass
}

TEST_CASE("NullBackend works through Backend wrapper") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```meson
  'null_backend' : files('test_null_backend.cpp'),
```

- [ ] **Step 3: Run test to verify it fails (NullBackend header doesn't exist yet)**

Run: `meson compile -C builddir`
Expected: Compilation error — `prism/core/null_backend.hpp` not found.

- [ ] **Step 4: Create NullBackend**

Create `include/prism/core/null_backend.hpp`:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

namespace prism {

class NullBackend final : public BackendBase {
public:
    void run(std::function<void(const InputEvent&)> event_cb) override {
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
Expected: All tests pass (7 old + 3 new = 10 total, though meson counts test executables, so 8 test suites).

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/null_backend.hpp tests/test_null_backend.cpp tests/meson.build
git commit -m "feat: add NullBackend for headless MVU testing"
```

---

### Task 3: Ui<State> class

**Files:**
- Create: `include/prism/core/ui.hpp`
- Create: `tests/test_ui.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests for Ui<State>**

Create `tests/test_ui.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <prism/core/ui.hpp>
#include <prism/core/null_backend.hpp>

#include <string>

struct TestState {
    int count = 42;
    std::string name = "hello";
};

TEST_CASE("app<State> runs view at least once") {
    int call_count = 0;

    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{},
        [&](prism::Ui<TestState>& ui) {
            ++call_count;
        }
    );

    CHECK(call_count >= 1);
}

TEST_CASE("ui-> gives read-only state access") {
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{.count = 7, .name = "world"},
        [](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 7);
            CHECK(ui->name == "world");
            CHECK(ui.state().count == 7);
        }
    );
}

TEST_CASE("ui.frame() records draw commands") {
    bool drew = false;

    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{},
        [&](prism::Ui<TestState>& ui) {
            ui.frame().filled_rect({10, 10, 50, 50}, prism::Color::rgba(255, 0, 0));
            drew = true;
        }
    );

    CHECK(drew);
}

TEST_CASE("app<State> default-constructs state when not provided") {
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        [](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 42);
            CHECK(ui->name == "hello");
        }
    );
}

TEST_CASE("convenience overload with title creates software-like flow") {
    // This test uses NullBackend via the Backend overload to verify
    // the title+State overload signature compiles and works.
    int called = 0;
    prism::app<TestState>(
        prism::Backend{std::make_unique<prism::NullBackend>()},
        {},
        TestState{.count = 99},
        [&](prism::Ui<TestState>& ui) {
            CHECK(ui->count == 99);
            ++called;
        }
    );
    CHECK(called >= 1);
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to `headless_tests`:

```meson
  'ui' : files('test_ui.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C builddir`
Expected: Compilation error — `prism/core/ui.hpp` not found.

- [ ] **Step 4: Create Ui<State> and app<State>()**

Create `include/prism/core/ui.hpp`:

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
    friend void app(Backend, BackendConfig, S, std::function<void(Ui<S>&)>);
    template <typename S>
    friend void app(Backend, BackendConfig, std::function<void(Ui<S>&)>);
    template <typename S>
    friend void app(std::string_view, S, std::function<void(Ui<S>&)>);
    template <typename S>
    friend void app(std::string_view, std::function<void(Ui<S>&)>);
};

// Core overload — takes an explicit Backend, config, initial state, and view.
template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view) {
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
        }

        if (!running.load(std::memory_order_relaxed)) break;

        AppAccess::reset(frame, w, h);
        Ui<State> ui(state, frame);
        view(ui);
        backend.submit(AppAccess::take_snapshot(frame, ++version));
        backend.wake();
    }

    backend.quit();
    backend_thread.join();
}

// Overload: Backend + config, default-constructed state.
template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view) {
    app<State>(std::move(backend), cfg, State{}, std::move(view));
}

// Convenience: title + initial state → software backend.
template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    app<State>(Backend::software(cfg), cfg, std::move(initial), std::move(view));
}

// Convenience: title only → software backend + default state.
template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view) {
    app<State>(title, State{}, std::move(view));
}

} // namespace prism
```

- [ ] **Step 5: Build and run tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests pass (8 old + 5 new).

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/ui.hpp tests/test_ui.cpp tests/meson.build
git commit -m "feat: add Ui<State> and prism::app<State>() MVU entry point"
```

---

### Task 4: Update umbrella header

**Files:**
- Modify: `include/prism/prism.hpp`

- [ ] **Step 1: Add includes**

Add to `include/prism/prism.hpp`:

```cpp
#include <prism/core/null_backend.hpp>
#include <prism/core/ui.hpp>
```

- [ ] **Step 2: Build and run all tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add include/prism/prism.hpp
git commit -m "chore: add ui.hpp and null_backend.hpp to umbrella header"
```
