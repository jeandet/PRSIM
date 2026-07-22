# Cursor Shape API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a backend-agnostic `CursorShape` enum and `Window::set_cursor()` so `Handle` dividers, text-entry widgets, and custom-chrome window edges all show the right OS mouse cursor.

**Architecture:** `CursorShape` lives in a new, dependency-free `prism::ui` header so both widget code and backend code can use it without a layering inversion. Two independent resolvers push to the same `Window::set_cursor()`: `WidgetTree::desired_cursor()` (captured-widget priority, else hovered-widget, else `Default`) drives widget content from `model_app` on the main thread; `WindowChrome::cursor_for(HitZone)` drives window chrome from `SoftwareBackend::run()` on the backend thread. They never contend for the same frame because chrome zones stop forwarding `MouseMove` to the app — each layer owns a mutually exclusive region of the window.

**Tech Stack:** C++26, SDL3 (`SDL_SystemCursor`/`SDL_CreateSystemCursor`/`SDL_SetCursor`), Meson build, doctest for tests.

## Global Constraints

- Full spec: `docs/superpowers/specs/2026-07-22-cursor-shape-api-design.md` — consult it for the "why" behind any task if a step feels underspecified.
- TDD: every task writes a failing test before the implementation, per this repo's workflow rule.
- Every new test file gets added to `tests/meson.build`'s `headless_tests` map (or the matching dict for SDL-dependent tests), alphabetically placed.
- Follow this repo's existing patterns exactly — mirror `ScrollState`/`ScrollbarDrag`/existing `Widget<T>` idioms rather than inventing new ones.
- No comment-decorated code blocks; comments only justify non-obvious decisions.
- Run the full test suite before every commit and read the actual pass/fail count and exit code — don't infer success from a partial grep. A task's commit must always leave `meson test -C builddir` fully green — never split a change across commits in a way that leaves the build broken in between (this is why Task 3 below does both `Window` implementers in one task instead of two).

---

## Task 1: `CursorShape` enum, `WidgetVisualState::cursor`, `Handle`'s resize cursor, `WidgetTree::desired_cursor()`

**Files:**
- Create: `include/prism/ui/cursor.hpp`
- Modify: `include/prism/ui/delegate.hpp:24-28` (`WidgetVisualState`)
- Modify: `include/prism/app/widget_tree.hpp:187-205` (`wire_split_handles`'s `build_widget` closure), add `desired_cursor()` near `hovered_id()`/`captured_id()` (`:748` / `:861`)
- Test: `tests/test_cursor.cpp` (**new**)
- Modify: `tests/meson.build` (register `test_cursor.cpp` in `headless_tests`, alphabetically before `debug_name`)

**Interfaces:**
- Produces: `prism::ui::CursorShape` (enum class: `Default, Text, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE`) — consumed by every later task. `WidgetVisualState::cursor` (`CursorShape`, default `Default`) — consumed by Task 2. `WidgetTree::desired_cursor() const -> CursorShape` — consumed by Task 4.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cursor.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/layout.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/app/event_routing.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;

struct TwoPaneRowModel {
    Field<int> a{0};
    Field<int> b{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

struct TwoPaneColumnModel {
    Field<int> a{0};
    Field<int> b{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

struct BareHandleModel {
    Field<int> a{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(a);
            vb.handle();
        });
    }
};

TEST_CASE("desired_cursor is Default when nothing is hovered or captured") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    tree.build_snapshot(406, 100, 1);
    CHECK(tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("Hovering a Handle in an hstack reports ResizeEW") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);
}

TEST_CASE("Hovering a Handle in a vstack reports ResizeNS") {
    TwoPaneColumnModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(200, 66, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::ResizeNS);
}

TEST_CASE("A bare, unwired handle() outside a split container has no cursor override") {
    BareHandleModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(300, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];

    MouseMove move{handle_rect.center()};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    CHECK(tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("Capturing a Handle keeps its resize cursor even after the mouse leaves its rect") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto handle_center = handle_rect.center();

    MouseButton press{handle_center, 1, true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);
    REQUIRE(tree.captured_id() == handle_id);
    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);

    MouseMove move{Point{X{handle_center.x.raw() + 60.f}, handle_center.y}};
    prism::app::detail::route_mouse_move(tree, *snap, move);
    CHECK(tree.desired_cursor() == CursorShape::ResizeEW);
}

TEST_CASE("Releasing a captured Handle off its rect falls back to whatever is now hovered") {
    TwoPaneRowModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto handle_center = handle_rect.center();

    MouseButton press{handle_center, 1, true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);

    Point release_pos{pane0_rect.center()};
    MouseMove move{release_pos};
    prism::app::detail::route_mouse_move(tree, *snap, move);
    MouseButton release{release_pos, 1, false};
    InputEvent release_ev{release};
    prism::app::detail::route_mouse_button(tree, *snap, release_ev, release);

    REQUIRE(tree.captured_id() == 0);
    CHECK(tree.desired_cursor() == CursorShape::Default);
}
```

Register it in `tests/meson.build`'s `headless_tests` map (alphabetically, right before `'debug_name'`):

```meson
  'connection' : files('test_connection.cpp'),
  'cursor' : files('test_cursor.cpp'),
  'debug_name' : files('test_debug_name.cpp'),
```

(Note: the map isn't fully alphabetical today — just slot it next to the closest neighbors as shown; don't reorder unrelated entries.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `meson test -C builddir cursor -v`
Expected: FAIL to compile — no `CursorShape`, no `WidgetTree::desired_cursor`.

- [ ] **Step 3: Create the `CursorShape` enum**

Create `include/prism/ui/cursor.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace prism::ui {

enum class CursorShape : uint8_t { Default, Text, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE };

} // namespace prism::ui
```

- [ ] **Step 4: Add `WidgetVisualState::cursor`**

In `include/prism/ui/delegate.hpp`, add the include near the top (alongside the other `#include`s before `namespace prism::ui {`):

```cpp
#include <prism/ui/cursor.hpp>
```

Then change `WidgetVisualState`:

```cpp
struct WidgetVisualState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    CursorShape cursor = CursorShape::Default;
};
```

- [ ] **Step 5: Set the cursor in `wire_split_handles`**

In `include/prism/app/widget_tree.hpp`, `wire_split_handles`'s `build_widget` closure (the real, draggable handle — not the bare `handle()` a few lines below it):

```cpp
                child.build_widget = [&tree = tree_, container_id, index, vertical](WidgetNode& wn) {
                    wn.focus_policy = FocusPolicy::none;
                    wn.visual_state.cursor = vertical ? CursorShape::ResizeNS : CursorShape::ResizeEW;
                    wn.record = &draw_divider;
```

Leave the bare `handle()` method (`h.build_widget = [](WidgetNode& wn) { ... }`, a few lines below) untouched — it has no known axis.

- [ ] **Step 6: Add `WidgetTree::desired_cursor()`**

In `include/prism/app/widget_tree.hpp`, add right after `[[nodiscard]] WidgetId captured_id() const { return captured_id_; }` (near line 748):

```cpp
    [[nodiscard]] CursorShape desired_cursor() const {
        WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
        if (active == 0) return CursorShape::Default;
        auto it = index_.find(active);
        return it != index_.end() ? it->second->visual_state.cursor : CursorShape::Default;
    }
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `meson test -C builddir cursor -v`
Expected: PASS (6 test cases)

- [ ] **Step 8: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code)

- [ ] **Step 9: Commit**

```bash
git add include/prism/ui/cursor.hpp include/prism/ui/delegate.hpp \
        include/prism/app/widget_tree.hpp tests/test_cursor.cpp tests/meson.build
git commit -m "feat(cursor): add CursorShape and WidgetTree::desired_cursor(), wire Handle's resize cursor"
```

---

## Task 2: `TextField`/`Password`/`TextArea` report an I-beam cursor

**Files:**
- Modify: `include/prism/delegates/text_delegates.hpp:399-404` (`Widget<TextField<T>>::record`), `:424-429` (`Widget<Password<T>>::record`), `:449-453` (`Widget<TextArea<T>>::record`)
- Test: `tests/test_text_field.cpp`, `tests/test_password.cpp`, `tests/test_text_area.cpp`

**Interfaces:**
- Consumes: `CursorShape::Text` (Task 1), `WidgetVisualState::cursor` (Task 1).
- Produces: nothing new consumed by later tasks — this is a leaf integration.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_text_field.cpp`, in the `// --- record() tests ---` section:

```cpp
TEST_CASE("TextField record sets Text cursor shape") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextField<>>::record(dl, field, node);

    CHECK(node.visual_state.cursor == prism::CursorShape::Text);
}
```

Add to `tests/test_password.cpp`, in the `// --- record() tests ---` section:

```cpp
TEST_CASE("Password record sets Text cursor shape") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::Password<>>::record(dl, field, node);

    CHECK(node.visual_state.cursor == prism::CursorShape::Text);
}
```

Add to `tests/test_text_area.cpp`, in the `// --- record() tests ---` section:

```cpp
TEST_CASE("TextArea record sets Text cursor shape") {
    prism::Field<prism::TextArea<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TextArea<>>::record(dl, field, node);

    CHECK(node.visual_state.cursor == prism::CursorShape::Text);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `meson test -C builddir text_field password text_area -v`
Expected: FAIL — `node.visual_state.cursor` is `CursorShape::Default`, not `CursorShape::Text`.

- [ ] **Step 3: Set the cursor in all three `record()` methods**

In `include/prism/delegates/text_delegates.hpp`:

```cpp
template <StringLike T>
void Widget<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field,
                                    WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return v; });
}
```

```cpp
template <StringLike T>
void Widget<Password<T>>::record(DrawList& dl, const Field<Password<T>>& field,
                                    WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return detail::mask_string(v.size()); });
}
```

```cpp
template <StringLike T>
void Widget<TextArea<T>>::record(DrawList& dl, const Field<TextArea<T>>& field,
                                   WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    detail::text_area_record(dl, field, node);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `meson test -C builddir text_field password text_area -v`
Expected: PASS

- [ ] **Step 5: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code)

- [ ] **Step 6: Commit**

```bash
git add include/prism/delegates/text_delegates.hpp \
        tests/test_text_field.cpp tests/test_password.cpp tests/test_text_area.cpp
git commit -m "feat(cursor): TextField/Password/TextArea report an I-beam cursor"
```

---

## Task 3: `Window::set_cursor` virtual, implemented by `HeadlessWindow` and `SdlWindow`

**Files:**
- Modify: `include/prism/app/window.hpp` (add include, `using namespace prism::ui;`, pure virtual)
- Modify: `include/prism/app/headless_window.hpp` (override + `cursor()` getter + field)
- Modify: `include/prism/backends/sdl_window.hpp` (declare override)
- Modify: `src/backends/sdl_window.cpp` (implement, with a lazily-cached shape→`SDL_Cursor*` table)
- Test: `tests/test_window.cpp`, `tests/test_sdl_window.cpp` (**new**)
- Modify: `tests/meson.build` (register `test_sdl_window.cpp` as a standalone SDL-linked executable, mirroring `test_software_backend_request_window`)

**Interfaces:**
- Consumes: `CursorShape` (Task 1).
- Produces: `Window::set_cursor(CursorShape) = 0` — consumed by Task 4. `HeadlessWindow::cursor() const -> CursorShape` — consumed by Task 4's test.

`Window` has exactly two implementers (`HeadlessWindow`, `SdlWindow`), and adding a pure virtual makes both abstract-and-non-instantiable until they override it — so this is one task, not two: the build doesn't compile again until both are done, and a task's commit must never leave the build broken (see Global Constraints).

- [ ] **Step 1: Write the failing `HeadlessWindow` tests**

Add to `tests/test_window.cpp`, after the existing `HeadlessWindow` test cases:

```cpp
TEST_CASE("HeadlessWindow defaults to Default cursor") {
    prism::HeadlessWindow w(1, {});
    CHECK(w.cursor() == prism::CursorShape::Default);
}

TEST_CASE("HeadlessWindow set_cursor stores the shape") {
    prism::HeadlessWindow w(1, {});
    w.set_cursor(prism::CursorShape::Text);
    CHECK(w.cursor() == prism::CursorShape::Text);
}
```

- [ ] **Step 2: Run the `window` test target to verify it fails**

Run: `meson test -C builddir window -v`
Expected: FAIL to compile — `HeadlessWindow` has no member `cursor`/`set_cursor`. (This target doesn't touch `SdlWindow`, so it builds/fails in isolation from the rest of the suite.)

- [ ] **Step 3: Add the pure virtual to `Window`**

In `include/prism/app/window.hpp`, change the includes and `using` declarations:

```cpp
#include <prism/input/input_event.hpp>
#include <prism/ui/cursor.hpp>

#include <cstdint>
#include <string_view>
#include <utility>

namespace prism::app {
using namespace prism::input;
using namespace prism::ui;
```

Then add the virtual at the end of the `Window` class, right after `virtual void close() = 0;`:

```cpp
    virtual void close() = 0;

    virtual void set_cursor(CursorShape shape) = 0;
};
```

- [ ] **Step 4: Implement it in `HeadlessWindow`**

In `include/prism/app/headless_window.hpp`, add the override and change the private section:

```cpp
    void minimize() override {}
    void maximize() override {}
    void restore() override {}
    void show() override {}
    void hide() override {}
    void close() override {}

    void set_cursor(CursorShape shape) override { cursor_ = shape; }
    CursorShape cursor() const { return cursor_; }

private:
    WidgetId id_;
    std::string title_;
    int w_, h_;
    int x_ = 0, y_ = 0;
    bool resizable_;
    bool fullscreen_;
    DecorationMode decoration_;
    CursorShape cursor_ = CursorShape::Default;
};
```

(This replaces the existing block — only the `set_cursor`/`cursor()` methods and the `cursor_` field are new, the rest is unchanged.)

- [ ] **Step 5: Run the `window` test target to verify it passes**

Run: `meson test -C builddir window -v`
Expected: PASS

- [ ] **Step 6: Write the failing `SdlWindow` test**

Create `tests/test_sdl_window.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_window.hpp>

#include <SDL3/SDL.h>

TEST_CASE("SdlWindow::set_cursor applies every CursorShape without crashing") {
    SDL_Init(SDL_INIT_VIDEO);
    prism::backends::SdlWindow win(1, {.title = "Test", .width = 100, .height = 100});
    win.ensure_created();

    win.set_cursor(prism::ui::CursorShape::Default);
    win.set_cursor(prism::ui::CursorShape::Text);
    win.set_cursor(prism::ui::CursorShape::ResizeNS);
    win.set_cursor(prism::ui::CursorShape::ResizeEW);
    win.set_cursor(prism::ui::CursorShape::ResizeNESW);
    win.set_cursor(prism::ui::CursorShape::ResizeNWSE);

    CHECK(true); // SDL exposes no queryable "current cursor" — absence of a crash is the assertion,
                 // matching the existing precedent in tests/test_app.cpp for real-SDL side effects.
    SDL_Quit();
}
```

Register it in `tests/meson.build`, right after the existing `test_software_backend_request_window` block (same standalone style, not inside a `foreach` dict):

```meson
test_sdl_window = executable('test_sdl_window',
  files('test_sdl_window.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('sdl_window', test_sdl_window)
```

- [ ] **Step 7: Run the `sdl_window` test target to verify it fails**

Run: `meson test -C builddir sdl_window -v`
Expected: FAIL to compile — `SdlWindow` is abstract (doesn't override `set_cursor` yet), so it can't be constructed.

- [ ] **Step 8: Declare and implement `SdlWindow::set_cursor`**

In `include/prism/backends/sdl_window.hpp`, add after `void close() override;`:

```cpp
    void close() override;

    void set_cursor(CursorShape shape) override;

    // Backend-internal access
```

In `src/backends/sdl_window.cpp`, add after `SdlWindow::close()`:

```cpp
namespace {

SDL_Cursor* sdl_cursor_for(CursorShape shape) {
    static SDL_Cursor* cache[6] = {};
    static constexpr SDL_SystemCursor system_cursors[] = {
        SDL_SYSTEM_CURSOR_DEFAULT,     // Default
        SDL_SYSTEM_CURSOR_TEXT,        // Text
        SDL_SYSTEM_CURSOR_NS_RESIZE,   // ResizeNS
        SDL_SYSTEM_CURSOR_EW_RESIZE,   // ResizeEW
        SDL_SYSTEM_CURSOR_NESW_RESIZE, // ResizeNESW
        SDL_SYSTEM_CURSOR_NWSE_RESIZE, // ResizeNWSE
    };
    auto index = static_cast<size_t>(shape);
    if (!cache[index]) cache[index] = SDL_CreateSystemCursor(system_cursors[index]);
    return cache[index];
}

} // namespace

void SdlWindow::set_cursor(CursorShape shape) {
    if (auto* c = sdl_cursor_for(shape)) SDL_SetCursor(c);
}
```

The cache is a function-local `static` (not a per-`SdlWindow` member) because SDL cursors are process-global — this avoids re-creating the same six `SDL_Cursor*` once per window when multiple windows exist (e.g. the debug inspector's secondary window).

- [ ] **Step 9: Run the `sdl_window` test target to verify it passes**

Run: `meson test -C builddir sdl_window -v`
Expected: PASS

- [ ] **Step 10: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code) — this is the first point since Step 3 where the whole suite builds again.

- [ ] **Step 11: Commit**

```bash
git add include/prism/app/window.hpp include/prism/app/headless_window.hpp \
        include/prism/backends/sdl_window.hpp src/backends/sdl_window.cpp \
        tests/test_window.cpp tests/test_sdl_window.cpp tests/meson.build
git commit -m "feat(cursor): add Window::set_cursor, implemented by HeadlessWindow and SdlWindow"
```

---

## Task 4: `model_app` pushes `desired_cursor()` to the window

**Files:**
- Modify: `include/prism/app/window_registry.hpp:15-22` (`Entry`)
- Modify: `include/prism/app/model_app.hpp:159-168` (mouse-event dispatch block)
- Test: `tests/test_model_app.cpp`

**Interfaces:**
- Consumes: `WidgetTree::desired_cursor()` (Task 1), `Window::set_cursor()` (Task 3), `HeadlessWindow::cursor()` (Task 3).
- Produces: `WindowRegistry::Entry::last_cursor` — internal dedup state, not consumed elsewhere.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_model_app.cpp`:

```cpp
struct CursorTextFieldModel {
    prism::Field<prism::TextField<>> name{{.value = "hi"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(name);
    }
};

TEST_CASE("model_app pushes a hovered TextField's cursor to the window") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct HoverBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        prism::HeadlessWindow window_{0, {}};
        HoverBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
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

            cb(prism::WindowEvent{window_.id(), prism::MouseMove{rect.center()}});

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

    CursorTextFieldModel model;
    auto backend = prism::Backend{std::make_unique<HoverBackend>(latest_snap, snap_count)};
    auto& window = backend.create_window({.width = 800, .height = 600});
    prism::model_app(backend, window, model);

    CHECK(static_cast<prism::HeadlessWindow&>(window).cursor() == prism::CursorShape::Text);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `meson test -C builddir model_app -v`
Expected: FAIL — `window.cursor()` is `CursorShape::Default` (nothing pushes it yet).

- [ ] **Step 3: Add `last_cursor` to `WindowRegistry::Entry`**

In `include/prism/app/window_registry.hpp`:

```cpp
    struct Entry {
        Window* window = nullptr;
        std::unique_ptr<WidgetTree> tree;
        std::shared_ptr<const SceneSnapshot> current_snap;
        int width = 0;
        int height = 0;
        uint64_t version = 0;
        CursorShape last_cursor = CursorShape::Default;
    };
```

- [ ] **Step 4: Push the cursor after mouse-event dispatch in `model_app`**

In `include/prism/app/model_app.hpp`, extend the existing block:

```cpp
                    if (entry->current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(*entry->tree, *entry->current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(*entry->tree, *entry->current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(*entry->tree, *entry->current_snap, *ms);
                        if (auto shape = entry->tree->desired_cursor(); shape != entry->last_cursor) {
                            entry->last_cursor = shape;
                            entry->window->set_cursor(shape);
                        }
                    }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `meson test -C builddir model_app -v`
Expected: PASS

- [ ] **Step 6: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code)

- [ ] **Step 7: Commit**

```bash
git add include/prism/app/window_registry.hpp include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "feat(cursor): push WidgetTree's desired cursor to the window in model_app"
```

---

## Task 5: `WindowChrome::cursor_for(HitZone)`

**Files:**
- Modify: `include/prism/ui/window_chrome.hpp` (add include, `cursor_for` static)
- Test: `tests/test_window_chrome.cpp` (**new**)
- Modify: `tests/meson.build` (register in `headless_tests`, right after `'window_registry'`)

**Interfaces:**
- Consumes: `CursorShape` (Task 1), `WindowChrome::HitZone` (existing).
- Produces: `WindowChrome::cursor_for(HitZone) -> CursorShape` — consumed by Task 6.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_window_chrome.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/window_chrome.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using HZ = prism::WindowChrome::HitZone;

TEST_CASE("WindowChrome::cursor_for maps straight edges to axis-aligned resize shapes") {
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeN) == prism::CursorShape::ResizeNS);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeS) == prism::CursorShape::ResizeNS);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeE) == prism::CursorShape::ResizeEW);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeW) == prism::CursorShape::ResizeEW);
}

TEST_CASE("WindowChrome::cursor_for maps corners to the matching diagonal resize shape") {
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeNW) == prism::CursorShape::ResizeNWSE);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeSE) == prism::CursorShape::ResizeNWSE);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeNE) == prism::CursorShape::ResizeNESW);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeSW) == prism::CursorShape::ResizeNESW);
}

TEST_CASE("WindowChrome::cursor_for is Default for non-resize zones") {
    CHECK(prism::WindowChrome::cursor_for(HZ::Client) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::TitleBar) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Close) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Minimize) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Maximize) == prism::CursorShape::Default);
}
```

Register it in `tests/meson.build`'s `headless_tests` map, right after `'window_registry'`:

```meson
  'window' : files('test_window.cpp'),
  'window_registry' : files('test_window_registry.cpp'),
  'window_chrome' : files('test_window_chrome.cpp'),
  'theme' : files('test_theme.cpp'),
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `meson test -C builddir window_chrome -v`
Expected: FAIL to compile — `WindowChrome` has no member `cursor_for`.

- [ ] **Step 3: Add the include and the static method**

In `include/prism/ui/window_chrome.hpp`, add to the includes:

```cpp
#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/cursor.hpp>
```

Then add, right after `hit_test`'s closing brace (before `static void render(...)`):

```cpp
    static CursorShape cursor_for(HitZone zone) {
        switch (zone) {
            case HitZone::ResizeN:
            case HitZone::ResizeS:  return CursorShape::ResizeNS;
            case HitZone::ResizeE:
            case HitZone::ResizeW:  return CursorShape::ResizeEW;
            case HitZone::ResizeNW:
            case HitZone::ResizeSE: return CursorShape::ResizeNWSE;
            case HitZone::ResizeNE:
            case HitZone::ResizeSW: return CursorShape::ResizeNESW;
            default: return CursorShape::Default;
        }
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `meson test -C builddir window_chrome -v`
Expected: PASS

- [ ] **Step 5: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code)

- [ ] **Step 6: Commit**

```bash
git add include/prism/ui/window_chrome.hpp tests/test_window_chrome.cpp tests/meson.build
git commit -m "feat(cursor): map WindowChrome hit-zones to cursor shapes"
```

---

## Task 6: Resize cursors over custom-chrome window edges

**Files:**
- Modify: `include/prism/backends/sdl_window.hpp` (test-hook `cursor()` getter + `last_cursor_` field)
- Modify: `src/backends/sdl_window.cpp` (`set_cursor` also stores `last_cursor_`)
- Modify: `src/backends/software_backend.cpp:150-165` (`SDL_EVENT_MOUSE_MOTION` case — the actual behavior change)
- Test: `tests/test_software_backend_chrome_cursor.cpp` (**new**, standalone SDL-linked executable, mirrors `test_sdl_window`/`test_software_backend_request_window`)
- Modify: `tests/meson.build`

**Interfaces:**
- Consumes: `WindowChrome::cursor_for` (Task 5), `SdlWindow::set_cursor` (Task 3).
- Produces: `SdlWindow::cursor() const -> CursorShape` — a test-only observability hook (SDL exposes no queryable "current cursor," so this is what Step 1's tests assert against); not consumed by production code elsewhere.

`SoftwareBackend::run()`'s real SDL event loop has no existing test seam (`begin_resize`/`update_resize` — chrome resize itself — have none either, for the same reason). This task closes that gap for cursor behavior specifically by injecting real `SDL_Event` structs into the live event queue with `SDL_PushEvent` (documented thread-safe from any thread) while `run()` executes on a background thread, then polling the new `cursor()` hook for the expected shape. This exercises the actual `SDL_WaitEvent` → chrome-hit-test → `set_cursor` path, not a mock of it.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_software_backend_chrome_cursor.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

bool wait_for_cursor(prism::backends::SdlWindow& win, prism::CursorShape expected) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (win.cursor() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void push_motion(SDL_WindowID window_id, float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = window_id;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

} // namespace

TEST_CASE("SoftwareBackend sets a resize cursor when the mouse hovers a custom-chrome window edge") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread runner([&] { backend.run([](const prism::WindowEvent&) {}); });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 150.f); // left edge, well below the title bar
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeEW));

    backend.quit();
    runner.join();
}

TEST_CASE("SoftwareBackend sets a diagonal resize cursor when the mouse hovers a corner") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread runner([&] { backend.run([](const prism::WindowEvent&) {}); });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 2.f); // top-left corner
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeNWSE));

    backend.quit();
    runner.join();
}

TEST_CASE("SoftwareBackend forwards MouseMove and leaves the cursor alone when the mouse is over client content") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::atomic<int> mouse_move_count{0};
    std::thread runner([&] {
        backend.run([&](const prism::WindowEvent& we) {
            if (std::holds_alternative<prism::MouseMove>(we.event))
                mouse_move_count.fetch_add(1, std::memory_order_release);
        });
    });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 150.f); // left edge — chrome-owned, not forwarded
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeEW));

    push_motion(window_id, 150.f, 150.f); // client area — forwarded, cursor untouched by chrome
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (mouse_move_count.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    CHECK(mouse_move_count.load(std::memory_order_acquire) >= 1);
    CHECK(sdl_win.cursor() == prism::CursorShape::ResizeEW); // chrome layer never touched it back

    backend.quit();
    runner.join();
}
```

Register it in `tests/meson.build`, right after the `test_sdl_window` block added in Task 3 (same standalone style):

```meson
test_software_backend_chrome_cursor = executable('test_software_backend_chrome_cursor',
  files('test_software_backend_chrome_cursor.cpp'),
  dependencies : [prism_software_backend_dep, sdl3_dep, sdl3_ttf_dep, doctest_dep],
)
test('software_backend_chrome_cursor', test_software_backend_chrome_cursor)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `meson test -C builddir software_backend_chrome_cursor -v`
Expected: FAIL to compile — `SdlWindow` has no member `cursor`.

- [ ] **Step 3: Add the test-hook getter to `SdlWindow`**

In `include/prism/backends/sdl_window.hpp`, add the getter near `set_cursor` and the field near the other private members:

```cpp
    void set_cursor(CursorShape shape) override;
    CursorShape cursor() const { return last_cursor_; }
```

```cpp
    // Manual resize tracking
    WindowChrome::HitZone resize_zone_ = WindowChrome::HitZone::Client;
    float resize_start_x_ = 0, resize_start_y_ = 0;
    int resize_start_w_ = 0, resize_start_h_ = 0;
    CursorShape last_cursor_ = CursorShape::Default;
```

In `src/backends/sdl_window.cpp`, update `set_cursor` to record it:

```cpp
void SdlWindow::set_cursor(CursorShape shape) {
    last_cursor_ = shape;
    if (auto* c = sdl_cursor_for(shape)) SDL_SetCursor(c);
}
```

- [ ] **Step 4: Run the tests to verify they fail for the right reason**

Run: `meson test -C builddir software_backend_chrome_cursor -v`
Expected: now compiles, but FAILs on `wait_for_cursor` timing out — the chrome-hit-test behavior itself doesn't exist yet, so `cursor()` never leaves `Default`.

- [ ] **Step 5: Make the behavior change**

In `src/backends/software_backend.cpp`, replace the `SDL_EVENT_MOUSE_MOTION` case:

```cpp
            case SDL_EVENT_MOUSE_MOTION: {
                auto it_w = windows_.find(wid);
                if (it_w != windows_.end() && it_w->second->in_resize()) {
                    it_w->second->update_resize(
                        static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y));
                    break;
                }
                float my = ev.motion.y;
                if (it_w != windows_.end() &&
                    it_w->second->decoration_mode() == DecorationMode::Custom)
                    my -= WindowChrome::title_bar_h.raw();
                event_cb(WindowEvent{wid, MouseMove{Point{X{ev.motion.x}, Y{my}}}});
                break;
            }
```

with:

```cpp
            case SDL_EVENT_MOUSE_MOTION: {
                auto it_w = windows_.find(wid);
                if (it_w != windows_.end() && it_w->second->in_resize()) {
                    it_w->second->update_resize(
                        static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y));
                    break;
                }
                if (it_w != windows_.end() &&
                    it_w->second->decoration_mode() == DecorationMode::Custom) {
                    int ww, wh;
                    SDL_GetWindowSize(it_w->second->sdl_window(), &ww, &wh);
                    auto zone = WindowChrome::hit_test(
                        static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y), ww, wh);
                    if (zone != WindowChrome::HitZone::Client) {
                        it_w->second->set_cursor(WindowChrome::cursor_for(zone));
                        break;
                    }
                }
                float my = ev.motion.y;
                if (it_w != windows_.end() &&
                    it_w->second->decoration_mode() == DecorationMode::Custom)
                    my -= WindowChrome::title_bar_h.raw();
                event_cb(WindowEvent{wid, MouseMove{Point{X{ev.motion.x}, Y{my}}}});
                break;
            }
```

This does not change observable app-facing behavior: hovering the title bar/edge strip today already produces a `MouseMove` the app forwards but that hits nothing (coordinates fall outside any widget's rect, since content starts at local Y 0, below the title bar). The only change is that the app no longer receives that pointless event, and in exchange that motion event becomes the sole owner of the cursor for that position. When the mouse re-enters the client area, the very next motion event resumes forwarding and `desired_cursor()` (Task 4) reclaims the cursor — no explicit reset needed.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `meson test -C builddir software_backend_chrome_cursor -v`
Expected: PASS (3 test cases)

- [ ] **Step 7: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail count and exit code)

- [ ] **Step 8: Manual verification (if a display is available)**

Run any example with `DecorationMode::Custom` (the default — see `WindowConfig`), then hover the outermost ~6px of the window edge and each corner. Expect: edges show a two-headed arrow aligned with that edge (horizontal on left/right, vertical on top/bottom), corners show a diagonal two-headed arrow, and the cursor reverts to the normal pointer/I-beam/etc. as soon as it crosses back onto real window content. If no display is available in this environment, skip this step and note it explicitly rather than claiming it was verified. This step is a supplement to Step 6's real automated coverage, not a substitute for it.

- [ ] **Step 9: Commit**

```bash
git add include/prism/backends/sdl_window.hpp src/backends/sdl_window.cpp \
        src/backends/software_backend.cpp \
        tests/test_software_backend_chrome_cursor.cpp tests/meson.build
git commit -m "feat(cursor): show resize cursors over custom-chrome window edges"
```
