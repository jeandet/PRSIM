# Dual-Window Event Loop Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `model_app()` safely route events to and publish snapshots for more than one `Window`/`WidgetTree` pair sharing one app thread, and let an app add a second window after the loop has already started — with zero behavior change for every existing single-window app.

**Architecture:** `model_app()` always drives events through a new `WindowRegistry` (holding exactly one entry unless something later attaches a second). `AppContext` gains `registry()`/`backend()`/`set_global_key_handler()` accessors so later code (outside this plan's scope) can attach a second window. `BackendBase::request_window()` (default: synchronous `create_window`) is the one new virtual; only `SoftwareBackend` needs a real cross-thread implementation, reusing the existing lock-free `mpsc_queue`.

**Tech Stack:** C++26, GCC 16 `-freflection`, Meson, doctest, SDL3 (only for `SoftwareBackend`'s override).

## Global Constraints

- `WidgetTree` is neither copyable nor movable (deleted copy ctor, no declared move ctor) — never store it by value anywhere that requires moving one in after construction; own it via `std::unique_ptr<WidgetTree>` and construct in place.
- `model_app()`'s existing single-window test suites (`tests/test_model_app.cpp`, `tests/test_ui.cpp`) must pass **unmodified** after every task in this plan — they are the regression proof that the registry-of-one refactor preserves current behavior byte-for-byte in outcome (not necessarily in generated code).
- `BackendBase::request_window` is a **default virtual, not pure** — do not force every `BackendBase` subclass (there are 10+ across `tests/test_ui.cpp` and `tests/test_model_app.cpp`) to add an override.
- Build/verify with `meson test -C builddir` (or `ninja -C builddir test`) after every task; read the actual pass/fail count, don't infer from partial output.
- No app code changes anywhere in this plan — everything is inside `include/prism/app/` or `include/prism/backends/`.

---

## Task 1: Extract event routing into its own header

Pure mechanical extraction, no behavior change — needed so `window_registry.hpp` (Task 2) and
`model_app.hpp` can both use the routing functions without a circular include.

**Files:**
- Create: `include/prism/app/event_routing.hpp`
- Modify: `include/prism/app/model_app.hpp:1-124`

**Interfaces:**
- Produces: `prism::app::detail::route_mouse_move/route_mouse_button/route_mouse_scroll/route_key_press/route_text_input` — same signatures as today, just relocated.

- [ ] **Step 1: Create `event_routing.hpp` with the routing functions moved verbatim**

```cpp
// include/prism/app/event_routing.hpp
#pragma once

#include <prism/input/hit_test.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>

#include <variant>

namespace prism::app::detail {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

inline void route_mouse_move(WidgetTree& tree, const SceneSnapshot& snap,
                             const MouseMove& mm) {
    auto hovered = hit_test(snap, mm.position);
    tree.update_hover(hovered);
    if (tree.in_scrollbar_drag()) {
        tree.update_scrollbar_drag(mm.position.y);
        return;
    }
    if (auto cid = tree.captured_id(); cid != 0) {
        auto rect = find_widget_rect(snap, cid);
        InputEvent ev = mm;
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    } else if (hovered) {
        auto rect = find_widget_rect(snap, *hovered);
        InputEvent ev = mm;
        tree.dispatch(*hovered, rect ? localize_mouse(ev, *rect) : ev);
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
    if (!mb.pressed && tree.captured_id() != 0) {
        auto cid = tree.captured_id();
        tree.set_pressed(cid, false);
        auto rect = find_widget_rect(snap, cid);
        tree.dispatch(cid, rect ? localize_mouse(ev, *rect) : ev);
    } else if (id) {
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
    if (id) {
        auto rect = find_widget_rect(snap, *id);
        InputEvent ev = ms;
        tree.dispatch(*id, rect ? localize_mouse(ev, *rect) : ev);
        tree.scroll_at(*id, DY{ms.dy.raw() * wheel_multiplier});
    }
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

} // namespace prism::app::detail
```

- [ ] **Step 2: Replace the extracted block in `model_app.hpp` with an include**

Remove lines 37-124 (the entire `namespace detail { ... }` block) from `model_app.hpp` and add the
include:

```cpp
// model_app.hpp, in the #include block near the top
#include <prism/app/event_routing.hpp>
```

`model_app.hpp` no longer needs `#include <prism/input/hit_test.hpp>` directly (now pulled in
transitively via `event_routing.hpp`), but leave it if anything else in the file still uses it
directly — check before removing.

- [ ] **Step 3: Build and run the full test suite — must be unchanged**

```bash
meson test -C builddir
```
Expected: identical pass count to the pre-change baseline (record the baseline count before Step 1
so this is a real comparison, not an assumption).

- [ ] **Step 4: Commit**

```bash
git add include/prism/app/event_routing.hpp include/prism/app/model_app.hpp
git commit -m "refactor: extract event routing functions into event_routing.hpp

No behavior change — needed so window_registry.hpp and model_app.hpp
can both use the routing functions without a circular include."
```

---

## Task 2: `WindowRegistry`

**Files:**
- Create: `include/prism/app/window_registry.hpp`
- Test: `tests/test_window_registry.cpp`
- Modify: `tests/meson.build` (register the new test)

**Interfaces:**
- Consumes: `prism::app::detail::route_*` (Task 1), `prism::ui::WidgetTree` (existing).
- Produces: `prism::app::WindowRegistry` — `template<typename Model> WindowId add(Window&, Model&)`,
  `void remove(WindowId)`, `Entry* find(WindowId)`, `template<typename Fn> void for_each(Fn&&)`,
  `template<typename Fn> void for_each_dirty(Fn&&)`. `WindowRegistry::Entry` — public struct with
  `Window* window`, `std::unique_ptr<WidgetTree> tree`, `std::shared_ptr<const SceneSnapshot>
  current_snap`, `int width`, `int height`, `uint64_t version`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_window_registry.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/window_registry.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct Counter {
    prism::Field<int> value{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); }
};
}

TEST_CASE("WindowRegistry adds an entry and routes a dispatch to it") {
    prism::HeadlessWindow win{1, {}};
    Counter model;
    prism::WindowRegistry registry;

    auto id = registry.add(win, model);
    CHECK(id == 1);

    auto* entry = registry.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->window == &win);
    CHECK(entry->tree != nullptr);
}

TEST_CASE("WindowRegistry::find returns nullptr for an unknown id") {
    prism::WindowRegistry registry;
    CHECK(registry.find(999) == nullptr);
}

TEST_CASE("WindowRegistry routes two entries independently") {
    prism::HeadlessWindow win_a{1, {}};
    prism::HeadlessWindow win_b{2, {}};
    Counter model_a, model_b;
    prism::WindowRegistry registry;

    auto id_a = registry.add(win_a, model_a);
    auto id_b = registry.add(win_b, model_b);
    CHECK(id_a != id_b);

    registry.find(id_a)->tree->root().dirty = true;

    size_t dirty_count = 0;
    prism::WindowId dirty_id = 0;
    registry.for_each_dirty([&](prism::WindowId id, prism::WindowRegistry::Entry&) {
        ++dirty_count;
        dirty_id = id;
    });
    CHECK(dirty_count == 1);
    CHECK(dirty_id == id_a);
}

TEST_CASE("WindowRegistry::remove erases an entry") {
    prism::HeadlessWindow win{1, {}};
    Counter model;
    prism::WindowRegistry registry;
    auto id = registry.add(win, model);
    registry.remove(id);
    CHECK(registry.find(id) == nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
meson setup builddir --reconfigure  # picks up the new source once added to meson.build in Step 4
```
Expected at this point: compile failure (`window_registry.hpp` doesn't exist yet) — confirms the
test is exercising code that doesn't exist.

- [ ] **Step 3: Implement `WindowRegistry`**

```cpp
// include/prism/app/window_registry.hpp
#pragma once

#include <prism/app/event_routing.hpp>
#include <prism/app/window.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>

#include <memory>
#include <unordered_map>

namespace prism::app {
using namespace prism::ui;

class WindowRegistry {
public:
    struct Entry {
        Window* window;
        std::unique_ptr<WidgetTree> tree;
        std::shared_ptr<const SceneSnapshot> current_snap;
        int width = 0;
        int height = 0;
        uint64_t version = 0;
    };

    template <typename Model>
    WindowId add(Window& window, Model& model) {
        WindowId id = window.id();
        Entry e;
        e.window = &window;
        e.tree = std::make_unique<WidgetTree>(model);
        auto [w, h] = window.size();
        if (window.decoration_mode() == DecorationMode::Custom)
            h -= static_cast<int>(WindowChrome::title_bar_h.raw());
        e.width = w;
        e.height = h;
        entries_.emplace(id, std::move(e));
        return id;
    }

    void remove(WindowId id) { entries_.erase(id); }

    [[nodiscard]] Entry* find(WindowId id) {
        auto it = entries_.find(id);
        return it != entries_.end() ? &it->second : nullptr;
    }

    template <typename Fn>
    void for_each(Fn&& fn) {
        for (auto& [id, entry] : entries_) fn(id, entry);
    }

    template <typename Fn>
    void for_each_dirty(Fn&& fn) {
        for (auto& [id, entry] : entries_)
            if (entry.tree->any_dirty()) fn(id, entry);
    }

private:
    std::unordered_map<WindowId, Entry> entries_;
};

} // namespace prism::app
```

- [ ] **Step 4: Register the test in meson.build**

```meson
# tests/meson.build, add to the headless_tests dict (alphabetical, near 'widget_tree')
  'window_registry' : files('test_window_registry.cpp'),
```

- [ ] **Step 5: Build and run — verify the new tests pass**

```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir window_registry
```
Expected: all 4 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/window_registry.hpp tests/test_window_registry.cpp tests/meson.build
git commit -m "feat: add WindowRegistry for routing events to multiple window/tree pairs

Entry owns its WidgetTree via unique_ptr, since WidgetTree itself is
neither copyable nor movable."
```

---

## Task 3: `BackendBase::request_window` default + `SoftwareBackend` override

**Files:**
- Modify: `include/prism/app/backend.hpp`
- Modify: `include/prism/backends/software_backend.hpp`
- Modify: `src/backends/software_backend.cpp`

**Interfaces:**
- Produces: `BackendBase::request_window(WindowConfig) -> Window*` (default virtual), `Backend::request_window(WindowConfig) -> Window*` (forwarding wrapper), `SoftwareBackend::request_window` override. Returns a pointer (not a `WindowId`) because `WindowRegistry::add` needs a `Window&` to store — returning only an id with no way back to the window object it names would force every caller to invent its own lookup. `nullptr` means failure.

This task's cross-thread path cannot be exercised headlessly (needs a real SDL video driver — see
the design spec's accepted testing gap); this task's own deliverable is verified by compilation plus
the full existing `SoftwareBackend`-dependent test suite (`backend_base_tests`/`backend_tests` meson
groups) still passing, proving no regression to `create_window`/`run`/`submit`.

- [ ] **Step 1: Add the default virtual to `BackendBase` and forwarding wrapper to `Backend`**

```cpp
// include/prism/app/backend.hpp — add inside class BackendBase, after create_window
    virtual Window* request_window(WindowConfig cfg) { return &create_window(std::move(cfg)); }
```

```cpp
// include/prism/app/backend.hpp — add inside class Backend, after create_window
    Window* request_window(WindowConfig cfg) { return impl_->request_window(std::move(cfg)); }
```

- [ ] **Step 2: Add `PendingWindowRequest` and the queue member to `SoftwareBackend`**

```cpp
// include/prism/backends/software_backend.hpp
#include <prism/core/mpsc_queue.hpp>

#include <condition_variable>
#include <mutex>

// ... inside namespace prism::backends, before class SoftwareBackend:
struct PendingWindowRequest {
    WindowConfig cfg;
    Window* result = nullptr;
    bool done = false;
    std::mutex m;
    std::condition_variable cv;
};

class SoftwareBackend final : public BackendBase {
public:
    // ... existing members ...
    Window* request_window(WindowConfig cfg) override;

private:
    // ... existing members ...
    prism::core::mpsc_queue<std::shared_ptr<PendingWindowRequest>> window_requests_;
    void drain_window_requests();
};
```

- [ ] **Step 3: Implement `request_window` and `drain_window_requests`**

```cpp
// src/backends/software_backend.cpp

#include <chrono>

Window* SoftwareBackend::request_window(WindowConfig cfg) {
    auto req = std::make_shared<PendingWindowRequest>();
    req->cfg = cfg;
    window_requests_.push(req);
    wake();

    std::unique_lock lock(req->m);
    req->cv.wait_for(lock, std::chrono::seconds(2), [&] { return req->done; });
    return req->done ? req->result : nullptr;
}

void SoftwareBackend::drain_window_requests() {
    while (auto req_opt = window_requests_.pop()) {
        auto req = *req_opt;
        Window* result = nullptr;
        if (running_.load(std::memory_order_relaxed)) {
            auto id = ++next_id_;
            auto win = std::make_unique<SdlWindow>(id, req->cfg);
            win->ensure_created();
            SDL_StartTextInput(win->sdl_window());
            auto [it, _] = windows_.emplace(id, std::move(win));
            snapshots_[id];
            result = it->second.get();
        }
        {
            std::lock_guard lock(req->m);
            req->result = result;
            req->done = true;
        }
        req->cv.notify_one();
    }
}
```

- [ ] **Step 4: Drain the queue on `SDL_EVENT_USER` and once more after the loop exits**

```cpp
// src/backends/software_backend.cpp, inside run()'s switch statement:
            case SDL_EVENT_USER:
                drain_window_requests();
                break;
```

```cpp
// src/backends/software_backend.cpp, immediately after the while(running_...) loop ends,
// before run() returns — fails any request that arrived exactly at shutdown instead of
// leaving it to time out:
    }
    drain_window_requests();
}
```

- [ ] **Step 5: Build and run the full test suite**

```bash
ninja -C builddir
meson test -C builddir
```
Expected: identical pass count to the Task 2 baseline — this task adds no new headless-testable
behavior (see rationale above), only proves no regression.

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/backend.hpp include/prism/backends/software_backend.hpp src/backends/software_backend.cpp
git commit -m "feat: add BackendBase::request_window, cross-thread SoftwareBackend impl

Default virtual body (synchronous create_window) means the 10+ test-local
BackendBase subclasses need no changes. SoftwareBackend's override uses
the existing lock-free mpsc_queue + wake()/SDL_EVENT_USER to safely
create a window from the SDL-owning thread after run() has started,
bounded by a 2s timeout so a caller can never hang forever."
```

---

## Task 4: `TestBackend` multi-window support

**Files:**
- Modify: `include/prism/app/test_backend.hpp`
- Modify: `include/prism/app/null_backend.hpp` (no functional change — confirms it still compiles
  unmodified against the new default virtual)
- Test: extend `tests/test_test_backend.cpp`

**Interfaces:**
- Consumes: `BackendBase::request_window` default (Task 3, overridden here).
- Produces: `TestBackend::request_window(WindowConfig) -> Window*`, distinct windows per call, both
  reachable.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_test_backend.cpp — add at the end of the file

TEST_CASE("TestBackend::request_window returns a distinct window from create_window") {
    prism::TestBackend tb{{}};
    auto& primary = tb.create_window({});
    auto* second = tb.request_window({});

    REQUIRE(second != nullptr);
    CHECK(second->id() != primary.id());
}

TEST_CASE("TestBackend::request_window can be called before create_window") {
    prism::TestBackend tb{{}};
    auto* a = tb.request_window({});
    auto* b = tb.request_window({});
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->id() != b->id());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
ninja -C builddir
```
Expected: compile failure — `TestBackend` has no `request_window` override yet, so the default
(`&create_window(cfg)`) would actually compile and *pass* the first assertion trivially wrong (it
would return a pointer to the SAME window a fresh `create_window` call just overwrote, not a
distinct one) — confirm this by running before Step 3 and observing the first test case fails on
`CHECK(second->id() != primary.id())`.

- [ ] **Step 3: Convert `TestBackend` to id-keyed storage and add the override**

```cpp
// include/prism/app/test_backend.hpp
#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

#include <unordered_map>
#include <vector>

namespace prism::app {

class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
    std::unordered_map<WindowId, HeadlessWindow> windows_;
    WindowId next_id_ = 0;
    WindowId primary_id_ = 0;

public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    Window& create_window(WindowConfig cfg) override {
        auto id = ++next_id_;
        auto [it, _] = windows_.emplace(id, HeadlessWindow{id, cfg});
        primary_id_ = id;
        return it->second;
    }

    Window* request_window(WindowConfig cfg) override {
        auto id = ++next_id_;
        auto [it, _] = windows_.emplace(id, HeadlessWindow{id, cfg});
        return &it->second;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(WindowEvent{primary_id_, ev});
        event_cb(WindowEvent{primary_id_, WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism::app
```

- [ ] **Step 4: Build and run — verify new tests pass and existing ones are unaffected**

```bash
ninja -C builddir
meson test -C builddir test_backend
```
Expected: all test cases in `test_test_backend.cpp` pass, including the 3 pre-existing ones
(`fires events then WindowClose`, `with no events`, `works through Backend wrapper`) — these prove
`primary_id_`-based `run()` forwarding is behaviorally identical to the old hardcoded-`window_.id()`
forwarding for the single-`create_window`-call case every existing test uses.

- [ ] **Step 5: Confirm `NullBackend` still compiles unmodified**

```bash
ninja -C builddir
```
Expected: succeeds — `NullBackend` inherits `BackendBase::request_window`'s default body unchanged,
no source edit needed. (If this doesn't compile, `NullBackend` derivation must be checked — it
should not require any change.)

- [ ] **Step 6: Run the full suite**

```bash
meson test -C builddir
```
Expected: identical pass count plus the 2 new test cases from Step 1.

- [ ] **Step 7: Commit**

```bash
git add include/prism/app/test_backend.hpp tests/test_test_backend.cpp
git commit -m "feat: give TestBackend real multi-window storage for request_window

Preserves existing single-window behavior byte-for-byte (primary_id_ is
set by create_window exactly as the old hardcoded window_.id() was)."
```

---

## Task 5: Refactor `model_app()` to always route through `WindowRegistry`

The core rewrite. No new public behavior yet (that's Task 6) — this task's job is proving the
registry-of-one refactor is behaviorally identical to today for every existing single-window app.

**Files:**
- Modify: `include/prism/app/model_app.hpp`

**Interfaces:**
- Consumes: `WindowRegistry` (Task 2), `event_routing.hpp` (Task 1).
- Produces: `model_app()` — same three public overloads, same signatures, same observable behavior.

- [ ] **Step 1: Record the pre-refactor baseline**

```bash
meson test -C builddir 2>&1 | tail -5
```
Note the exact pass count (e.g. "Ok: 187"). This is the number every subsequent step in this task
must match exactly.

- [ ] **Step 2: Rewrite the 2-arg `model_app()` overload**

Replace the entire body of the `Backend&, Window&, Model&` overload in `model_app.hpp` with:

```cpp
template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    WindowRegistry registry;
    WindowId primary_id = registry.add(window, model);

    AnimationClock anim_clock;
    bool tick_scheduled = false;

    auto publish_entry = [&](WindowRegistry::Entry& entry, WindowId id) {
        entry.current_snap = std::shared_ptr<const SceneSnapshot>(
            entry.tree->build_snapshot(static_cast<float>(entry.width),
                                        static_cast<float>(entry.height),
                                        ++entry.version));
        backend.submit(id, entry.current_snap);
        backend.wake();
        entry.tree->clear_dirty();
    };

    auto publish_dirty = [&] {
        registry.for_each_dirty([&](WindowId id, WindowRegistry::Entry& entry) {
            publish_entry(entry, id);
        });
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
                publish_dirty();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            WindowId wid = we.window;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev, wid] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            loop.finish();
                        } else {
                            registry.remove(wid);
                        }
                        return;
                    }

                    auto* entry = registry.find(wid);
                    if (!entry) return;

                    bool needs_publish = false;
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        entry->width = resize->width;
                        entry->height = resize->height;
                        needs_publish = true;
                    }
                    if (entry->current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(*entry->tree, *entry->current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(*entry->tree, *entry->current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(*entry->tree, *entry->current_snap, *ms);
                    }
                    if (auto* kp = std::get_if<KeyPress>(&ev))
                        detail::route_key_press(*entry->tree, ev, *kp);
                    if (std::get_if<TextInput>(&ev))
                        detail::route_text_input(*entry->tree, ev);

                    entry->tree->drain_shared();
                    if (entry->tree->any_dirty() || needs_publish)
                        publish_entry(*entry, wid);
                    schedule_tick();
                })
            );
        });
    });

    backend.wait_ready();
    registry.for_each([&](WindowId id, WindowRegistry::Entry& entry) {
        publish_entry(entry, id);
    });

    auto ctx = AppContext(sched, anim_clock, window);
    if (setup) {
        setup(ctx);
        schedule_tick();
    }

    loop.run();

    backend.quit();
    backend_thread.join();
}
```

Note: `AppContext`'s construction is unchanged in this task — Task 6 adds the new accessors. This
keeps the two concerns (internal routing refactor vs. new public surface) separately reviewable.

Also add the include:
```cpp
#include <prism/app/window_registry.hpp>
```

- [ ] **Step 3: Build and run the full test suite — must match the Step 1 baseline exactly**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: identical pass count to Step 1's baseline. If anything fails, this is a genuine regression
in the refactor — do not proceed until every existing test passes again. Pay particular attention to
`tests/test_model_app.cpp` (resize/click/drag/slider/shared-state tests) and `tests/test_ui.cpp`,
since these exercise every branch touched by this rewrite.

- [ ] **Step 4: Commit**

```bash
git add include/prism/app/model_app.hpp
git commit -m "refactor: route model_app()'s dispatch/publish through WindowRegistry

Registry-of-one always — no behavior change for existing single-window
apps (proven by the full existing test suite passing unmodified). Lays
the groundwork for a second window without generalizing model_app()'s
public API."
```

---

## Task 6: `AppContext` primitives — `registry()`, `backend()`, `set_global_key_handler()`

**Files:**
- Modify: `include/prism/app/model_app.hpp`
- Test: `tests/test_model_app.cpp` (add new cases)

**Interfaces:**
- Produces: `AppContext::registry() -> WindowRegistry&`, `AppContext::backend() -> Backend&`,
  `AppContext::set_global_key_handler(std::function<void(const KeyPress&)>)`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_model_app.cpp — add near the other setup-callback tests

TEST_CASE("model_app exposes registry() and backend() via AppContext") {
    struct CapturingBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    Model model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>()};
    auto& window = backend.create_window({});

    bool saw_registry = false, saw_backend = false;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* entry = ctx.registry().find(window.id());
        saw_registry = (entry != nullptr);
        ctx.backend(); // must be callable — presence check
        saw_backend = true;
    });

    CHECK(saw_registry);
    CHECK(saw_backend);
}

TEST_CASE("model_app's global key handler fires before per-window dispatch") {
    struct CapturingBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    Model model;
    auto backend = prism::Backend{std::make_unique<CapturingBackend>()};
    auto& window = backend.create_window({});

    int handler_calls = 0;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx.set_global_key_handler([&](const prism::KeyPress&) { ++handler_calls; });
    });

    CHECK(handler_calls == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`ctx.registry()`/`ctx.backend()`/`ctx.set_global_key_handler` don't exist
yet).

- [ ] **Step 3: Extend `AppContext` and wire the handler into dispatch**

```cpp
// model_app.hpp — replace the AppContext class body
class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler)
        : sched_(s), clock_(&c), window_(&w), backend_(&b), registry_(&r),
          key_handler_(&key_handler) {}

    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }
    Backend& backend() { return *backend_; }
    WindowRegistry& registry() { return *registry_; }
    void set_global_key_handler(std::function<void(const KeyPress&)> fn) {
        *key_handler_ = std::move(fn);
    }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
    Backend* backend_;
    WindowRegistry* registry_;
    std::function<void(const KeyPress&)>* key_handler_;
};
```

```cpp
// model_app.hpp — inside the 2-arg model_app() overload, declare alongside anim_clock:
    std::function<void(const KeyPress&)> global_key_handler;
```

```cpp
// model_app.hpp — in the dispatch lambda, change:
                    if (auto* kp = std::get_if<KeyPress>(&ev))
                        detail::route_key_press(*entry->tree, ev, *kp);
// to:
                    if (auto* kp = std::get_if<KeyPress>(&ev)) {
                        if (global_key_handler) global_key_handler(*kp);
                        detail::route_key_press(*entry->tree, ev, *kp);
                    }
```

```cpp
// model_app.hpp — update the AppContext construction:
    auto ctx = AppContext(sched, anim_clock, window, backend, registry, global_key_handler);
```

- [ ] **Step 4: Build and run — new tests pass, full suite unaffected**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: Task 5's baseline count + 2 new passing test cases.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "feat: expose AppContext::registry()/backend()/set_global_key_handler()

Generic primitives only — no hotkey, no debug-tools concept here. Lets
a caller (outside this plan's scope) safely attach a second window and
intercept key presses globally."
```

---

## Task 7: Secondary-window close semantics + shutdown-race test

**Files:**
- Test: `tests/test_model_app.cpp` (add new cases)

**Interfaces:**
- Consumes: everything from Tasks 1-6, including `request_window`'s corrected `Window*` return type.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_model_app.cpp — add near the other model_app tests

TEST_CASE("closing a secondary window removes it from the registry without quitting") {
    struct TwoWindowBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;

        Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{secondary_id_, prism::WindowClose{}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    static Model second_model; // must outlive the setup() closure; WidgetTree only stores a
                                // reference to the model it was constructed from
    Model model;
    auto backend = prism::Backend{std::make_unique<TwoWindowBackend>()};
    auto& window = backend.create_window({});

    bool registry_had_secondary_before_its_close = false;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* second_window = ctx.backend().request_window({});
        REQUIRE(second_window != nullptr);
        auto second_id = ctx.registry().add(*second_window, second_model);
        registry_had_secondary_before_its_close = (ctx.registry().find(second_id) != nullptr);
    });

    CHECK(registry_had_secondary_before_its_close);
    // Reaching this line at all (no hang) proves the secondary window's WindowClose did
    // not call loop.finish() — only the primary window's did, per Task 5's implementation.
    // (A regression to "every WindowClose quits" would hang this test forever, since the
    // secondary's WindowClose is delivered first and the test scheduler is synchronous —
    // confirm this by temporarily reverting Task 5's `if (wid == primary_id)` guard to
    // unconditional loop.finish() and observing model_app() returns *before* the primary's
    // WindowClose is even processed, which the assertion above does not by itself catch;
    // this comment documents the risk for the implementer, not an automated check.)
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
ninja -C builddir
```
Expected: compiles and passes trivially if Task 5's `if (wid == primary_id)` distinction is already
correct (it was implemented as part of Task 5's rewrite) — this test's job is to be a permanent
regression guard, not to discover new behavior. Confirm it's actually exercising the guard by
temporarily changing Task 5's code to unconditional `loop.finish()` and re-running: expect this test
to still terminate (both branches call `loop.finish()` eventually) but `meson test` overall to now
hang or misbehave on the *next* test that relies on a window staying open — if nothing breaks, add a
second assertion that would catch it (e.g. counting `WindowClose` deliveries the app-level setup
observes). Revert the temporary change before continuing.

- [ ] **Step 3: Build and run**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: Task 6's baseline + 1 new passing test.

- [ ] **Step 4: Write the `SoftwareBackend` shutdown-race test**

This needs the real `SoftwareBackend`, so it belongs in a new headless-safe test that exercises only
`request_window`'s timeout path, not real SDL windowing:

```cpp
// tests/test_software_backend_request_window.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>

#include <chrono>

TEST_CASE("SoftwareBackend::request_window times out rather than hanging forever "
          "if nothing ever drains the queue") {
    // No run() thread is started, so the request is never drained — this proves the
    // bounded wait_for(2s) fires rather than blocking indefinitely.
    prism::backends::SoftwareBackend backend{{}};
    auto start = std::chrono::steady_clock::now();
    auto* win = backend.request_window({});
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(win == nullptr);
    CHECK(elapsed < std::chrono::seconds(5)); // bounded, not infinite
}
```

Register it in `tests/meson.build`'s `backend_base_tests` dict:
```meson
  'software_backend_request_window' : files('test_software_backend_request_window.cpp'),
```

Note: this links `prism_software_backend_dep`, so it will call `SDL_Init`-adjacent code paths
indirectly through `SoftwareBackend`'s constructor/destructor — confirm this is safe to run
headlessly in this environment before relying on it in CI (matches the existing
`backend_base_tests` group's assumptions, which already do this for `test_null_backend.cpp`/
`test_test_backend.cpp`).

- [ ] **Step 5: Build and run**

```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir software_backend_request_window
```
Expected: 1 test case, `win == nullptr` and bounded elapsed time.

- [ ] **Step 6: Run the full suite one final time**

```bash
meson test -C builddir 2>&1 | tail -5
```
Read the actual pass/fail count and exit code — this is the final verification for the whole plan.

- [ ] **Step 7: Commit**

```bash
git add tests/test_model_app.cpp tests/test_software_backend_request_window.cpp tests/meson.build
git commit -m "test: cover secondary-window close semantics and request_window's shutdown timeout"
```
