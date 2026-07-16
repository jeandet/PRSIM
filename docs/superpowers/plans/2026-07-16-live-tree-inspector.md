# Live Tree Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second debug window that shows the main window's live `WidgetTree` as an indented,
auto-refreshing list, with bidirectional click/hover highlighting between the two windows,
activated by Ctrl+Shift+I with zero app code required.

**Architecture:** Built entirely on sub-project 1's `WindowRegistry`/`AppContext` primitives and
sub-project 2's `on_row_click`. Three small, targeted extensions to already-merged core files
(`WidgetNode`/`Node` debug names, `WidgetTree` highlight injection, `AppContext` post-dispatch hook,
`SoftwareBackend` window teardown), then the tree-inspector-specific pieces (`NodeRow`,
`flatten_tree`, `TreeInspectorModel`, `TreeInspectorController`) on top.

**Tech Stack:** C++26, GCC 16 `-freflection`, Meson, doctest, SDL3 (only for `SoftwareBackend`'s
window-teardown override).

## Global Constraints

- Every new field/behavior that adds unconditional per-node or per-tree cost must be justified:
  `WidgetTree::highlight_id_` (one `std::optional<WidgetId>` per tree) is unconditionally compiled;
  `WidgetNode::debug_name`/`Node::debug_name` (a `std::string` per node) are gated behind
  `PRISM_DEBUG_TOOLS_ENABLED` since they add real per-node memory cost across every tree in every
  build.
- `debug_name` capture: use `identifier_of(m)` (no `has_identifier` guard needed — `m` is a genuine
  nonstatic-data-member reflection, always has an identifier) inside `build_node_tree`'s
  reflection-fallback branch, where the real member name is cheaply available. Everywhere else (the
  much more common `view()`-based path, and the reflection-fallback branch as a base case), use
  `std::meta::has_identifier(^^T) ? std::string(std::meta::identifier_of(^^T)) :
  std::string{}` inside `node_leaf`/`node_readonly_leaf` — mirroring the exact, GCC-16-verified
  pattern already used in `check_unplaced_fields` (`widget_tree.hpp`) for the type-reflection case,
  not inventing a new idiom.
- `mark_dirty_by_id(root_.id)` (already public on `WidgetTree`) is how any new code marks a tree
  dirty enough to force a republish — don't add a second "mark whole tree dirty" primitive.
- Build/verify with `meson test -C builddir` (or `ninja -C builddir test`) after every task; read
  the actual pass/fail count.
- Tasks 3 and 4 modify already-merged, already-reviewed code (`model_app.hpp`,
  `software_backend.cpp`) — treat their existing test suites as regression proof exactly as
  sub-project 1's plan did: record the baseline before each change, confirm it's unchanged after.

---

## Task 1: `debug_name` on `Node`/`WidgetNode`, build flag, key constants

**Files:**
- Create: `meson_options.txt` — wait, this already exists (created by sub-project 1's Task 6? — no,
  sub-project 1's spec proposed it but sub-project 1's actual shipped plan never included a Task for
  it, since the hotkey/build-flag ownership moved to this sub-project during the mid-plan
  correction). Verify whether `meson_options.txt` exists at the start of this task; create it if not.
- Modify: `meson.build` (root) — thread the option into `PRISM_DEBUG_TOOLS_ENABLED`.
- Modify: `include/prism/ui/node.hpp` — add `Node::debug_name`.
- Modify: `include/prism/ui/widget_node.hpp` — add `WidgetNode::debug_name`, populate it in
  `node_leaf<T>`/`node_readonly_leaf<T>`.
- Modify: `include/prism/app/widget_tree.hpp` — copy `debug_name` in `build_widget_node`, override
  it with the real member identifier in `build_node_tree`'s reflection-fallback branch.
- Modify: `include/prism/input/input_event.hpp` — add `mods::ctrl`, `keys::i`.
- Test: `tests/test_input_event.cpp`, a new `tests/test_debug_name.cpp`.

**Interfaces:**
- Produces: `PRISM_DEBUG_TOOLS_ENABLED` compile define; `WidgetNode::debug_name`/`Node::debug_name`
  (both `#ifdef`-gated `std::string`); `prism::input::mods::ctrl`, `prism::input::keys::i`.

- [ ] **Step 1: Check for and create `meson_options.txt` if needed**

```bash
ls meson_options.txt 2>/dev/null || echo "does not exist yet"
```

If it doesn't exist, create it:
```meson
# meson_options.txt
option('prism_debug_tools', type : 'feature', value : 'auto',
       description : 'Compile in the live debug-tools hotkey (Ctrl+Shift+I) and its supporting infrastructure')
```

- [ ] **Step 2: Wire the option into a compile define in root `meson.build`**

```meson
# meson.build (root), after the -freflection check block:
debug_tools_opt = get_option('prism_debug_tools')
if debug_tools_opt == 'auto'
  debug_tools_enabled = get_option('buildtype') != 'release'
else
  debug_tools_enabled = debug_tools_opt == 'enabled'
endif
if debug_tools_enabled
  add_project_arguments('-DPRISM_DEBUG_TOOLS_ENABLED', language : 'cpp')
endif
```

- [ ] **Step 3: Rebuild to confirm the define takes effect, write a trivial confirming test**

```bash
meson setup builddir --reconfigure
```

```cpp
// tests/test_debug_name.cpp (new file, headless)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

TEST_CASE("PRISM_DEBUG_TOOLS_ENABLED is defined by default (non-release build)") {
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    CHECK(true);
#else
    FAIL("expected PRISM_DEBUG_TOOLS_ENABLED to be defined in a non-release buildtype");
#endif
}
```
Register in `tests/meson.build`'s `headless_tests` dict: `'debug_name' : files('test_debug_name.cpp'),`.
Build and confirm it passes: `ninja -C builddir && meson test -C builddir debug_name`.

- [ ] **Step 4: Add `mods::ctrl` and `keys::i`**

```cpp
// include/prism/input/input_event.hpp, inside namespace keys, after the existing entries:
    inline constexpr int32_t i = 0x69;         // matches SDLK_I
```
```cpp
// inside namespace mods, after shift:
    inline constexpr uint16_t ctrl = 0x00C0;  // matches SDL_KMOD_CTRL
```

Add to `tests/test_input_event.cpp` (append near existing `mods::shift`/`keys::tab` checks, matching
the file's existing style — read it first to match the exact assertion pattern used there):
```cpp
TEST_CASE("mods::ctrl matches SDL_KMOD_CTRL value") {
    CHECK(prism::mods::ctrl == 0x00C0);
}
TEST_CASE("keys::i matches SDLK_I value") {
    CHECK(prism::keys::i == 0x69);
}
```

- [ ] **Step 5: Add `debug_name` to `Node`**

```cpp
// include/prism/ui/node.hpp
#include <string>   // add to the include block

// inside struct Node, after `bool is_leaf = false;`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    std::string debug_name;
#endif
```

- [ ] **Step 6: Add `debug_name` to `WidgetNode`, populate it in the leaf factory functions**

```cpp
// include/prism/ui/widget_node.hpp
#include <prism/core/reflect.hpp>   // add to the include block, for std::meta:: reflection guards

// inside struct WidgetNode, after `WidgetId id = 0;`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    std::string debug_name;
#endif
```

```cpp
// node_leaf<T> — add right after `n.is_leaf = true;`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
    n.debug_name = std::meta::has_identifier(^^T)
        ? std::string(std::meta::identifier_of(^^T)) : std::string{};
#endif
#endif
```

```cpp
// node_readonly_leaf<T, Observable> — same addition, right after `n.is_leaf = true;`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#if __cpp_impl_reflection
    n.debug_name = std::meta::has_identifier(^^T)
        ? std::string(std::meta::identifier_of(^^T)) : std::string{};
#endif
#endif
```

- [ ] **Step 7: Copy `debug_name` from `Node` to `WidgetNode` in `build_widget_node`, override with the real member name in the reflection-fallback branch**

```cpp
// include/prism/app/widget_tree.hpp, WidgetTree::build_widget_node, right after `wn.theme = &theme_;`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        wn.debug_name = node.debug_name;
#endif
```

```cpp
// build_node_tree's reflection-fallback branch (the `#if __cpp_impl_reflection ... else { ... }`
// block, `is_field_v<M>` case) — change from:
                } else if constexpr (is_field_v<M>) {
                    root.children.push_back(node_leaf(member, next_id_));
// to:
                } else if constexpr (is_field_v<M>) {
                    auto leaf = node_leaf(member, next_id_);
#ifdef PRISM_DEBUG_TOOLS_ENABLED
                    leaf.debug_name = std::string(std::meta::identifier_of(m));
#endif
                    root.children.push_back(std::move(leaf));
```

- [ ] **Step 8: Write a test proving the member-name override works**

```cpp
// tests/test_debug_name.cpp — add this TEST_CASE (only meaningful/compiled when both
// PRISM_DEBUG_TOOLS_ENABLED and __cpp_impl_reflection are defined; guard accordingly)
#if defined(PRISM_DEBUG_TOOLS_ENABLED) && __cpp_impl_reflection
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

namespace {
struct ReflectedOnlyModel {
    prism::Field<int> volume{0};
};
}

TEST_CASE("reflection-only model captures the real field name as debug_name") {
    ReflectedOnlyModel model;
    prism::WidgetTree tree(model);
    REQUIRE(!tree.root().children.empty());
    CHECK(tree.root().children[0].debug_name == "volume");
}
#endif
```

- [ ] **Step 9: Build and run the full suite**

```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir
```
Expected: all new test cases pass; no regressions elsewhere (this task adds fields/branches gated by
a flag that's already the default, so `debug_name` capture runs for every model built during the
existing test suite too — watch for any unexpected build failure in existing reflection-model tests,
which would indicate the `node_leaf`/`node_readonly_leaf` changes broke something).

- [ ] **Step 10: Commit**

```bash
git add meson_options.txt meson.build include/prism/ui/node.hpp include/prism/ui/widget_node.hpp \
        include/prism/app/widget_tree.hpp include/prism/input/input_event.hpp \
        tests/test_debug_name.cpp tests/test_input_event.cpp tests/meson.build
git commit -m "feat: add debug_name to Node/WidgetNode, PRISM_DEBUG_TOOLS_ENABLED build flag

Gated behind the new flag since a std::string per node is real
per-node memory cost across every tree in every build. Captured via
node_leaf/node_readonly_leaf (type name, covers .widget()/.list()/the
reflection-fallback path uniformly) with an override to the real
reflected member name in build_node_tree's reflection-fallback branch,
where it's cheaply available."
```

---

## Task 2: Highlight injection (`WidgetTree::set_debug_highlight`, `hovered_id()`, `build_snapshot()` step)

**Files:**
- Modify: `include/prism/app/widget_tree.hpp`
- Test: `tests/test_widget_tree.cpp`

**Interfaces:**
- Produces: `WidgetTree::set_debug_highlight(std::optional<WidgetId>)`, `WidgetTree::hovered_id()
  const -> WidgetId`.

- [ ] **Step 1: Write the failing tests**

Verified against this file's existing conventions: `SimpleModel` (`Field<int> count`, `Field<std::string>
name`, `view()` calling `vb.vstack(count, name)`), `tree.leaf_ids()`, and `tree.update_hover(ids[0])`
(existing "update_hover sets hovered state" tests use exactly this shape, `tests/test_widget_tree.cpp:254-298`
— no `MouseMove` dispatch needed, `update_hover` is the already-tested direct primitive).

```cpp
// tests/test_widget_tree.cpp — add near the end

TEST_CASE("hovered_id() reflects the currently hovered widget") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    CHECK(tree.hovered_id() == ids[0]);
}

TEST_CASE("hovered_id() is 0 when nothing is hovered") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.hovered_id() == 0);
}

TEST_CASE("set_debug_highlight injects a RectOutline at the target's rect on the next build_snapshot") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    prism::Rect target_rect;
    for (auto& [id, rect] : snap1->geometry)
        if (id == ids[0]) target_rect = rect;

    tree.set_debug_highlight(ids[0]);
    auto snap2 = tree.build_snapshot(400, 300, 2);

    bool found = false;
    for (auto& cmd : snap2->overlay.commands) {
        if (auto* ro = std::get_if<prism::RectOutline>(&cmd)) {
            if (ro->rect.origin.x.raw() == target_rect.origin.x.raw()
                && ro->rect.origin.y.raw() == target_rect.origin.y.raw())
                found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("set_debug_highlight(nullopt) clears the highlight") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());

    tree.set_debug_highlight(ids[0]);
    auto snap_highlighted = tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();
    bool found_when_set = false;
    for (auto& cmd : snap_highlighted->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found_when_set = true;
    REQUIRE(found_when_set);

    tree.set_debug_highlight(std::nullopt);
    auto snap_cleared = tree.build_snapshot(400, 300, 3);
    bool found_when_cleared = false;
    for (auto& cmd : snap_cleared->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found_when_cleared = true;
    CHECK_FALSE(found_when_cleared);
}

TEST_CASE("set_debug_highlight with a nonexistent id injects nothing and does not crash") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    tree.set_debug_highlight(999999);
    auto snap = tree.build_snapshot(400, 300, 1);
    bool found = false;
    for (auto& cmd : snap->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found = true;
    CHECK_FALSE(found);
}
```

Note: the "clears the highlight" and "nonexistent id" tests scan for *any* `RectOutline`, not a
specific one — if `SimpleModel`'s own widgets never draw a `RectOutline` themselves (they don't:
`int`/`std::string` fields render as plain text/numeric widgets, no focus ring drawn without an
actual focus dispatch), this is unambiguous. If a future edit to this test file changes `SimpleModel`
in a way that adds its own `RectOutline` (e.g. by giving it a focused widget), tighten these
assertions to match on the specific highlighted rect instead, as the first test already does.

- [ ] **Step 2: Run tests to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`set_debug_highlight`/`hovered_id` don't exist yet).

- [ ] **Step 3: Add `hovered_id()` accessor**

```cpp
// include/prism/app/widget_tree.hpp — add near the existing focused_id()/captured_id() accessors:
    [[nodiscard]] WidgetId hovered_id() const { return hovered_id_; }
```

- [ ] **Step 4: Add `set_debug_highlight` and the `highlight_id_` member**

```cpp
// public section, near mark_dirty_by_id:
    void set_debug_highlight(std::optional<WidgetId> id) {
        highlight_id_ = id;
        mark_dirty_by_id(root_.id);
    }

// private section, alongside hovered_id_/focused_id_/captured_id_:
    std::optional<WidgetId> highlight_id_;
```

- [ ] **Step 5: Inject the highlight at the end of `build_snapshot()`**

```cpp
// include/prism/app/widget_tree.hpp, WidgetTree::build_snapshot — change:
        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        resolve_clips(*snap);
        return snap;
// to:
        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        resolve_clips(*snap);
        if (highlight_id_) {
            for (auto& [id, rect] : snap->geometry) {
                if (id == *highlight_id_) {
                    snap->overlay.rect_outline(rect, Color::rgba(255, 140, 0), 2.0f);
                    break;
                }
            }
        }
        return snap;
```

- [ ] **Step 6: Build and run — new tests pass, no regressions**

```bash
ninja -C builddir
meson test -C builddir widget_tree
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 7: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: add WidgetTree::set_debug_highlight/hovered_id, highlight injection in build_snapshot

Unconditionally compiled (a std::optional<WidgetId> and one lookup
only when set are negligible cost) — no PRISM_DEBUG_TOOLS_ENABLED
gating needed, unlike debug_name's real per-node memory cost."
```

---

## Task 3: `AppContext::set_post_dispatch_hook` + all-dirty-entry publish

Modifies already-merged `model_app.hpp` — treat the existing suite as regression proof exactly as
sub-project 1's Task 5/6 did.

**Files:**
- Modify: `include/prism/app/model_app.hpp`
- Test: `tests/test_model_app.cpp`

**Interfaces:**
- Produces: `AppContext::set_post_dispatch_hook(std::function<void()>)`.
- Consumes: `WindowRegistry::for_each_dirty` (existing, sub-project 1).

- [ ] **Step 1: Record the baseline**

```bash
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 2: Write the failing tests**

```cpp
// tests/test_model_app.cpp — add near the other AppContext-primitive tests (reuse the
// CapturingBackend/IntFieldModel-style fixtures already established in this file by sub-project 1's
// Task 6 — read that section first to match conventions exactly, including the `prism::Window&`
// qualification this file requires)

TEST_CASE("post-dispatch hook fires once per processed event, regardless of which window") {
    struct TwoEventBackend final : public prism::BackendBase {
        prism::HeadlessWindow window_{0, {}};
        Window& create_window(prism::WindowConfig cfg) override {
            window_ = prism::HeadlessWindow{1, cfg};
            return window_;
        }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            event_cb(prism::WindowEvent{window_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{window_.id(), prism::MouseMove{prism::Point{prism::X{0}, prism::Y{0}}}});
            event_cb(prism::WindowEvent{window_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    Model model;
    auto backend = prism::Backend{std::make_unique<TwoEventBackend>()};
    auto& window = backend.create_window({});

    int hook_calls = 0;
    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        ctx.set_post_dispatch_hook([&] { ++hook_calls; });
    });

    CHECK(hook_calls == 2); // KeyPress and MouseMove each trigger the continuation; WindowClose returns early
}

TEST_CASE("dirtying a non-event entry via the post-dispatch hook still gets it published") {
    struct OneEventBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int submit_count_for_secondary_ = 0;

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
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::tab, 0}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId id, std::shared_ptr<const prism::SceneSnapshot>) override {
            if (id == secondary_id_) ++submit_count_for_secondary_;
        }
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    static Model second_model;
    Model model;
    auto backend_ptr = std::make_unique<OneEventBackend>();
    auto* raw_backend = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    prism::model_app(backend, window, model, [&](prism::AppContext& ctx) {
        auto* second_window = ctx.backend().request_window({});
        REQUIRE(second_window != nullptr);
        auto second_id = ctx.registry().add(*second_window, second_model);
        ctx.set_post_dispatch_hook([&ctx, second_id] {
            // Simulate the tree inspector marking the debug tree dirty every time something happens.
            auto* entry = ctx.registry().find(second_id);
            if (entry) entry->tree->mark_dirty_by_id(entry->tree->root().id);
        });
    });

    CHECK(raw_backend->submit_count_for_secondary_ >= 1);
}
```

- [ ] **Step 3: Run to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`set_post_dispatch_hook` doesn't exist yet).

- [ ] **Step 4: Add `set_post_dispatch_hook` to `AppContext`, thread it through**

```cpp
// include/prism/app/model_app.hpp — AppContext class:
    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w, Backend& b,
                         WindowRegistry& r, std::function<void(const KeyPress&)>& key_handler,
                         std::function<void()>& post_dispatch_hook)
        : sched_(s), clock_(&c), window_(&w), backend_(&b), registry_(&r),
          key_handler_(&key_handler), post_dispatch_hook_(&post_dispatch_hook) {}

    // ... existing accessors ...
    void set_post_dispatch_hook(std::function<void()> fn) {
        *post_dispatch_hook_ = std::move(fn);
    }

private:
    // ... existing members ...
    std::function<void()>* post_dispatch_hook_;
```

```cpp
// inside the 2-arg model_app() overload, declare alongside global_key_handler:
    std::function<void()> post_dispatch_hook;
```

```cpp
// update the AppContext construction:
    auto ctx = AppContext(sched, anim_clock, window, backend, registry, global_key_handler, post_dispatch_hook);
```

- [ ] **Step 5: Change the per-event continuation's publish logic**

```cpp
// include/prism/app/model_app.hpp — inside the dispatch continuation, change:
                    entry->tree->drain_shared();
                    if (entry->tree->any_dirty() || needs_publish)
                        publish_entry(wid, *entry);
                    schedule_tick();
// to:
                    entry->tree->drain_shared();
                    if (post_dispatch_hook) post_dispatch_hook();
                    if (needs_publish) publish_entry(wid, *entry);
                    registry.for_each_dirty([&](WindowId id, WindowRegistry::Entry& e) {
                        publish_entry(id, e);
                    });
                    schedule_tick();
```
Note: no explicit "skip re-publishing `wid`" guard is needed — `publish_entry` always calls
`tree->clear_dirty()`, so if `wid`'s entry was just published via the `needs_publish` branch, it's no
longer dirty by the time `for_each_dirty` runs and is naturally skipped. This preserves the exact
prior single-window behavior (publish once, either via the resize branch or the dirty-scan) while
generalizing to every entry.

- [ ] **Step 6: Build and run — new tests pass, baseline unchanged**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: Step 1's baseline count + 2 new passing tests. Pay particular attention to any existing
resize-handling test — the `needs_publish` path must still fire exactly once for a resize with no
other dirty entries.

- [ ] **Step 7: Commit**

```bash
git add include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "feat: add AppContext::set_post_dispatch_hook, publish all dirty entries per event

Previously only the entry that received an event could get republished
within that event's continuation — correct for exactly one entry, but
now that a second, cross-updating entry can exist (the tree inspector),
it needs to generalize. No behavior change for the single-entry case:
publish_entry always clears dirty, so the resize-forced publish and the
dirty-scan never double-publish the same entry."
```

---

## Task 4: Secondary-window teardown (`BackendBase::close_window`, `SoftwareBackend`, `TestBackend`)

**Files:**
- Modify: `include/prism/app/backend.hpp`
- Modify: `include/prism/backends/software_backend.hpp`
- Modify: `src/backends/software_backend.cpp`
- Modify: `include/prism/app/test_backend.hpp`
- Test: `tests/test_test_backend.cpp`, `tests/test_software_backend_request_window.cpp` (rename
  scope note: this file already exists from sub-project 1's Task 7 — add to it, don't replace it)

**Interfaces:**
- Produces: `BackendBase::close_window(WindowId)` (default no-op), `SoftwareBackend::close_window`
  (real cross-thread-safe teardown), `TestBackend::close_window` (synchronous, for headless tests).

- [ ] **Step 1: Write the failing `TestBackend` tests**

```cpp
// tests/test_test_backend.cpp — add at the end

TEST_CASE("TestBackend::close_window removes a window created via request_window") {
    prism::TestBackend tb{{}};
    auto* win = tb.request_window({});
    REQUIRE(win != nullptr);
    auto id = win->id();
    tb.close_window(id);
    CHECK(tb.window_count() == 0); // see Step 3 — a small test-only accessor
}

TEST_CASE("TestBackend::close_window on an unknown id is a safe no-op") {
    prism::TestBackend tb{{}};
    tb.close_window(9999);
    CHECK(true); // must not crash
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`close_window`/`window_count` don't exist yet on `TestBackend`).

- [ ] **Step 3: Add `close_window` (and a test-only `window_count()`) to `TestBackend`**

```cpp
// include/prism/app/test_backend.hpp — add inside class TestBackend, public section:
    void close_window(WindowId id) override { windows_.erase(id); }
    [[nodiscard]] size_t window_count() const { return windows_.size(); }
```
Also add `virtual void close_window(WindowId id) {}` to `BackendBase` first (Step 4) before this
compiles as an override.

- [ ] **Step 4: Add the default virtual to `BackendBase`**

```cpp
// include/prism/app/backend.hpp — inside class BackendBase, after request_window:
    virtual void close_window(WindowId) {}
```
```cpp
// inside class Backend, after request_window:
    void close_window(WindowId id) { impl_->close_window(id); }
```

- [ ] **Step 5: Build and run `TestBackend`'s tests**

```bash
ninja -C builddir
meson test -C builddir test_backend
```

- [ ] **Step 6: Implement `SoftwareBackend::close_window`**

```cpp
// include/prism/backends/software_backend.hpp — inside class SoftwareBackend, public section:
    void close_window(WindowId id) override;

// private section, alongside window_requests_:
    prism::core::mpsc_queue<WindowId> close_requests_;
    void drain_close_requests();
```

```cpp
// src/backends/software_backend.cpp
void SoftwareBackend::close_window(WindowId id) {
    close_requests_.push(id);
    wake();
}

void SoftwareBackend::drain_close_requests() {
    while (auto id_opt = close_requests_.pop()) {
        windows_.erase(*id_opt);
        snapshots_.erase(*id_opt);
    }
}
```

- [ ] **Step 7: Drain close requests alongside window-creation requests**

```cpp
// src/backends/software_backend.cpp, inside run()'s switch statement — change:
            case SDL_EVENT_USER:
                drain_window_requests();
                break;
// to:
            case SDL_EVENT_USER:
                drain_window_requests();
                drain_close_requests();
                break;
```
```cpp
// and right after the main while(running_...) loop ends (alongside the existing post-loop
// drain_window_requests() call):
    drain_window_requests();
    drain_close_requests();
}
```

- [ ] **Step 8: Add the native-decoration close event and fix the custom-chrome Close handler**

Read the current `run()` switch statement first to find the exact `default: break;` location (no
`SDL_EVENT_WINDOW_CLOSE_REQUESTED` case exists today) and the exact custom-chrome Close-button
handler (search for `WindowChrome::HitZone::Close`).

```cpp
// add a new case, alongside the other SDL_EVENT_WINDOW_* handling:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                event_cb(WindowEvent{wid, WindowClose{}});
                break;
```
This deliberately does NOT set `running_.store(false)` — forwarding `WindowClose` and letting
`model_app()`'s existing `if (wid == primary_id) loop.finish(); else registry.remove(wid);` decide
is what makes secondary-window close work. `model_app()`'s dispatch also needs one addition to
actually tear down the OS window when removing a secondary entry — see Step 9.

```cpp
// find the existing custom-chrome Close zone handler, currently something like:
                    if (zone == WindowChrome::HitZone::Close) {
                        event_cb(WindowEvent{wid, WindowClose{}});
                        running_.store(false, std::memory_order_relaxed);
                        break;
                    }
// change to:
                    if (zone == WindowChrome::HitZone::Close) {
                        event_cb(WindowEvent{wid, WindowClose{}});
                        break;
                    }
```
This is backward-compatible: for the single-window case, `wid` is always the primary id, so
`model_app()`'s `if (wid == primary_id) loop.finish();` still fires immediately — `loop.finish()`
returns from `loop.run()`, which then calls `backend.quit()` (`running_.store(false); wake();`) —
same end state, one more round-trip through the app thread instead of the backend thread deciding
unilaterally.

- [ ] **Step 9: Call `close_window` when removing a secondary entry in `model_app()`**

```cpp
// include/prism/app/model_app.hpp — change:
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            loop.finish();
                        } else {
                            registry.remove(wid);
                        }
                        return;
                    }
// to:
                    if (std::holds_alternative<WindowClose>(ev)) {
                        if (wid == primary_id) {
                            loop.finish();
                        } else {
                            registry.remove(wid);
                            backend.close_window(wid);
                        }
                        return;
                    }
```

- [ ] **Step 10: Build and run the full suite**

```bash
ninja -C builddir
meson test -C builddir
```
Expected: no regressions (this task's `SoftwareBackend`/native-SDL changes are not headless-testable
— same accepted gap as sub-project 1's `request_window`; verified by compilation + full suite pass,
per that established precedent).

- [ ] **Step 11: Commit**

```bash
git add include/prism/app/backend.hpp include/prism/backends/software_backend.hpp \
        src/backends/software_backend.cpp include/prism/app/test_backend.hpp \
        include/prism/app/model_app.hpp tests/test_test_backend.cpp
git commit -m "feat: add BackendBase::close_window, real secondary-window teardown

Closes the gap sub-project 1's final review flagged: SoftwareBackend
had no way to close a non-primary window without quitting the whole
app. Backward-compatible with the single-window case — the backend no
longer unilaterally decides to quit on any close-button click; it just
forwards WindowClose and lets model_app()'s existing primary/secondary
check decide, exactly as SDL_EVENT_QUIT already does for whole-app
quit signals."
```

---

## Task 5: `NodeRow` + `flatten_tree()`

**Files:**
- Create: `include/prism/widgets/debug/tree_inspector.hpp`
- Test: `tests/test_flatten_tree.cpp`
- Modify: `tests/meson.build`

**Interfaces:**
- Produces: `prism::debug::NodeRow`, `prism::debug::flatten_tree(const WidgetTree&, const
  std::set<WidgetId>& expanded) -> std::vector<NodeRow>`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_flatten_tree.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>
#include <prism/core/field.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct Leaf { prism::Field<int> value{0}; };
struct FlatModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
};
}

TEST_CASE("flatten_tree on an empty-of-children root produces just the root row") {
    struct Empty { void view(prism::WidgetTree::ViewBuilder&) {} };
    Empty model;
    prism::WidgetTree tree(model);
    auto rows = prism::debug::flatten_tree(tree, {});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].depth == 0);
}

TEST_CASE("flatten_tree depth-first order matches child order, depth increments per level") {
    FlatModel model;
    prism::WidgetTree tree(model);
    std::set<prism::WidgetId> all_expanded;
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    all_expanded.insert(tree.root().id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);
    REQUIRE(rows.size() >= 3); // root + a + b, at minimum
    CHECK(rows[0].depth == 0);
    for (size_t i = 1; i < rows.size(); ++i)
        CHECK(rows[i].depth >= 1);
}

TEST_CASE("flatten_tree skips children of a node not in the expanded set") {
    FlatModel model;
    prism::WidgetTree tree(model);
    auto rows_collapsed = prism::debug::flatten_tree(tree, {}); // nothing expanded
    CHECK(rows_collapsed.size() == 1); // just the root, no children shown

    std::set<prism::WidgetId> only_root{tree.root().id};
    auto rows_root_expanded = prism::debug::flatten_tree(tree, only_root);
    CHECK(rows_root_expanded.size() >= 3); // root + its direct children now visible
}

TEST_CASE("flatten_tree row fields reflect live tree state") {
    FlatModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1); // establish geometry
    std::set<prism::WidgetId> all_expanded{tree.root().id};
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);
    for (auto& row : rows) {
        CHECK(row.id != 0);
        CHECK(!row.layout_kind_name.empty());
    }
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`tree_inspector.hpp` doesn't exist yet).

- [ ] **Step 3: Implement `NodeRow` and `flatten_tree`**

Needs read access to `WidgetTree::root()` (existing, public: `[[nodiscard]] WidgetNode& root()`) and
a way to name each `LayoutKind` value — write a small local `layout_kind_name(LayoutKind) ->
std::string_view` switch (the enum has 9 values: `Default, Row, Column, Spacer, Canvas, Scroll,
VirtualList, Table, Tabs`, `include/prism/ui/delegate.hpp`).

```cpp
// include/prism/widgets/debug/tree_inspector.hpp
#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/types.hpp>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace prism::debug {
using namespace prism::core;
using namespace prism::ui;
using namespace prism::app;

struct NodeRow {
    WidgetId id = 0;
    std::string name;
    std::string layout_kind_name;
    int depth = 0;
    Rect rect;
    bool dirty = false;
    bool hovered = false;
    bool focused = false;
    bool pressed = false;
    bool has_children = false;
    bool expanded = false;
};

inline std::string_view layout_kind_name(LayoutKind k) {
    switch (k) {
        case LayoutKind::Default:     return "Default";
        case LayoutKind::Row:         return "Row";
        case LayoutKind::Column:      return "Column";
        case LayoutKind::Spacer:      return "Spacer";
        case LayoutKind::Canvas:      return "Canvas";
        case LayoutKind::Scroll:      return "Scroll";
        case LayoutKind::VirtualList: return "VirtualList";
        case LayoutKind::Table:       return "Table";
        case LayoutKind::Tabs:        return "Tabs";
    }
    return "?";
}

namespace detail {

inline void flatten_node(const WidgetNode& node, int depth, WidgetId hovered_id,
                         const std::set<WidgetId>& expanded, std::vector<NodeRow>& out) {
    NodeRow row;
    row.id = node.id;
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    row.name = node.debug_name.empty()
        ? std::string(layout_kind_name(node.layout_kind)) : node.debug_name;
#else
    row.name = layout_kind_name(node.layout_kind);
#endif
    row.layout_kind_name = layout_kind_name(node.layout_kind);
    row.depth = depth;
    row.rect = Rect{Point{X{0}, Y{0}}, node.canvas_bounds.extent};
    row.dirty = node.dirty;
    row.hovered = (node.id == hovered_id);
    row.focused = node.visual_state.focused;
    row.pressed = node.visual_state.pressed;
    row.has_children = !node.children.empty();
    row.expanded = expanded.contains(node.id);
    out.push_back(row);

    if (!node.children.empty() && expanded.contains(node.id)) {
        for (auto& child : node.children)
            flatten_node(child, depth + 1, hovered_id, expanded, out);
    }
}

} // namespace detail

inline std::vector<NodeRow> flatten_tree(WidgetTree& tree, const std::set<WidgetId>& expanded) {
    std::vector<NodeRow> rows;
    detail::flatten_node(tree.root(), 0, tree.hovered_id(), expanded, rows);
    return rows;
}

} // namespace prism::debug
```

Note: `flatten_tree` takes `WidgetTree&` (non-const) because `WidgetTree::root()` is non-const
(`WidgetNode& root()` — there is no `const WidgetNode& root() const` overload confirmed available;
verify this against the actual current `widget_tree.hpp` before finalizing the signature, and adjust
the test declarations above to match if a const overload does exist).

Also verify `WidgetVisualState`'s exact field names (`focused`/`pressed`/`hovered` assumed here based
on prior research — confirm against `include/prism/ui/delegate.hpp`'s `WidgetVisualState` struct
before trusting this code, and adjust field names if they differ).

- [ ] **Step 4: Register the test, build, run**

```meson
# tests/meson.build, headless_tests dict:
  'flatten_tree' : files('test_flatten_tree.cpp'),
```
```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir flatten_tree
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/debug/tree_inspector.hpp tests/test_flatten_tree.cpp tests/meson.build
git commit -m "feat: add NodeRow and flatten_tree() — pure depth-first WidgetTree walk

No rendering dependency; falls back to the layout-kind name when
debug_name is empty (container nodes, or builds without
PRISM_DEBUG_TOOLS_ENABLED)."
```

---

## Task 6: `TreeInspectorModel`

**Files:**
- Modify: `include/prism/widgets/debug/tree_inspector.hpp`
- Test: `tests/test_tree_inspector_model.cpp`
- Modify: `tests/meson.build`

**Interfaces:**
- Produces: `prism::debug::TreeInspectorModel` — `List<NodeRow> rows`, `Field<std::optional<WidgetId>>
  selected`, `std::function<void(size_t, const NodeRow&)> on_click` (installed by
  `TreeInspectorController`, Task 7 — the model stays a plain data holder; row-click *behavior*
  belongs to the controller, not the model, matching every other model in this codebase), `view()`
  using sub-project 2's `on_row_click`. Also produces `Widget<NodeRow>` (`namespace prism::ui`), the
  rendering specialization `.list()` needs for each row.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_inspector_model.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("TreeInspectorModel row click invokes on_click with the row's index and id") {
    prism::debug::TreeInspectorModel model;
    model.rows.push_back(prism::debug::NodeRow{.id = 42});

    std::vector<std::pair<size_t, prism::WidgetId>> clicks;
    model.on_click = [&](size_t index, const prism::debug::NodeRow& row) {
        clicks.emplace_back(index, row.id);
    };

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    prism::WidgetId row_leaf_id = 0;
    for (auto& [id, rect] : snap->geometry) {
        if (id != 0 && rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { row_leaf_id = id; break; }
    }
    REQUIRE(row_leaf_id != 0);

    tree.dispatch(row_leaf_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0] == std::make_pair(size_t{0}, prism::WidgetId{42}));
}

TEST_CASE("TreeInspectorModel with no on_click set does not crash on row click") {
    prism::debug::TreeInspectorModel model;
    model.rows.push_back(prism::debug::NodeRow{.id = 7});
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    prism::WidgetId row_leaf_id = 0;
    for (auto& [id, rect] : snap->geometry)
        if (id != 0 && rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { row_leaf_id = id; break; }
    REQUIRE(row_leaf_id != 0);

    tree.dispatch(row_leaf_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});
    CHECK(true); // must not crash — on_click is unset (default-constructed std::function)
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
ninja -C builddir
```

- [ ] **Step 3: Implement `Widget<NodeRow>` and `TreeInspectorModel`**

`Widget<NodeRow>` needs a rendering specialization so `.list()` can draw each row (indent by `depth`,
show `name`, a dirty indicator) — modeled on the established `Widget<Checkbox>` pattern
(`include/prism/ui/delegate.hpp`) for exact signature shape (`record(DrawList&, const
Field<NodeRow>&, WidgetNode&)`, `handle_input(Field<NodeRow>&, const InputEvent&, WidgetNode&)`,
`static constexpr FocusPolicy focus_policy`). `ViewBuilder::list<T>` takes `List<T>&` directly (not
`Field<List<T>>&` — `List<T>` is already its own observable type, confirmed against
`tests/test_virtual_list.cpp`'s `StringListModel`, which uses `List<std::string> items;` with no
`Field<>` wrapper).

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — add after flatten_tree, closing
// namespace prism::debug temporarily for the Widget<> specialization, which must live in
// namespace prism::ui to be found by ADL/template lookup:

} // close namespace prism::debug

namespace prism::ui {
template <>
struct Widget<prism::debug::NodeRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height row_h{22.f};

    static void record(DrawList& dl, const Field<prism::debug::NodeRow>& field, WidgetNode& node) {
        auto& row = field.get();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        auto w = detail::widget_w(node);
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{w, row_h}}, bg);
        X indent{static_cast<float>(row.depth) * 16.f};
        std::string label = (row.has_children ? (row.expanded ? "v " : "> ") : "  ") + row.name;
        dl.text(label, Point{X{8.f} + indent, Y{4.f}}, 13.f, row.dirty ? t.warning : t.text);
    }

    static void handle_input(Field<prism::debug::NodeRow>&, const InputEvent&, WidgetNode&) {
        // Selection is handled entirely via on_row_click on the containing .list() call —
        // this leaf has nothing of its own to mutate on click.
    }
};
} // namespace prism::ui

namespace prism::debug { // reopen

struct TreeInspectorModel {
    List<NodeRow> rows;
    Field<std::optional<WidgetId>> selected;
    std::function<void(size_t, const NodeRow&)> on_click;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.list(rows, [this](size_t index, const NodeRow& row) {
            if (on_click) on_click(index, row);
        });
    }
};

} // namespace prism::debug
```

- [ ] **Step 4: Register, build, run**

```meson
# tests/meson.build:
  'tree_inspector_model' : files('test_tree_inspector_model.cpp'),
```
```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir tree_inspector_model
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/debug/tree_inspector.hpp tests/test_tree_inspector_model.cpp tests/meson.build
git commit -m "feat: add TreeInspectorModel and Widget<NodeRow> rendering

Indented single-line row (expand/collapse marker, name, dirty
indicator), selection via sub-project 2's on_row_click rather than the
row's own handle_input."
```

---

## Task 7: `TreeInspectorController`

**Files:**
- Modify: `include/prism/widgets/debug/tree_inspector.hpp`
- Test: `tests/test_tree_inspector_controller.cpp`
- Modify: `tests/meson.build`

**Interfaces:**
- Produces: `prism::debug::TreeInspectorController` — construction attaches the debug window;
  installs the post-dispatch hook; drives debug→main highlight and main→debug hover/scroll.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_inspector_controller.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>
#include <prism/app/test_backend.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct MainModel { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
}

TEST_CASE("TreeInspectorController refresh populates rows from the main tree") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::debug::TreeInspectorController controller(main_tree, debug_model);

    controller.refresh();
    CHECK(debug_model.rows.size() >= 2); // root + the int field's leaf
}

TEST_CASE("clicking a debug row sets the main tree's highlight") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::debug::TreeInspectorController controller(main_tree, debug_model);
    controller.refresh();

    REQUIRE(!debug_model.rows.empty());
    controller.on_row_clicked(0, debug_model.rows[debug_model.rows.size() - 1]); // a leaf, not the root

    auto snap2 = main_tree.build_snapshot(400, 300, 2);
    bool found = false;
    for (auto& cmd : snap2->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found = true;
    CHECK(found);
}

TEST_CASE("hovering the main tree updates the debug model's selection on refresh") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto leaf_id = snap->geometry.back().first;
    auto leaf_rect = snap->geometry.back().second;
    main_tree.dispatch(leaf_id, prism::MouseMove{leaf_rect.origin}); // won't actually hover without
                                                                       // going through hit_test/route_mouse_move
    // Use the real routing path instead of raw dispatch, matching how other tests in this codebase
    // drive hover state (search tests/test_widget_tree.cpp for update_hover usage and mirror it):
    // main_tree.update_hover(leaf_id) is the direct, already-tested primitive to use here.
    main_tree.update_hover(leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::debug::TreeInspectorController controller(main_tree, debug_model);
    controller.refresh();

    CHECK(debug_model.selected.get() == std::optional<prism::WidgetId>{leaf_id});
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
ninja -C builddir
```

- [ ] **Step 3: Implement `TreeInspectorController`**

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — add after TreeInspectorModel

class TreeInspectorController {
public:
    TreeInspectorController(WidgetTree& main_tree, TreeInspectorModel& debug_model)
        : main_tree_(&main_tree), debug_model_(&debug_model) {
        expanded_.insert(main_tree_->root().id);
        debug_model_->on_click = [this](size_t index, const NodeRow& row) {
            on_row_clicked(index, row);
        };
    }

    void refresh() {
        auto rows = flatten_tree(*main_tree_, expanded_);
        while (!debug_model_->rows.empty())
            debug_model_->rows.erase(debug_model_->rows.size() - 1);
        for (auto& row : rows)
            debug_model_->rows.push_back(row);

        auto hovered = main_tree_->hovered_id();
        if (hovered != 0)
            debug_model_->selected.set(hovered);
    }

    void on_row_clicked(size_t, const NodeRow& row) {
        main_tree_->set_debug_highlight(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
    }

private:
    WidgetTree* main_tree_;
    TreeInspectorModel* debug_model_;
    std::set<WidgetId> expanded_;
};
```

**Verify before finalizing:** `List<T>::erase(size_t)` erases by INDEX, and repeatedly erasing index
`size()-1` from the back is correct for a full clear, but check whether `List<T>` has a cheaper
"clear all" primitive (search `include/prism/core/list.hpp` again — sub-project 2's own design spec
noted "No 'replace all' / 'clear + bulk push' method exists" as of that sub-project, so this
loop-based clear is likely still the only option; confirm this hasn't changed and note the O(n²)
erase-from-front-each-time footgun is avoided by erasing from the BACK, which is O(1) per erase for
a `std::vector`-backed `List<T>`).

- [ ] **Step 4: Register, build, run**

```meson
# tests/meson.build:
  'tree_inspector_controller' : files('test_tree_inspector_controller.cpp'),
```
```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir tree_inspector_controller
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/debug/tree_inspector.hpp tests/test_tree_inspector_controller.cpp tests/meson.build
git commit -m "feat: add TreeInspectorController — refresh, click-to-highlight, hover-to-select"
```

---

## Task 8: Hotkey wiring in `model_app()`

**Files:**
- Modify: `include/prism/app/model_app.hpp`
- Test: `tests/test_model_app.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1-7.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_model_app.cpp — add at the end (guard the whole test with
// #ifdef PRISM_DEBUG_TOOLS_ENABLED, since the feature itself is compiled out otherwise)

#ifdef PRISM_DEBUG_TOOLS_ENABLED
#include <prism/widgets/debug/tree_inspector.hpp>

TEST_CASE("Ctrl+Shift+I attaches a debug window; pressing it again removes it") {
    struct HotkeyBackend final : public prism::BackendBase {
        prism::HeadlessWindow primary_{0, {}};
        prism::HeadlessWindow secondary_{0, {}};
        prism::WindowId secondary_id_ = 0;
        int close_calls_ = 0;

        Window& create_window(prism::WindowConfig cfg) override {
            primary_ = prism::HeadlessWindow{1, cfg};
            return primary_;
        }
        Window* request_window(prism::WindowConfig cfg) override {
            secondary_id_ = 2;
            secondary_ = prism::HeadlessWindow{secondary_id_, cfg};
            return &secondary_;
        }
        void close_window(prism::WindowId) override { ++close_calls_; }
        void run(std::function<void(const prism::WindowEvent&)> event_cb) override {
            auto mods = static_cast<uint16_t>(prism::mods::ctrl | prism::mods::shift);
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::KeyPress{prism::keys::i, mods}});
            event_cb(prism::WindowEvent{primary_.id(), prism::WindowClose{}});
        }
        void submit(prism::WindowId, std::shared_ptr<const prism::SceneSnapshot>) override {}
        void wake() override {}
        void quit() override {}
    };

    struct Model { prism::Field<int> value{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(value); } };
    Model model;
    auto backend_ptr = std::make_unique<HotkeyBackend>();
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    // The hotkey is installed inside model_app() itself (Step 3), unconditionally when
    // PRISM_DEBUG_TOOLS_ENABLED is defined — no app-provided setup() is needed for it to fire, so
    // this test passes nullptr and relies purely on HotkeyBackend's two KeyPress events.
    prism::model_app(backend, window, model, nullptr);

    // First Ctrl+Shift+I attaches (request_window called, no close yet); second detaches
    // (close_window called once) — proving both the attach and the teardown path ran.
    CHECK(raw->close_calls_ == 1);
}
#endif
```

- [ ] **Step 2: Run to verify it fails**

```bash
ninja -C builddir
```

- [ ] **Step 3: Wire the hotkey inside `model_app()`, gated by the build flag**

```cpp
// include/prism/app/model_app.hpp — include the new header:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
#include <prism/widgets/debug/tree_inspector.hpp>
#endif
```

```cpp
// inside the 2-arg model_app() overload, after the AppContext construction and before
// `if (setup) { setup(ctx); ... }`:
#ifdef PRISM_DEBUG_TOOLS_ENABLED
    prism::debug::TreeInspectorModel debug_model;
    std::optional<WindowId> debug_window_id;
    std::optional<prism::debug::TreeInspectorController> debug_controller;

    ctx.set_global_key_handler([&](const KeyPress& kp) {
        if (kp.key != keys::i || (kp.mods & (mods::ctrl | mods::shift)) != (mods::ctrl | mods::shift))
            return;
        if (!debug_window_id) {
            auto* win = ctx.backend().request_window(WindowConfig{.title = "PRISM Tree Inspector"});
            if (!win) return; // request failed — log once and stay dormant, per the spec's error handling
            debug_window_id = ctx.registry().add(*win, debug_model);
            debug_controller.emplace(*registry.find(primary_id)->tree, debug_model);
            ctx.set_post_dispatch_hook([&] {
                if (debug_controller) debug_controller->refresh();
            });
        } else {
            ctx.registry().remove(*debug_window_id);
            ctx.backend().close_window(*debug_window_id);
            debug_window_id.reset();
            debug_controller.reset();
            ctx.set_post_dispatch_hook(nullptr);
        }
    });
#endif
```

**Verify before finalizing:**
1. `debug_controller.emplace(*registry.find(primary_id)->tree, debug_model)` — confirm
   `WindowRegistry::find` is reachable here (it is, `registry` is the local variable in scope), and
   that `TreeInspectorController`'s constructor signature (`WidgetTree&, TreeInspectorModel&`) is
   satisfied by dereferencing `find(primary_id)->tree` (a `std::unique_ptr<WidgetTree>` — dereference
   with `*`). `TreeInspectorController`'s constructor (Task 7) already installs `debug_model.on_click`
   itself, so no separate wiring is needed here beyond constructing it.
2. This wiring OVERWRITES any `set_global_key_handler`/`set_post_dispatch_hook` an app's own
   `setup()` callback might separately want to install, since it runs BEFORE `setup(ctx)` is called
   — an app's own `setup()` would silently clobber the debug hotkey if it also calls
   `set_global_key_handler`. This is a real, known limitation (only one handler slot exists per
   sub-project 1's design) — document it in this task's commit message rather than solving it now
   (solving it, e.g. via a chain-of-handlers, is out of scope and not something either prior spec
   anticipated).

- [ ] **Step 4: Build and run**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/model_app.hpp include/prism/widgets/debug/tree_inspector.hpp tests/test_model_app.cpp
git commit -m "feat: wire Ctrl+Shift+I hotkey to attach/detach the tree inspector

Zero app code required when PRISM_DEBUG_TOOLS_ENABLED.

Known limitation: this hotkey wiring installs the sole
global-key-handler/post-dispatch-hook slots AppContext exposes: an
app's own setup() calling set_global_key_handler/set_post_dispatch_hook
would silently override this. Not solved here — out of scope."
```

---

## Task 9: Integration test + final verification

**Files:**
- Test: `tests/test_model_app.cpp` or a new dedicated integration test file — decide based on which
  reads more naturally once Task 8's actual code is in place.

**Interfaces:**
- Consumes: everything from Tasks 1-8.

- [ ] **Step 1: Write an end-to-end integration test**

Using `TestBackend` (extended in Task 4 with `close_window`) and the real hotkey wiring from Task 8,
write one test that: creates a primary window with a small model, sends the hotkey twice (attach,
detach) plus a hover event and a debug-row click in between, and asserts the full round trip:
rows populated after attach, main tree's overlay contains a highlight after the simulated click,
`close_calls_` fires on detach. This is largely a fuller version of Task 8's own test — write it as
a genuine integration scenario exercising steps 2-4 of the spec's Data flow section together, not a
restatement of Task 8's narrower test.

- [ ] **Step 2: Build and run the full suite one final time**

```bash
meson setup builddir --reconfigure
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Read the actual pass/fail count and exit code — this is the final verification for the whole plan.

- [ ] **Step 3: Commit**

```bash
git add tests/
git commit -m "test: end-to-end integration test for the live tree inspector"
```
