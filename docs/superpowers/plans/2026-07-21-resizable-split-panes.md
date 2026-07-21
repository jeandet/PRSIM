# Resizable Split Panes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `vb.handle()` marker primitive that makes the adjacent panes of an `hstack`/`vstack` user-resizable by dragging, and wire it into the existing tree-widget's row-list/detail-panel split.

**Architecture:** A `Handle` is a fixed-thickness leaf inserted between panes. Until dragged, a Row/Column with a Handle child lays out exactly as it does today (content-preferred/expand). The first drag snapshots the row's current pane sizes into a `SplitState` living in the container's `WidgetNode::edit_state` (mirroring `ScrollState`); from then on, `layout_measure` reads pane sizes from that state instead of recomputing them, while `layout_arrange`'s existing Row/Column loop is untouched. Dragging reuses the same self-contained on_input pattern `Widget<Slider>` already uses (no new event-routing branch needed).

**Tech Stack:** C++26 (P2996 reflection where available), Meson build, doctest for tests.

## Global Constraints

- Strong `Scalar<Tag>` coordinate types (`X`, `Y`, `DX`, `DY`, `Width`, `Height`) at API boundaries — no raw floats in public signatures. Internal layout math may use raw floats (existing convention in `layout.hpp`).
- No comment-decorated code blocks; comments only justify non-obvious decisions.
- Every task adds/extends tests before or alongside the implementation (TDD) and must leave the full test suite passing.
- Every new file added to `tests/meson.build`'s `headless_tests` map, alphabetically placed.
- Follow this repo's existing patterns exactly (mirror `ScrollState`/`ScrollbarDrag`/`Widget<Slider>` idioms) rather than inventing new ones.

---

## Task 1: Theme divider colors

**Files:**
- Modify: `include/prism/ui/context.hpp:64` (right after `scrollbar_thumb`)
- Test: `tests/test_theme.cpp`

**Interfaces:**
- Produces: `Theme::divider` (`Color`), `Theme::divider_hover` (`Color`) — consumed by Task 2's Handle `record()`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_theme.cpp` (check the file first for its exact `TEST_CASE` style; it follows the same pattern as other `Theme` field checks already in that file):

```cpp
TEST_CASE("Theme has divider colors distinct from scrollbar thumb") {
    prism::Theme t;
    CHECK(t.divider.r != 0 || t.divider.g != 0 || t.divider.b != 0);
    CHECK(t.divider_hover.a > t.divider.a);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_theme -v`
Expected: FAIL to compile — `Theme` has no member `divider`.

- [ ] **Step 3: Add the fields**

In `include/prism/ui/context.hpp`, right after the `scrollbar_thumb` line:

```cpp
    // Scrollbar thumb
    Color scrollbar_thumb = Color::rgba(120, 120, 130, 160);

    // Split-pane divider
    Color divider        = Color::rgba(90, 90, 100, 160);
    Color divider_hover   = Color::rgba(130, 130, 145, 200);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_theme -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/context.hpp tests/test_theme.cpp
git commit -m "feat(theme): add divider/divider_hover colors for split panes"
```

---

## Task 2: Handle renders as a fixed-thickness themed bar

**Files:**
- Modify: `include/prism/ui/delegate.hpp:41` (`LayoutKind` enum)
- Modify: `include/prism/ui/layout.hpp:17-25` (add `splitter::` namespace), `:49` (`LayoutNode::Kind`), `:92-134` (`layout_measure`)
- Modify: `include/prism/app/widget_tree.hpp:1631-1725` (`build_layout`), `:1310-1340` (`update_canvas_bounds`), and add `ViewBuilder::handle()` near `spacer()` (`:167-173`)
- Test: `tests/test_layout.cpp` (pure layout test), `tests/test_split.cpp` (**new** — full ViewBuilder/WidgetTree test)
- Modify: `tests/meson.build` (register `test_split.cpp`)

**Interfaces:**
- Consumes: nothing new from other tasks.
- Produces: `LayoutKind::Handle` (enum value), `LayoutNode::Kind::Handle` (enum value), `splitter::thickness_px` (`inline constexpr float`, value `6.f`), `splitter::min_pane_size_px` (`inline constexpr float`, value `24.f`) — both consumed by Tasks 3 and 5. `ViewBuilder::handle()` (no-arg method) — consumed by all later tasks and by Task 9's `tree()` wiring.

- [ ] **Step 1: Write the failing pure-layout test**

Add to `tests/test_layout.cpp` (this file already has `TEST_CASE`s constructing bare `LayoutNode` trees and calling `layout_measure`/`layout_arrange` directly — follow that style):

```cpp
TEST_CASE("Handle LayoutNode measures to fixed thickness regardless of content") {
    LayoutNode handle;
    handle.kind = LayoutNode::Kind::Handle;
    handle.id = 1;
    // No draws at all — a Handle's size must not depend on bounding_box().

    layout_measure(handle, LayoutAxis::Horizontal);

    CHECK(handle.hint.preferred == doctest::Approx(splitter::thickness_px));
    CHECK_FALSE(handle.hint.expand);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_layout -v`
Expected: FAIL to compile — no `LayoutNode::Kind::Handle`, no `splitter::thickness_px`.

- [ ] **Step 3: Add the layout.hpp pieces**

In `include/prism/ui/layout.hpp`, right after the existing `namespace scrollbar { ... }` block:

```cpp
namespace splitter {
    inline constexpr float thickness_px = 6.f;
    inline constexpr float min_pane_size_px = 24.f;
}
```

Change the `LayoutNode::Kind` enum:

```cpp
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle } kind = Kind::Leaf;
```

In `layout_measure`'s switch, add a case (placement anywhere in the switch is fine; put it next to `Kind::Leaf` for readability):

```cpp
    case LayoutNode::Kind::Handle:
        node.hint = {.preferred = splitter::thickness_px, .expand = false};
        return;
```

- [ ] **Step 4: Run the layout test to verify it passes**

Run: `meson test -C builddir test_layout -v`
Expected: PASS

- [ ] **Step 5: Write the failing ViewBuilder/WidgetTree test**

Create `tests/test_split.cpp`:

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

struct TwoPaneModel {
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

TEST_CASE("A Handle between two panes renders as a fixed-width bar") {
    TwoPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);

    REQUIRE(snap->geometry.size() == 3);
    auto& [pane0_id, pane0_rect] = snap->geometry[0];
    auto& [handle_id, handle_rect] = snap->geometry[1];
    auto& [pane1_id, pane1_rect] = snap->geometry[2];

    CHECK(handle_rect.extent.w.raw() == doctest::Approx(splitter::thickness_px));
    CHECK(handle_rect.origin.x.raw() == doctest::Approx(pane0_rect.origin.x.raw() + pane0_rect.extent.w.raw()));
    CHECK(pane1_rect.origin.x.raw() == doctest::Approx(handle_rect.origin.x.raw() + handle_rect.extent.w.raw()));
}
```

- [ ] **Step 6: Register the new test file**

In `tests/meson.build`, add (alphabetically, right after `'software_renderer'` or near `'scroll'` — match the existing alphabetical-ish ordering in the file):

```meson
  'split' : files('test_split.cpp'),
```

- [ ] **Step 7: Run test to verify it fails**

Run: `meson setup --reconfigure builddir && meson test -C builddir test_split -v`
Expected: FAIL — `ViewBuilder` has no member `handle`; `LayoutKind` has no `Handle`.

- [ ] **Step 8: Add `LayoutKind::Handle`**

In `include/prism/ui/delegate.hpp`, change:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle };
```

- [ ] **Step 9: Add the `build_layout` branch for Handle**

In `include/prism/app/widget_tree.hpp`, inside `build_layout`'s `!node.is_container` branch, add a new `else if` before the generic leaf `else`:

```cpp
        if (!node.is_container) {
            if (node.layout_kind == LK::Spacer) {
                LayoutNode spacer;
                spacer.kind = LayoutNode::Kind::Spacer;
                spacer.id = node.id;
                spacer.theme = node.theme;
                parent.children.push_back(std::move(spacer));
            } else if (node.layout_kind == LK::Canvas) {
                LayoutNode canvas;
                canvas.kind = LayoutNode::Kind::Canvas;
                canvas.id = node.id;
                canvas.theme = node.theme;
                canvas.draws = node.draws;
                canvas.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(canvas));
            } else if (node.layout_kind == LK::Handle) {
                LayoutNode handle;
                handle.kind = LayoutNode::Kind::Handle;
                handle.id = node.id;
                handle.theme = node.theme;
                handle.draws = node.draws;
                handle.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(handle));
            } else {
```

- [ ] **Step 10: Extend the re-record condition in `update_canvas_bounds`**

Still in `widget_tree.hpp`, change:

```cpp
        // Re-record all leaf/canvas widgets after layout so delegates
        // can use their allocated size instead of hardcoded minimums.
        if (layout_node.kind == LayoutNode::Kind::Leaf ||
            layout_node.kind == LayoutNode::Kind::Canvas) {
```

to:

```cpp
        // Re-record all leaf/canvas/handle widgets after layout so delegates
        // can use their allocated size instead of hardcoded minimums.
        if (layout_node.kind == LayoutNode::Kind::Leaf ||
            layout_node.kind == LayoutNode::Kind::Canvas ||
            layout_node.kind == LayoutNode::Kind::Handle) {
```

- [ ] **Step 11: Add `ViewBuilder::handle()`**

In `include/prism/app/widget_tree.hpp`, right after the existing `spacer()` method:

```cpp
        void spacer() {
            Node s;
            s.id = tree_.next_id_++;
            s.is_leaf = true;
            s.layout_kind = LayoutKind::Spacer;
            current_parent().children.push_back(std::move(s));
        }

        void handle() {
            Node h;
            h.id = tree_.next_id_++;
            h.is_leaf = true;
            h.layout_kind = LayoutKind::Handle;
            h.build_widget = [](WidgetNode& wn) {
                wn.focus_policy = FocusPolicy::none;
                wn.record = [](WidgetNode& node) {
                    node.draws.clear();
                    auto& vs = node_vs(node);
                    auto& t = node_theme(node);
                    auto sz = node_allocated(node);
                    auto color = (vs.pressed || vs.hovered) ? t.divider_hover : t.divider;
                    node.draws.filled_rect(detail::make_rect(X{0}, Y{0}, sz.w, sz.h), color);
                };
                wn.record(wn);
            };
            current_parent().children.push_back(std::move(h));
        }
```

- [ ] **Step 12: Run test to verify it passes**

Run: `meson test -C builddir test_split -v`
Expected: PASS

- [ ] **Step 13: Run the full test suite**

Run: `meson test -C builddir`
Expected: all tests pass (read the actual pass/fail summary line, don't assume)

- [ ] **Step 14: Commit**

```bash
git add include/prism/ui/delegate.hpp include/prism/ui/layout.hpp \
        include/prism/app/widget_tree.hpp tests/test_layout.cpp \
        tests/test_split.cpp tests/meson.build
git commit -m "feat(layout): add Handle primitive rendering as a fixed-thickness bar"
```

---

## Task 3: `split_sizes` override in Row/Column measure

**Files:**
- Modify: `include/prism/ui/layout.hpp` (`LayoutNode` struct, `detail::measure_linear`)
- Test: `tests/test_layout.cpp`

**Interfaces:**
- Consumes: `LayoutNode::Kind::Handle` (Task 2).
- Produces: `LayoutNode::split_sizes` (`std::vector<float>`) — consumed by Task 5 (`build_layout` populates it from `SplitState`).

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_layout.cpp`:

```cpp
TEST_CASE("Row measure ignores split_sizes when empty (regression pin)") {
    LayoutNode row;
    row.kind = LayoutNode::Kind::Row;
    row.id = 1;

    LayoutNode leaf;
    leaf.kind = LayoutNode::Kind::Leaf;
    leaf.id = 2;
    leaf.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{80}, Height{20}}}, Color::rgba(255, 0, 0));
    row.children.push_back(std::move(leaf));

    LayoutNode handle;
    handle.kind = LayoutNode::Kind::Handle;
    handle.id = 3;
    row.children.push_back(std::move(handle));

    LayoutNode leaf2;
    leaf2.kind = LayoutNode::Kind::Leaf;
    leaf2.id = 4;
    leaf2.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{120}, Height{20}}}, Color::rgba(0, 255, 0));
    row.children.push_back(std::move(leaf2));

    layout_measure(row, LayoutAxis::Vertical);

    CHECK(row.children[0].hint.preferred == doctest::Approx(80.f));
    CHECK(row.children[1].hint.preferred == doctest::Approx(splitter::thickness_px));
    CHECK(row.children[2].hint.preferred == doctest::Approx(120.f));
}

TEST_CASE("Row measure uses split_sizes for panes when engaged, ignoring content") {
    LayoutNode row;
    row.kind = LayoutNode::Kind::Row;
    row.id = 1;
    row.split_sizes = {150.f, 250.f};

    LayoutNode leaf;
    leaf.kind = LayoutNode::Kind::Leaf;
    leaf.id = 2;
    leaf.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{80}, Height{20}}}, Color::rgba(255, 0, 0));
    row.children.push_back(std::move(leaf));

    LayoutNode handle;
    handle.kind = LayoutNode::Kind::Handle;
    handle.id = 3;
    row.children.push_back(std::move(handle));

    LayoutNode leaf2;
    leaf2.kind = LayoutNode::Kind::Leaf;
    leaf2.id = 4;
    leaf2.draws.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{120}, Height{20}}}, Color::rgba(0, 255, 0));
    row.children.push_back(std::move(leaf2));

    layout_measure(row, LayoutAxis::Vertical);

    CHECK(row.children[0].hint.preferred == doctest::Approx(150.f));
    CHECK(row.children[1].hint.preferred == doctest::Approx(splitter::thickness_px));
    CHECK(row.children[2].hint.preferred == doctest::Approx(250.f));
    CHECK_FALSE(row.children[0].hint.expand);
    CHECK_FALSE(row.children[2].hint.expand);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_layout -v`
Expected: first test PASSes already (regression pin, nothing to override yet); second test FAILs — `LayoutNode` has no member `split_sizes`, and even once it compiles, panes would measure from content (80/120), not (150/250).

- [ ] **Step 3: Add `split_sizes` field and the override**

In `include/prism/ui/layout.hpp`, add to `LayoutNode` (near `table_header_h`):

```cpp
    Height table_header_h{0};
    std::vector<float> split_sizes;  // main-axis size per pane; meaningful only for Kind::Row/Column when split mode is engaged
    const Theme* theme = &detail::layout_default_theme;
```

Change `detail::measure_linear`:

```cpp
inline void measure_linear(LayoutNode& node, LayoutAxis own_axis, LayoutAxis parent_axis) {
    float sum = 0, max_cross = 0;
    bool has_expander = false;
    size_t pane_index = 0;
    for (auto& child : node.children) {
        layout_measure(child, own_axis);
        if (!node.split_sizes.empty() && child.kind != LayoutNode::Kind::Handle) {
            if (pane_index < node.split_sizes.size()) {
                child.hint.preferred = node.split_sizes[pane_index];
                child.hint.expand = false;
            }
            ++pane_index;
        }
        sum += child.hint.preferred;
        max_cross = std::max(max_cross, child.hint.cross);
        if (child.hint.expand) has_expander = true;
    }
    bool aligned = (own_axis == parent_axis);
    node.hint.preferred = aligned ? sum : max_cross;
    node.hint.cross = aligned ? max_cross : sum;
    if (has_expander) node.hint.expand = true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test -C builddir test_layout -v`
Expected: both PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/layout.hpp tests/test_layout.cpp
git commit -m "feat(layout): Row/Column measure reads pane sizes from split_sizes when engaged"
```

---

## Task 4: `WidgetNode::arranged_extent` for every node kind

**Files:**
- Modify: `include/prism/ui/widget_node.hpp:27-67` (`WidgetNode` struct)
- Modify: `include/prism/app/widget_tree.hpp:1310-1320` (`update_canvas_bounds`)
- Test: `tests/test_split.cpp`

**Interfaces:**
- Produces: `WidgetNode::arranged_extent` (`Size`) — the last-arranged main-axis extent for *any* node kind (unlike `canvas_bounds`, which only `update_canvas_bounds` populates for `Leaf`/`Canvas`/`Handle`). Consumed by Task 5's `begin_split_drag` (needs the actual size of container-kind panes, e.g. a nested `vstack` or a `VirtualList`, which never get `canvas_bounds`).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_split.cpp`:

```cpp
struct NestedPaneModel {
    Field<int> a{0};
    Field<int> b{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(a);   // leaf pane
            vb.handle();
            vb.vstack([&] { vb.widget(b); });  // container pane
        });
    }
};

TEST_CASE("arranged_extent is populated for both leaf and container panes") {
    NestedPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(snap->geometry.size() >= 2);

    // geometry[0] is the leaf pane's own rect; the container pane's rect is
    // whatever the nested vstack produces as its outermost geometry entry.
    // We assert via the public snapshot instead of poking WidgetNode directly
    // (WidgetTree::index_ is private) — cross-check arranged_extent indirectly
    // by confirming the total layout is self-consistent and the container's
    // child widget got a real, nonzero allocation.
    auto& [pane0_id, pane0_rect] = snap->geometry[0];
    CHECK(pane0_rect.extent.w.raw() > 0.f);

    bool found_nonzero_nested = false;
    for (auto& [id, rect] : snap->geometry) {
        if (id != pane0_id && rect.extent.w.raw() > 0.f) found_nonzero_nested = true;
    }
    CHECK(found_nonzero_nested);
}
```

Note: this test only exercises the *observable* consequence (the nested container's child still gets laid out correctly) since `arranged_extent` itself isn't reachable from outside `WidgetTree`. The real proof that `arranged_extent` reaches container panes comes in Task 5's drag test, which depends on it.

- [ ] **Step 2: Run test to verify it fails or passes**

Run: `meson test -C builddir test_split -v`
Expected: PASS already (this test doesn't yet depend on the new field — it's a pre-existing-behavior sanity check kept as a placeholder assertion for Task 5 to build on). If it passes trivially, that's expected; proceed to add the field so Task 5 has something to consume.

- [ ] **Step 3: Add the field**

In `include/prism/ui/widget_node.hpp`, add to `WidgetNode` right after `canvas_bounds`:

```cpp
    Rect canvas_bounds{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    Size arranged_extent{Width{0}, Height{0}};  // last-arranged main-axis extent, populated for every node kind
```

- [ ] **Step 4: Populate it in `update_canvas_bounds`**

In `include/prism/app/widget_tree.hpp`, change:

```cpp
    void update_canvas_bounds(LayoutNode& layout_node, Height viewport_h = Height{0}) {
        // Propagate absolute Y and viewport height to all indexed widget nodes
        if (layout_node.id != 0) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                it->second->absolute_x = layout_node.allocated.origin.x;
                it->second->absolute_y = layout_node.allocated.origin.y;
                it->second->viewport_height = viewport_h;
                it->second->arranged_extent = layout_node.allocated.extent;
            }
        }
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir test_split -v`
Expected: PASS

- [ ] **Step 6: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass

- [ ] **Step 7: Commit**

```bash
git add include/prism/ui/widget_node.hpp include/prism/app/widget_tree.hpp tests/test_split.cpp
git commit -m "feat(widget-tree): populate arranged_extent for every node kind, not just leaves"
```

---

## Task 5: `SplitState` and drag-driven layout engagement

**Files:**
- Modify: `include/prism/ui/delegate.hpp` (add `SplitState` beside `ScrollState`)
- Modify: `include/prism/app/widget_tree.hpp` (`build_layout`'s Row/Column branch, new `SplitDrag` struct + `begin_split_drag`/`update_split_drag`/`end_split_drag`/`in_split_drag` methods, `split_drag_` member)
- Test: `tests/test_split.cpp`

**Interfaces:**
- Consumes: `LayoutNode::split_sizes` (Task 3), `WidgetNode::arranged_extent` (Task 4), `splitter::min_pane_size_px` (Task 2).
- Produces: `SplitState{engaged, pane_sizes}`, `WidgetTree::begin_split_drag(WidgetId container_id, size_t handle_index, float pos)`, `WidgetTree::update_split_drag(float pos)`, `WidgetTree::end_split_drag()`, `WidgetTree::in_split_drag() -> bool`. Consumed by Task 6 (real input wiring) and Task 7 (rescale).

- [ ] **Step 1: Write the failing tests**

The test needs the container's own `WidgetId` to call `begin_split_drag(container_id, ...)` directly. `snap->geometry` only contains leaf-level ids (the two panes and the handle) — a Row/Column container has no geometry entry of its own. So `hstack`'s single-lambda overload must report back the id of the container it just built, the same way `canvas()` already returns a `CanvasHandle` referencing its built node. Change it (and `vstack`'s equivalent) in `include/prism/app/widget_tree.hpp`:

```cpp
        WidgetId hstack(std::invocable auto&& fn) { return push_container(LayoutKind::Row, fn).id; }
        void hstack(auto&... args) { push_container(LayoutKind::Row, [&] { (item(args), ...); }); }
        WidgetId vstack(std::invocable auto&& fn) { return push_container(LayoutKind::Column, fn).id; }
```

(`push_container` already returns `Node&`; only the two `std::invocable` overloads change their return type from `void`. The parameter-pack overloads stay `void` — nothing needs their id.)

Add to `tests/test_split.cpp`:

```cpp
struct TwoPaneModelWithId {
    Field<int> a{0};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("First drag engages split mode and resizes exactly the two adjacent panes") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(model.container_id != 0);
    REQUIRE(snap->geometry.size() == 3);

    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto [pane1_id, pane1_rect] = snap->geometry[2];
    float pane0_w0 = pane0_rect.extent.w.raw();
    float pane1_w0 = pane1_rect.extent.w.raw();

    float anchor = handle_rect.origin.x.raw() + handle_rect.extent.w.raw() / 2.f;
    tree.begin_split_drag(model.container_id, 0, anchor);
    CHECK(tree.in_split_drag());

    tree.update_split_drag(anchor + 20.f);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    auto [handle_id2, handle_rect2] = snap2->geometry[1];
    auto [pane1_id2, pane1_rect2] = snap2->geometry[2];

    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0 + 20.f));
    CHECK(pane1_rect2.extent.w.raw() == doctest::Approx(pane1_w0 - 20.f));

    tree.end_split_drag();
    CHECK_FALSE(tree.in_split_drag());
}

TEST_CASE("Dragging clamps at the minimum pane size") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];

    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    // Drag far past pane0's minimum.
    tree.update_split_drag(anchor - 10000.f);

    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(splitter::min_pane_size_px));
}

struct ThreePaneModel {
    Field<int> a{0}, b{0}, c{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
            vb.handle();
            vb.widget(c);
        });
    }
};

TEST_CASE("Dragging one handle in a 3-pane row leaves the far pane untouched") {
    ThreePaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(606, 100, 1);
    REQUIRE(snap->geometry.size() == 5);

    auto [id0, r0] = snap->geometry[0];
    auto [hid0, hr0] = snap->geometry[1];
    auto [id1, r1] = snap->geometry[2];
    auto [hid1, hr1] = snap->geometry[3];
    auto [id2, r2] = snap->geometry[4];
    float w2_before = r2.extent.w.raw();

    float anchor = hr0.origin.x.raw();
    tree.begin_split_drag(model.container_id, /*handle_index=*/0, anchor);
    tree.update_split_drag(anchor + 15.f);

    auto snap2 = tree.build_snapshot(606, 100, 2);
    auto [id2b, r2b] = snap2->geometry[4];
    CHECK(r2b.extent.w.raw() == doctest::Approx(w2_before));
}

struct ExpandingPaneModel {
    Field<Slider<double>> s{{.value = 0.5}};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.hstack([&] {
            vb.widget(s);   // Widget<Slider<>>::expand_axis == Horizontal -- expand=true here
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("First drag captures an expanding pane's real allocated size, not a fallback") {
    ExpandingPaneModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(500, 100, 1);
    REQUIRE(snap->geometry.size() == 3);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    float pane0_w0 = pane0_rect.extent.w.raw();
    // The slider pane expands to fill the row minus the fixed-width int pane
    // and the handle -- confirm it is NOT the 200px default_widget_w fallback.
    CHECK(pane0_w0 > 200.f);

    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    tree.update_split_drag(anchor); // no movement -- just engage

    auto snap2 = tree.build_snapshot(500, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    // Engaging with zero drag delta must reproduce the exact pre-drag width --
    // proves the captured size came from the real expand-filled allocation,
    // not some smaller content-bbox fallback that would visibly snap the pane
    // narrower the instant a drag starts.
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir test_split -v`
Expected: FAIL to compile — no `SplitState`, no `begin_split_drag`/`update_split_drag`/`end_split_drag`/`in_split_drag`.

- [ ] **Step 3: Add `SplitState`**

In `include/prism/ui/delegate.hpp`, right after `struct ScrollState { ... };`:

```cpp
// Ephemeral split-pane state (stored in WidgetNode::edit_state on a Row/Column container)
struct SplitState {
    bool engaged = false;           // true once the user has dragged a handle at least once
    std::vector<float> pane_sizes;  // main-axis size per pane (excludes Handle thickness); valid when engaged
};
```

- [ ] **Step 5: `build_layout` reads `SplitState` into `split_sizes`**

In `include/prism/app/widget_tree.hpp`, change the `Row`/`Column` branch of `build_layout`:

```cpp
        } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
            LayoutNode container;
            container.kind = (node.layout_kind == LK::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            container.id = node.id;
            container.theme = node.theme;
            if (auto* ss = std::any_cast<SplitState>(&node.edit_state); ss && ss->engaged)
                container.split_sizes = ss->pane_sizes;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else if (node.layout_kind == LK::Tabs) {
```

(This is a reordering-free change: insert the `if (auto* ss = ...)` line into the existing Row/Column `else if` block, right after `container.theme = node.theme;`.)

- [ ] **Step 6: Add `SplitDrag` and the drag methods**

In `include/prism/app/widget_tree.hpp`, add the member struct right after `struct ScrollbarDrag { ... };`:

```cpp
    struct SplitDrag {
        WidgetId container_id = 0;
        size_t handle_index = 0;
        float anchor = 0.f;
        float orig_before = 0.f;
        float orig_after = 0.f;
    };
```

Add the methods right after `end_scrollbar_drag()`/`in_scrollbar_drag()`:

```cpp
    void begin_split_drag(WidgetId container_id, size_t handle_index, float pos) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto& container_wn = *it->second;
        auto& ss = container_wn.get_or_create<SplitState>();
        bool vertical = (container_wn.layout_kind == LayoutKind::Column);
        if (!ss.engaged) {
            ss.pane_sizes.clear();
            for (auto& child : container_wn.children) {
                if (child.layout_kind == LayoutKind::Handle) continue;
                ss.pane_sizes.push_back(vertical ? child.arranged_extent.h.raw()
                                                  : child.arranged_extent.w.raw());
            }
            ss.engaged = true;
        }
        if (handle_index + 1 >= ss.pane_sizes.size()) return;
        split_drag_ = SplitDrag{
            .container_id = container_id,
            .handle_index = handle_index,
            .anchor = pos,
            .orig_before = ss.pane_sizes[handle_index],
            .orig_after = ss.pane_sizes[handle_index + 1],
        };
    }

    void update_split_drag(float pos) {
        if (split_drag_.container_id == 0) return;
        auto it = index_.find(split_drag_.container_id);
        if (it == index_.end()) return;
        auto* ss = std::any_cast<SplitState>(&it->second->edit_state);
        if (!ss) return;
        float delta = pos - split_drag_.anchor;
        float before = split_drag_.orig_before + delta;
        float after = split_drag_.orig_after - delta;
        if (before < splitter::min_pane_size_px) {
            after -= (splitter::min_pane_size_px - before);
            before = splitter::min_pane_size_px;
        }
        if (after < splitter::min_pane_size_px) {
            before -= (splitter::min_pane_size_px - after);
            after = splitter::min_pane_size_px;
        }
        ss->pane_sizes[split_drag_.handle_index] = before;
        ss->pane_sizes[split_drag_.handle_index + 1] = after;
        set_dirty(split_drag_.container_id);
    }

    void end_split_drag() {
        split_drag_ = {};
    }

    [[nodiscard]] bool in_split_drag() const { return split_drag_.container_id != 0; }
```

Add the member variable right after `ScrollbarDrag scrollbar_drag_;`:

```cpp
    ScrollbarDrag scrollbar_drag_;
    SplitDrag split_drag_;
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `meson test -C builddir test_split -v`
Expected: PASS

- [ ] **Step 7: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass

- [ ] **Step 8: Commit**

```bash
git add include/prism/ui/delegate.hpp include/prism/app/widget_tree.hpp tests/test_split.cpp
git commit -m "feat(widget-tree): SplitState + begin/update/end_split_drag engage resizable layout on first drag"
```

---

## Task 6: Real input wiring (press/drag/release on the handle itself)

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (`push_container`, new private `wire_split_handles`)
- Test: `tests/test_split.cpp`

**Interfaces:**
- Consumes: `WidgetTree::begin_split_drag`/`update_split_drag`/`end_split_drag` (Task 5).
- Produces: automatic drag behavior for every `vb.handle()` placed inside an `hstack`/`vstack` — no new public API; this is purely internal wiring exercised by user interaction.

Note on why this needs its own math, not just localized event coordinates: the handle's own rect moves every frame as a *direct result* of the drag it's tracking (its neighbor pane grows/shrinks). Using the event's already-localized position (relative to the handle's own, moving, rect) would make the tracked delta reference a shifting origin and desync from the real cumulative mouse movement. The fix is to reconstruct the *absolute* mouse position by adding the handle's last-known `absolute_x`/`absolute_y` back to the localized coordinate before calling into `WidgetTree`.

- [ ] **Step 1: Write the failing real-pipeline test**

Add to `tests/test_split.cpp` (mirror `tests/test_tree_view_builder.cpp`'s "clicking a row through the real hit_test/route_mouse_button pipeline" test style — drive `prism::hit_test` + `prism::app::detail::route_mouse_button`/`route_mouse_move`, no shortcuts):

```cpp
TEST_CASE("Pressing and dragging the handle through the real input pipeline resizes panes") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    REQUIRE(snap->geometry.size() == 3);

    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto [pane1_id, pane1_rect] = snap->geometry[2];
    float pane0_w0 = pane0_rect.extent.w.raw();
    float pane1_w0 = pane1_rect.extent.w.raw();

    Point press_pos{X{handle_rect.origin.x.raw() + 2.f}, Y{handle_rect.origin.y.raw() + 5.f}};
    auto hit = prism::hit_test(*snap, press_pos);
    REQUIRE(hit.has_value());
    CHECK(*hit == handle_id);

    MouseButton press{press_pos, /*button=*/1, /*pressed=*/true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);
    CHECK(tree.captured_id() == handle_id);

    // Two separate MouseMove events, each a real 10px rightward mouse movement.
    // This specifically guards the absolute-position fix described in this
    // task: if the handle's own (moving) rect were used to localize
    // subsequent events instead of reconstructing absolute position, the
    // second move would not add another clean 10px -- it would desync.
    MouseMove move1{Point{X{press_pos.x.raw() + 10.f}, press_pos.y}};
    prism::app::detail::route_mouse_move(tree, *snap, move1);
    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    CHECK(pane0_rect2.extent.w.raw() == doctest::Approx(pane0_w0 + 10.f));

    MouseMove move2{Point{X{press_pos.x.raw() + 20.f}, press_pos.y}};
    prism::app::detail::route_mouse_move(tree, *snap2, move2);
    auto snap3 = tree.build_snapshot(406, 100, 3);
    auto [pane0_id3, pane0_rect3] = snap3->geometry[0];
    CHECK(pane0_rect3.extent.w.raw() == doctest::Approx(pane0_w0 + 20.f));

    MouseButton release{Point{X{press_pos.x.raw() + 20.f}, press_pos.y}, 1, false};
    InputEvent release_ev{release};
    prism::app::detail::route_mouse_button(tree, *snap3, release_ev, release);
    CHECK(tree.captured_id() == 0);

    auto snap4 = tree.build_snapshot(406, 100, 4);
    auto [pane1_id4, pane1_rect4] = snap4->geometry[2];
    CHECK(pane1_rect4.extent.w.raw() == doctest::Approx(pane1_w0 - 20.f));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_split -v`
Expected: FAIL — pressing/dragging the handle currently does nothing (no `wire` set on it), so `pane0_rect2` stays equal to `pane0_w0`.

- [ ] **Step 3: Add `wire_split_handles` and hook it into `push_container`**

In `include/prism/app/widget_tree.hpp`, change `push_container`:

```cpp
        Node& push_container(LayoutKind kind, std::invocable auto&& fn) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = kind;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            auto& ref = parent.children.back();
            stack_.push_back(&ref);
            fn();
            stack_.pop_back();
            if (kind == LayoutKind::Row || kind == LayoutKind::Column)
                wire_split_handles(ref);
            return ref;
        }

        void wire_split_handles(Node& container) {
            bool vertical = (container.layout_kind == LayoutKind::Column);
            WidgetId container_id = container.id;
            size_t handle_index = 0;
            for (auto& child : container.children) {
                if (child.layout_kind != LayoutKind::Handle) continue;
                size_t index = handle_index++;
                child.build_widget = [&tree = tree_, container_id, index, vertical](WidgetNode& wn) {
                    wn.focus_policy = FocusPolicy::none;
                    wn.record = [](WidgetNode& node) {
                        node.draws.clear();
                        auto& vs = node_vs(node);
                        auto& t = node_theme(node);
                        auto sz = node_allocated(node);
                        auto color = (vs.pressed || vs.hovered) ? t.divider_hover : t.divider;
                        node.draws.filled_rect(detail::make_rect(X{0}, Y{0}, sz.w, sz.h), color);
                    };
                    wn.wire = [&tree, container_id, index, vertical](WidgetNode& self) {
                        self.connections.push_back(self.on_input.connect(
                            [&tree, container_id, index, vertical, &self](const InputEvent& ev) {
                                float origin = vertical ? self.absolute_y.raw() : self.absolute_x.raw();
                                if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == 1) {
                                    float local = vertical ? mb->position.y.raw() : mb->position.x.raw();
                                    if (mb->pressed) tree.begin_split_drag(container_id, index, local + origin);
                                    else tree.end_split_drag();
                                } else if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(self).pressed) {
                                    float local = vertical ? mm->position.y.raw() : mm->position.x.raw();
                                    tree.update_split_drag(local + origin);
                                }
                            }));
                    };
                    wn.record(wn);
                };
            }
        }
```

No change is needed to `handle()` itself: it still sets its Task 2 record-only `build_widget` (so a `Handle` would still render even if placed somewhere `wire_split_handles` doesn't reach). `wire_split_handles` runs after `fn()` has populated `container.children`, and simply reassigns `child.build_widget` on every Handle child it finds, replacing the plain one with the full record+wire version above.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_split -v`
Expected: PASS

- [ ] **Step 5: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_split.cpp
git commit -m "feat(widget-tree): wire real press/drag/release input on split handles"
```

---

## Task 7: Proportional rescale on parent resize

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (new `update_split_state`, call it from `build_snapshot`)
- Test: `tests/test_split.cpp`

**Interfaces:**
- Consumes: `SplitState` (Task 5), `LayoutNode::split_sizes`/`allocated` (Tasks 3, existing).
- Produces: `WidgetTree::update_split_state(LayoutNode&)` (private) — no public API; behavior-only change.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_split.cpp`:

```cpp
TEST_CASE("Resizing the window after a drag proportionally rescales pane sizes on the next frame") {
    TwoPaneModelWithId model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(406, 100, 1);
    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];

    // Engage split mode with an uneven split: pane0 grows by 100px.
    float anchor = handle_rect.origin.x.raw();
    tree.begin_split_drag(model.container_id, 0, anchor);
    tree.update_split_drag(anchor + 100.f);
    auto snap2 = tree.build_snapshot(406, 100, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    auto [pane1_id2, pane1_rect2] = snap2->geometry[2];
    float ratio_before = pane0_rect2.extent.w.raw()
        / (pane0_rect2.extent.w.raw() + pane1_rect2.extent.w.raw());

    // Double the window width. The rescale is documented as taking effect on
    // the frame *after* the mismatch is observed (same "detect now, correct
    // next frame" pattern already used for table/vlist viewport sizing).
    auto snap3 = tree.build_snapshot(812, 100, 3);
    auto snap4 = tree.build_snapshot(812, 100, 4);
    auto [pane0_id4, pane0_rect4] = snap4->geometry[0];
    auto [pane1_id4, pane1_rect4] = snap4->geometry[2];
    float ratio_after = pane0_rect4.extent.w.raw()
        / (pane0_rect4.extent.w.raw() + pane1_rect4.extent.w.raw());

    CHECK(ratio_after == doctest::Approx(ratio_before).epsilon(0.02));
    CHECK(pane0_rect4.extent.w.raw() + pane1_rect4.extent.w.raw()
          == doctest::Approx(812.f - splitter::thickness_px).epsilon(0.02));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_split -v`
Expected: FAIL — without rescale, `pane_sizes` stays fixed in absolute pixels, so after doubling the window the two panes no longer sum to the new available width and the ratio check may pass by coincidence but the sum check fails.

- [ ] **Step 3: Add `update_split_state`**

In `include/prism/app/widget_tree.hpp`, add right after `update_scroll_state`:

```cpp
    void update_split_state(LayoutNode& layout_node) {
        bool is_split_container = (layout_node.kind == LayoutNode::Kind::Row
                                 || layout_node.kind == LayoutNode::Kind::Column)
                                 && !layout_node.split_sizes.empty();
        if (is_split_container) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                if (auto* ss = std::any_cast<SplitState>(&it->second->edit_state); ss && ss->engaged) {
                    bool vertical = (layout_node.kind == LayoutNode::Kind::Column);
                    float available = vertical ? layout_node.allocated.extent.h.raw()
                                                : layout_node.allocated.extent.w.raw();
                    float handle_total = 0.f;
                    for (auto& child : layout_node.children)
                        if (child.kind == LayoutNode::Kind::Handle)
                            handle_total += splitter::thickness_px;
                    float pane_total = 0.f;
                    for (float sz : ss->pane_sizes) pane_total += sz;
                    float target = available - handle_total;
                    if (pane_total > 0.f && std::abs(target - pane_total) > 0.5f) {
                        float scale = target / pane_total;
                        for (auto& sz : ss->pane_sizes) sz *= scale;
                    }
                }
            }
        }
        for (auto& child : layout_node.children)
            update_split_state(child);
    }
```

Call it from `build_snapshot`'s `do_layout` lambda, right after `update_scroll_state(layout);`:

```cpp
            layout_measure(layout, LayoutAxis::Vertical);
            layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});
            update_scroll_state(layout);
            update_split_state(layout);
            return layout;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_split -v`
Expected: PASS

- [ ] **Step 5: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_split.cpp
git commit -m "feat(widget-tree): proportionally rescale pane sizes on parent resize"
```

---

## Task 8: vstack symmetry (Column-axis dragging)

**Files:**
- Test only: `tests/test_split.cpp`

**Interfaces:**
- Consumes: everything built in Tasks 2–7. No production code is expected to change in this task — it's a dedicated behavioral proof that the axis-agnostic design (Tasks 2–7 all branch on `vertical` derived from `layout_kind == Column`) actually holds for `vstack`. If it fails, fix the specific spot the failure points to rather than adding new vstack-specific code paths.

- [ ] **Step 1: Write the test**

Add to `tests/test_split.cpp`:

```cpp
struct TwoPaneColumnModel {
    Field<int> a{0};
    Field<int> b{0};
    WidgetId container_id = 0;

    void view(WidgetTree::ViewBuilder& vb) {
        container_id = vb.vstack([&] {
            vb.widget(a);
            vb.handle();
            vb.widget(b);
        });
    }
};

TEST_CASE("Dragging a handle in a vstack resizes panes along the Y axis") {
    TwoPaneColumnModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(200, 406, 1);
    REQUIRE(snap->geometry.size() == 3);

    auto [pane0_id, pane0_rect] = snap->geometry[0];
    auto [handle_id, handle_rect] = snap->geometry[1];
    auto [pane1_id, pane1_rect] = snap->geometry[2];
    float pane0_h0 = pane0_rect.extent.h.raw();
    float pane1_h0 = pane1_rect.extent.h.raw();

    Point press_pos{X{handle_rect.origin.x.raw() + 5.f}, Y{handle_rect.origin.y.raw() + 2.f}};
    MouseButton press{press_pos, 1, true};
    InputEvent press_ev{press};
    prism::app::detail::route_mouse_button(tree, *snap, press_ev, press);
    CHECK(tree.captured_id() == handle_id);

    MouseMove move{Point{press_pos.x, Y{press_pos.y.raw() + 15.f}}};
    prism::app::detail::route_mouse_move(tree, *snap, move);

    auto snap2 = tree.build_snapshot(200, 406, 2);
    auto [pane0_id2, pane0_rect2] = snap2->geometry[0];
    auto [pane1_id2, pane1_rect2] = snap2->geometry[2];
    CHECK(pane0_rect2.extent.h.raw() == doctest::Approx(pane0_h0 + 15.f));
    CHECK(pane1_rect2.extent.h.raw() == doctest::Approx(pane1_h0 - 15.f));
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir test_split -v`
Expected: PASS, given Tasks 2–7 were implemented axis-agnostically as specified. If it fails, the failure will point at whichever spot hardcoded a Row/X assumption — fix that specific line (do not add a parallel vstack-only code path).

- [ ] **Step 3: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass

- [ ] **Step 4: Commit**

```bash
git add tests/test_split.cpp
git commit -m "test(split): confirm vstack drags resize panes along the Y axis"
```

---

## Task 9: Wire the primitive into the tree widget's row-list/detail-panel split

**Files:**
- Modify: `include/prism/app/widget_tree.hpp:287-358` (`ViewBuilder::tree()`)
- Test: `tests/test_tree_view_builder.cpp`

**Interfaces:**
- Consumes: `ViewBuilder::handle()` (Task 2) and everything built on top of it.
- Produces: the file-browser tree/detail split (the motivating example for this whole feature) becomes draggable with no change needed in `examples/model_tree_browser.cpp` — it already just calls `vb.tree(ctrl)`.

- [ ] **Step 1: Write the failing test**

Read `tests/test_tree_view_builder.cpp`'s existing `TreeModel` fixture and `build_snapshot` dimensions first (it's referenced throughout that file), then add:

```cpp
TEST_CASE("vb.tree() places a draggable Handle between the row list and the detail panel") {
    TreeModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);

    // The detail panel is the last child of the tree's hstack; a Handle must
    // now sit immediately before it. Find it by layout_kind via geometry: the
    // handle is the only non-row, non-detail-panel entry with the fixed
    // splitter thickness.
    bool found_handle = false;
    for (auto& [id, rect] : snap->geometry) {
        if (rect.extent.w.raw() == doctest::Approx(prism::splitter::thickness_px).epsilon(0.01)) {
            found_handle = true;
        }
    }
    CHECK(found_handle);
}
```

(Adjust the fixture name/dimensions to match whatever `TreeModel`/dimensions the existing file already uses — check the file first; do not introduce a second, differently-shaped fixture if `TreeModel` already exists there.)

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_tree_view_builder -v`
Expected: FAIL — no handle-width geometry entry exists yet.

- [ ] **Step 3: Insert the handle**

In `include/prism/app/widget_tree.hpp`, inside `ViewBuilder::tree()`, change:

```cpp
                current_parent().children.push_back(std::move(container));
                widget(ctrl.detail);
            });
        }
```

to:

```cpp
                current_parent().children.push_back(std::move(container));
                handle();
                widget(ctrl.detail);
            });
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_tree_view_builder -v`
Expected: PASS

- [ ] **Step 5: Run the full suite**

Run: `meson test -C builddir`
Expected: all pass — read the actual summary line and exit code

- [ ] **Step 6: Manual sanity check (optional but recommended)**

Run the tree-browser example and drag the divider between the file list and the detail panel:

```bash
meson compile -C builddir model_tree_browser
./builddir/examples/model_tree_browser
```

- [ ] **Step 7: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_tree_view_builder.cpp
git commit -m "feat(tree): make the row-list/detail-panel split user-resizable"
```

---

## Final Step: Update memory

After all tasks are committed and the full suite passes, update the PRISM project memory (`docs/superpowers/specs/2026-07-21-resizable-split-panes-design.md` is already committed) with a new entry summarizing the completed feature, following the existing memory format used for Tree/Table/Tabs widgets.
