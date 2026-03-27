# Layout Engine + Hit Regions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `row()`, `column()`, `spacer()` layout containers and `hit_test()` to PRISM, producing per-widget geometry in `SceneSnapshot`.

**Architecture:** During the view pass, `Ui<State>` builds an ephemeral `LayoutNode` tree. After the view lambda returns, a two-pass solver (measure bottom-up, arrange top-down) computes `Rect` per node. The tree is then flattened into per-widget entries in `SceneSnapshot::geometry` and `draw_lists`. A `hit_test()` free function scans geometry in z-order for point-in-rect.

**Tech Stack:** C++26, header-only core (no new dependencies), doctest for tests, Meson build.

**Spec:** `docs/superpowers/specs/2026-03-27-layout-hit-regions-design.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `include/prism/core/layout.hpp` | `SizeHint`, `LayoutNode`, `layout_measure()`, `layout_arrange()`, `layout_flatten()` |
| Create | `include/prism/core/hit_test.hpp` | `hit_test()` free function |
| Create | `tests/test_layout.cpp` | Layout solver unit tests |
| Create | `tests/test_hit_test.cpp` | Hit test unit tests |
| Modify | `include/prism/core/draw_list.hpp` | Add `bounding_box()` to `DrawList` |
| Modify | `include/prism/core/ui.hpp` | Add layout tree building, `row()`, `column()`, `spacer()`, snapshot from layout |
| Modify | `include/prism/prism.hpp` | Include new headers |
| Modify | `tests/meson.build` | Register new test executables |
| Modify | `tests/test_ui.cpp` | Integration tests for layout in `app<State>()` |
| Modify | `examples/hello_rect.cpp` | Rewrite to use layout containers |

---

### Task 1: Add `bounding_box()` to DrawList

**Files:**
- Modify: `include/prism/core/draw_list.hpp:55-76`
- Test: `tests/test_draw_list.cpp`

- [ ] **Step 1: Write the failing test**

Add at the end of `tests/test_draw_list.cpp`:

```cpp
TEST_CASE("bounding_box of empty draw list is zero rect") {
    prism::DrawList dl;
    auto bb = dl.bounding_box();
    CHECK(bb.x == 0);
    CHECK(bb.y == 0);
    CHECK(bb.w == 0);
    CHECK(bb.h == 0);
}

TEST_CASE("bounding_box encompasses all commands") {
    prism::DrawList dl;
    dl.filled_rect({10, 20, 100, 50}, prism::Color::rgba(255, 0, 0));
    dl.filled_rect({50, 0, 30, 200}, prism::Color::rgba(0, 255, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.x == 10);
    CHECK(bb.y == 0);
    CHECK(bb.w == 100);    // max_x=110, min_x=10 -> 100
    CHECK(bb.h == 200);    // max_y=200, min_y=0 -> 200
}

TEST_CASE("bounding_box handles rect_outline") {
    prism::DrawList dl;
    dl.rect_outline({5, 5, 90, 40}, prism::Color::rgba(0, 0, 0), 2.f);
    auto bb = dl.bounding_box();
    CHECK(bb.x == 5);
    CHECK(bb.y == 5);
    CHECK(bb.w == 90);
    CHECK(bb.h == 40);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test draw_list -C builddir --print-errorlogs`
Expected: FAIL — `bounding_box` is not a member of `DrawList`

- [ ] **Step 3: Write minimal implementation**

In `include/prism/core/draw_list.hpp`, add `Rect::contains()` helper and `DrawList::bounding_box()`:

Add to `struct Rect`:
```cpp
[[nodiscard]] bool contains(Point p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}
```

Add to `struct DrawList`, after the `size()` method:
```cpp
[[nodiscard]] Rect bounding_box() const {
    if (commands.empty()) return {0, 0, 0, 0};
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    auto expand = [&](Rect r) {
        min_x = std::min(min_x, r.x);
        min_y = std::min(min_y, r.y);
        max_x = std::max(max_x, r.x + r.w);
        max_y = std::max(max_y, r.y + r.h);
    };
    for (const auto& cmd : commands) {
        std::visit([&](const auto& c) {
            if constexpr (requires { c.rect; })
                expand(c.rect);
            else if constexpr (requires { c.origin; })
                expand({c.origin.x, c.origin.y, 0, c.size});
        }, cmd);
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
}
```

Add `#include <algorithm>` and `#include <limits>` to the top of `draw_list.hpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test draw_list -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "feat: add bounding_box() to DrawList and contains() to Rect"
```

---

### Task 2: Layout data types and measure pass

**Files:**
- Create: `include/prism/core/layout.hpp`
- Create: `tests/test_layout.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests for SizeHint and layout_measure**

Create `tests/test_layout.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/layout.hpp>

TEST_CASE("leaf node measure uses draw list bounding box") {
    prism::LayoutNode leaf;
    leaf.kind = prism::LayoutNode::Kind::Leaf;
    leaf.draws.filled_rect({0, 0, 120, 40}, prism::Color::rgba(255, 0, 0));

    prism::layout_measure(leaf, prism::LayoutAxis::Horizontal);

    CHECK(leaf.hint.preferred == 120);
    CHECK(leaf.hint.cross == 40);
}

TEST_CASE("spacer has expand=true and zero preferred") {
    prism::LayoutNode spacer;
    spacer.kind = prism::LayoutNode::Kind::Spacer;

    prism::layout_measure(spacer, prism::LayoutAxis::Horizontal);

    CHECK(spacer.hint.preferred == 0);
    CHECK(spacer.hint.expand == true);
}

TEST_CASE("row measure sums children preferred widths") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);

    CHECK(row.hint.preferred == 160);  // 100 + 60
    CHECK(row.hint.cross == 80);       // max(50, 80)
}

TEST_CASE("column measure sums children preferred heights") {
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);

    CHECK(col.hint.preferred == 130);  // 50 + 80
    CHECK(col.hint.cross == 100);      // max(100, 60)
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```meson
  'layout' : files('test_layout.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: FAIL — `layout.hpp` does not exist

- [ ] **Step 4: Write minimal implementation**

Create `include/prism/core/layout.hpp`:

```cpp
#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace prism {

enum class LayoutAxis { Horizontal, Vertical };

struct SizeHint {
    float preferred = 0;
    float min = 0;
    float max = std::numeric_limits<float>::max();
    float cross = 0;
    bool expand = false;
};

struct LayoutNode {
    WidgetId id = 0;
    SizeHint hint;
    Rect allocated{0, 0, 0, 0};
    DrawList draws;
    std::vector<LayoutNode> children;
    enum class Kind { Leaf, Row, Column, Spacer } kind = Kind::Leaf;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Leaf: {
        auto bb = node.draws.bounding_box();
        if (parent_axis == LayoutAxis::Horizontal) {
            node.hint.preferred = bb.w;
            node.hint.cross = bb.h;
        } else {
            node.hint.preferred = bb.h;
            node.hint.cross = bb.w;
        }
        return;
    }
    case LayoutNode::Kind::Row: {
        LayoutAxis child_axis = LayoutAxis::Horizontal;
        float sum = 0, max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, child_axis);
            sum += child.hint.preferred;
            max_cross = std::max(max_cross, child.hint.cross);
        }
        if (parent_axis == LayoutAxis::Horizontal) {
            node.hint.preferred = sum;
            node.hint.cross = max_cross;
        } else {
            node.hint.preferred = max_cross;
            node.hint.cross = sum;
        }
        return;
    }
    case LayoutNode::Kind::Column: {
        LayoutAxis child_axis = LayoutAxis::Vertical;
        float sum = 0, max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, child_axis);
            sum += child.hint.preferred;
            max_cross = std::max(max_cross, child.hint.cross);
        }
        if (parent_axis == LayoutAxis::Vertical) {
            node.hint.preferred = sum;
            node.hint.cross = max_cross;
        } else {
            node.hint.preferred = max_cross;
            node.hint.cross = sum;
        }
        return;
    }
    }
}

} // namespace prism
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/layout.hpp tests/test_layout.cpp tests/meson.build
git commit -m "feat: add SizeHint, LayoutNode, and layout_measure()"
```

---

### Task 3: Arrange pass

**Files:**
- Modify: `include/prism/core/layout.hpp`
- Modify: `tests/test_layout.cpp`

- [ ] **Step 1: Write failing tests for layout_arrange**

Append to `tests/test_layout.cpp`:

```cpp
TEST_CASE("arrange row distributes width to children") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 400, 300});

    CHECK(row.allocated.x == 0);
    CHECK(row.allocated.w == 400);
    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 100);
    CHECK(row.children[0].allocated.h == 300);  // stretch cross-axis
    CHECK(row.children[1].allocated.x == 100);
    CHECK(row.children[1].allocated.w == 60);
    CHECK(row.children[1].allocated.h == 300);
}

TEST_CASE("arrange row with spacer distributes remaining space") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 500, 200});

    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 100);
    CHECK(row.children[1].allocated.x == 100);
    CHECK(row.children[1].allocated.w == 300);  // 500 - 100 - 100
    CHECK(row.children[2].allocated.x == 400);
    CHECK(row.children[2].allocated.w == 100);
}

TEST_CASE("arrange column distributes height to children") {
    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.draws.filled_rect({0, 0, 60, 60}, prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(a));
    col.children.push_back(std::move(b));

    prism::layout_measure(col, prism::LayoutAxis::Vertical);
    prism::layout_arrange(col, {10, 20, 300, 400});

    CHECK(col.allocated.x == 10);
    CHECK(col.allocated.y == 20);
    CHECK(col.children[0].allocated.x == 10);
    CHECK(col.children[0].allocated.y == 20);
    CHECK(col.children[0].allocated.w == 300);  // stretch cross-axis
    CHECK(col.children[0].allocated.h == 40);
    CHECK(col.children[1].allocated.x == 10);
    CHECK(col.children[1].allocated.y == 60);   // 20 + 40
    CHECK(col.children[1].allocated.h == 60);
}

TEST_CASE("arrange nested: column inside row") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;

    prism::LayoutNode left;
    left.kind = prism::LayoutNode::Kind::Leaf;
    left.draws.filled_rect({0, 0, 200, 100}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;

    prism::LayoutNode col;
    col.kind = prism::LayoutNode::Kind::Column;

    prism::LayoutNode ca;
    ca.kind = prism::LayoutNode::Kind::Leaf;
    ca.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(0, 0, 255));

    prism::LayoutNode cb;
    cb.kind = prism::LayoutNode::Kind::Leaf;
    cb.draws.filled_rect({0, 0, 100, 40}, prism::Color::rgba(0, 255, 0));

    col.children.push_back(std::move(ca));
    col.children.push_back(std::move(cb));

    row.children.push_back(std::move(left));
    row.children.push_back(std::move(sp));
    row.children.push_back(std::move(col));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 800, 600});

    // left: 200px, spacer: 800-200-100=500, col: 100px
    CHECK(row.children[0].allocated.x == 0);
    CHECK(row.children[0].allocated.w == 200);
    CHECK(row.children[1].allocated.x == 200);
    CHECK(row.children[1].allocated.w == 500);
    CHECK(row.children[2].allocated.x == 700);
    CHECK(row.children[2].allocated.w == 100);
    // column children within the column
    CHECK(row.children[2].children[0].allocated.x == 700);
    CHECK(row.children[2].children[0].allocated.y == 0);
    CHECK(row.children[2].children[0].allocated.h == 40);
    CHECK(row.children[2].children[1].allocated.y == 40);
    CHECK(row.children[2].children[1].allocated.h == 40);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: FAIL — `layout_arrange` not defined

- [ ] **Step 3: Write minimal implementation**

Add to `include/prism/core/layout.hpp`, after `layout_measure()`:

```cpp
inline void layout_arrange(LayoutNode& node, Rect available) {
    node.allocated = available;

    if (node.children.empty()) return;

    bool horizontal = (node.kind == LayoutNode::Kind::Row);

    // Count expanders and sum preferred sizes
    float total_preferred = 0;
    int expand_count = 0;
    for (auto& child : node.children) {
        if (child.hint.expand)
            ++expand_count;
        else
            total_preferred += child.hint.preferred;
    }

    float remaining = (horizontal ? available.w : available.h) - total_preferred;
    float expand_share = (expand_count > 0) ? std::max(0.f, remaining) / expand_count : 0;

    float offset = 0;
    for (auto& child : node.children) {
        float main_size = child.hint.expand ? expand_share : child.hint.preferred;
        Rect child_rect;
        if (horizontal) {
            child_rect = {available.x + offset, available.y, main_size, available.h};
        } else {
            child_rect = {available.x, available.y + offset, available.w, main_size};
        }
        layout_arrange(child, child_rect);
        offset += main_size;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/layout.hpp tests/test_layout.cpp
git commit -m "feat: add layout_arrange() two-pass layout solver"
```

---

### Task 4: Flatten layout tree into SceneSnapshot

**Files:**
- Modify: `include/prism/core/layout.hpp`
- Modify: `tests/test_layout.cpp`

- [ ] **Step 1: Write failing tests for layout_flatten**

Append to `tests/test_layout.cpp`:

```cpp
TEST_CASE("flatten produces per-widget geometry and draw lists") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode b;
    b.kind = prism::LayoutNode::Kind::Leaf;
    b.id = 2;
    b.draws.filled_rect({0, 0, 60, 80}, prism::Color::rgba(0, 255, 0));

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(b));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 400, 300});

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    // Two leaf nodes with draw commands
    CHECK(snap.geometry.size() == 2);
    CHECK(snap.draw_lists.size() == 2);
    CHECK(snap.z_order.size() == 2);

    CHECK(snap.geometry[0].first == 1);
    CHECK(snap.geometry[0].second.x == 0);
    CHECK(snap.geometry[0].second.w == 100);

    CHECK(snap.geometry[1].first == 2);
    CHECK(snap.geometry[1].second.x == 100);
    CHECK(snap.geometry[1].second.w == 60);
}

TEST_CASE("flatten translates draw commands to absolute coordinates") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    row.children.push_back(std::move(a));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {30, 40, 400, 300});

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    CHECK(snap.geometry.size() == 1);
    CHECK(snap.geometry[0].second.x == 30);
    CHECK(snap.geometry[0].second.y == 40);

    // The draw command should be translated
    auto& dl = snap.draw_lists[0];
    CHECK(dl.commands.size() == 1);
    auto* fr = std::get_if<prism::FilledRect>(&dl.commands[0]);
    REQUIRE(fr != nullptr);
    CHECK(fr->rect.x == 30);
    CHECK(fr->rect.y == 40);
}

TEST_CASE("flatten skips spacers and empty containers") {
    prism::LayoutNode row;
    row.kind = prism::LayoutNode::Kind::Row;
    row.id = 0;

    prism::LayoutNode a;
    a.kind = prism::LayoutNode::Kind::Leaf;
    a.id = 1;
    a.draws.filled_rect({0, 0, 100, 50}, prism::Color::rgba(255, 0, 0));

    prism::LayoutNode sp;
    sp.kind = prism::LayoutNode::Kind::Spacer;
    sp.id = 2;

    row.children.push_back(std::move(a));
    row.children.push_back(std::move(sp));

    prism::layout_measure(row, prism::LayoutAxis::Horizontal);
    prism::layout_arrange(row, {0, 0, 400, 300});

    prism::SceneSnapshot snap;
    snap.version = 1;
    prism::layout_flatten(row, snap);

    // Only the leaf with draw commands, not the spacer
    CHECK(snap.geometry.size() == 1);
    CHECK(snap.geometry[0].first == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: FAIL — `layout_flatten` not defined

- [ ] **Step 3: Write minimal implementation**

Add to `include/prism/core/layout.hpp`, after `layout_arrange()`:

```cpp
namespace detail {

inline void translate_draw_list(DrawList& dl, float dx, float dy) {
    for (auto& cmd : dl.commands) {
        std::visit([dx, dy](auto& c) {
            if constexpr (requires { c.rect; }) {
                c.rect.x += dx;
                c.rect.y += dy;
            } else if constexpr (requires { c.origin; }) {
                c.origin.x += dx;
                c.origin.y += dy;
            }
        }, cmd);
    }
}

} // namespace detail

inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (!node.draws.empty()) {
        detail::translate_draw_list(node.draws, node.allocated.x, node.allocated.y);
        auto idx = static_cast<uint16_t>(snap.geometry.size());
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(node.draws));
        snap.z_order.push_back(idx);
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap);
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test layout -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/layout.hpp tests/test_layout.cpp
git commit -m "feat: add layout_flatten() to produce per-widget SceneSnapshot"
```

---

### Task 5: hit_test() free function

**Files:**
- Create: `include/prism/core/hit_test.hpp`
- Create: `tests/test_hit_test.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests**

Create `tests/test_hit_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/hit_test.hpp>

TEST_CASE("hit_test returns nullopt on empty snapshot") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    auto result = prism::hit_test(snap, {50, 50});
    CHECK(!result.has_value());
}

TEST_CASE("hit_test finds widget containing point") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, {0, 0, 100, 100}});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    auto result = prism::hit_test(snap, {50, 50});
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

TEST_CASE("hit_test returns nullopt when point outside all widgets") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, {0, 0, 100, 100}});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    auto result = prism::hit_test(snap, {200, 200});
    CHECK(!result.has_value());
}

TEST_CASE("hit_test returns topmost widget in z-order") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    // Two overlapping widgets
    snap.geometry.push_back({1, {0, 0, 200, 200}});     // index 0, behind
    snap.geometry.push_back({2, {50, 50, 100, 100}});    // index 1, in front
    snap.draw_lists.push_back({});
    snap.draw_lists.push_back({});
    snap.z_order = {0, 1};  // back-to-front: 0 then 1

    // Point in overlapping area — should hit widget 2 (front)
    auto result = prism::hit_test(snap, {75, 75});
    REQUIRE(result.has_value());
    CHECK(*result == 2);

    // Point only in widget 1
    auto result2 = prism::hit_test(snap, {10, 10});
    REQUIRE(result2.has_value());
    CHECK(*result2 == 1);
}

TEST_CASE("hit_test edge: point on boundary is inside") {
    prism::SceneSnapshot snap;
    snap.version = 1;
    snap.geometry.push_back({1, {10, 10, 100, 100}});
    snap.draw_lists.push_back({});
    snap.z_order.push_back(0);

    // Top-left corner (exactly on boundary)
    auto result = prism::hit_test(snap, {10, 10});
    REQUIRE(result.has_value());
    CHECK(*result == 1);

    // Bottom-right corner (exclusive: x=110, y=110 is outside)
    auto outside = prism::hit_test(snap, {110, 110});
    CHECK(!outside.has_value());
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```meson
  'hit_test' : files('test_hit_test.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test hit_test -C builddir --print-errorlogs`
Expected: FAIL — `hit_test.hpp` does not exist

- [ ] **Step 4: Write minimal implementation**

Create `include/prism/core/hit_test.hpp`:

```cpp
#pragma once

#include <prism/core/scene_snapshot.hpp>

#include <optional>

namespace prism {

[[nodiscard]] inline std::optional<WidgetId> hit_test(
    const SceneSnapshot& snap, Point pos)
{
    // Walk z_order back-to-front, return first hit
    for (auto it = snap.z_order.rbegin(); it != snap.z_order.rend(); ++it) {
        auto& [id, rect] = snap.geometry[*it];
        if (rect.contains(pos))
            return id;
    }
    return std::nullopt;
}

} // namespace prism
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test hit_test -C builddir --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/hit_test.hpp tests/test_hit_test.cpp tests/meson.build
git commit -m "feat: add hit_test() z-order point-in-rect scan"
```

---

### Task 6: Wire layout into Ui<State>

**Files:**
- Modify: `include/prism/core/ui.hpp`
- Modify: `tests/test_ui.cpp`

- [ ] **Step 1: Write failing integration tests**

Append to `tests/test_ui.cpp`:

```cpp
TEST_CASE("ui.row() produces per-widget geometry in snapshot") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    prism::app<S>(
        prism::Backend{std::make_unique<CapturingBackend>(captured_snap)},
        prism::BackendConfig{.width = 800, .height = 600},
        S{},
        [](prism::Ui<S>& ui) {
            ui.row([&] {
                ui.frame().filled_rect({0, 0, 200, 100},
                    prism::Color::rgba(255, 0, 0));
                ui.spacer();
                ui.frame().filled_rect({0, 0, 100, 50},
                    prism::Color::rgba(0, 255, 0));
            });
        }
    );

    REQUIRE(captured_snap != nullptr);
    // Two leaf widgets (spacer has no draw commands, skipped)
    CHECK(captured_snap->geometry.size() == 2);
    // First widget at x=0, width=200
    CHECK(captured_snap->geometry[0].second.x == 0);
    CHECK(captured_snap->geometry[0].second.w == 200);
    // Second widget at x=700 (800-100), width=100
    CHECK(captured_snap->geometry[1].second.x == doctest::Approx(700));
    CHECK(captured_snap->geometry[1].second.w == 100);
}

TEST_CASE("ui.column() stacks children vertically") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    prism::app<S>(
        prism::Backend{std::make_unique<CapturingBackend>(captured_snap)},
        prism::BackendConfig{.width = 400, .height = 300},
        S{},
        [](prism::Ui<S>& ui) {
            ui.column([&] {
                ui.frame().filled_rect({0, 0, 100, 80},
                    prism::Color::rgba(255, 0, 0));
                ui.frame().filled_rect({0, 0, 100, 60},
                    prism::Color::rgba(0, 255, 0));
            });
        }
    );

    REQUIRE(captured_snap != nullptr);
    CHECK(captured_snap->geometry.size() == 2);
    CHECK(captured_snap->geometry[0].second.y == 0);
    CHECK(captured_snap->geometry[0].second.h == 80);
    CHECK(captured_snap->geometry[1].second.y == 80);
    CHECK(captured_snap->geometry[1].second.h == 60);
}

TEST_CASE("ui.frame() without layout works as before (backward compat)") {
    struct S {};
    std::shared_ptr<const prism::SceneSnapshot> captured_snap;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    prism::app<S>(
        prism::Backend{std::make_unique<CapturingBackend>(captured_snap)},
        prism::BackendConfig{.width = 800, .height = 600},
        S{},
        [](prism::Ui<S>& ui) {
            ui.frame().filled_rect({10, 10, 50, 50},
                prism::Color::rgba(255, 0, 0));
        }
    );

    REQUIRE(captured_snap != nullptr);
    // Single full-viewport entry, same as before
    CHECK(captured_snap->geometry.size() == 1);
    CHECK(captured_snap->geometry[0].second.w == 800);
    CHECK(captured_snap->geometry[0].second.h == 600);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test ui -C builddir --print-errorlogs`
Expected: FAIL — `row`, `column`, `spacer` are not members of `Ui`

- [ ] **Step 3: Rewrite Ui<State> with layout tree building**

Replace the contents of `include/prism/core/ui.hpp` with:

```cpp
#pragma once

#include <prism/core/app.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

    bool has_layout_root() const { return root_.has_value(); }
    LayoutNode& layout_root() { return *root_; }

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
        // Flush any pending draw commands into a leaf before closing
        flush_leaf();
        node_stack_.pop_back();
    }

    void flush_leaf() {
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

    // Grant friend access to app() overloads
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

namespace detail {

template <typename State>
std::shared_ptr<const SceneSnapshot> take_ui_snapshot(
    Ui<State>& ui, Frame& frame, int w, int h, uint64_t version)
{
    if (ui.has_layout_root()) {
        // Flush any pending leaf draws
        ui.frame(); // ensure node_frame_ is accessible
        DrawList& dl = ui.node_frame_.dl_;
        if (!dl.empty()) {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = ui.next_id_++;
            leaf.draws = std::move(dl);
            dl.clear();
            if (!ui.node_stack_.empty()) {
                ui.current_children().push_back(std::move(leaf));
            }
        }

        auto& root = ui.layout_root();
        LayoutAxis axis = (root.kind == LayoutNode::Kind::Row)
            ? LayoutAxis::Horizontal : LayoutAxis::Vertical;
        layout_measure(root, axis);
        layout_arrange(root, {0, 0, static_cast<float>(w), static_cast<float>(h)});

        auto snap = std::make_shared<SceneSnapshot>();
        snap->version = version;
        layout_flatten(root, *snap);
        return snap;
    }

    // No layout — fall back to existing single-entry snapshot
    return AppAccess::take_snapshot(frame, version);
}

} // namespace detail

template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
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
    backend.submit(detail::take_ui_snapshot(ui, frame, w, h, ++version));
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
            if (update) { update(state, *ev); }
        }

        AppAccess::reset(frame, w, h);
        Ui<State> ui2(state, frame);
        view(ui2);

        if (!running.load(std::memory_order_relaxed)) break;

        backend.submit(detail::take_ui_snapshot(ui2, frame, w, h, ++version));
        backend.wake();
    }

    backend.quit();
    backend_thread.join();
}

template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(std::move(backend), cfg, State{}, std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    app<State>(Backend::software(cfg), cfg, std::move(initial), std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(title, State{}, std::move(view), std::move(update));
}

} // namespace prism
```

**Key changes:**
- `Ui<State>` now has `row()`, `column()`, `spacer()` that build a `LayoutNode` tree
- `frame()` returns `node_frame_` when inside a layout context (local coords), or the original `frame_` when no layout is active
- Each boundary change (`begin_container`, `end_container`, `spacer`) flushes pending draw commands into a leaf node via `flush_leaf()`
- `detail::take_ui_snapshot()` either runs the layout solver or falls back to the old single-entry path

- [ ] **Step 4: Run all tests to verify everything passes**

Run: `meson test -C builddir --print-errorlogs`
Expected: All tests PASS (existing + new)

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/ui.hpp tests/test_ui.cpp
git commit -m "feat: wire layout tree into Ui<State> with row/column/spacer"
```

---

### Task 7: Update umbrella header and update example

**Files:**
- Modify: `include/prism/prism.hpp`
- Modify: `examples/hello_rect.cpp`

- [ ] **Step 1: Add new headers to prism.hpp**

Add to `include/prism/prism.hpp`, after the existing includes:

```cpp
#include <prism/core/hit_test.hpp>
#include <prism/core/layout.hpp>
```

- [ ] **Step 2: Rewrite hello_rect.cpp to use layout**

Replace `examples/hello_rect.cpp` with:

```cpp
#include <prism/prism.hpp>

#include <SDL3/SDL_keycode.h>

#include <array>

struct State {
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
            ui.row([&] {
                // Left sidebar
                ui.frame().filled_rect({0, 0, 200, 100},
                    prism::Color::rgba(50, 50, 60));

                ui.spacer();

                // Right panel with colored rect
                ui.column([&] {
                    ui.frame().filled_rect({0, 0, 300, 150},
                        colors[ui->color_index]);
                    ui.spacer();
                    ui.frame().filled_rect({0, 0, 300, 80},
                        prism::Color::rgba(40, 40, 50));
                });
            });
        },
        [](State& s, const prism::InputEvent& ev) {
            if (auto* click = std::get_if<prism::MouseButton>(&ev);
                click && click->pressed) {
                s.color_index = (s.color_index + 1) % colors.size();
            }
        }
    );
}
```

- [ ] **Step 3: Build and verify the example compiles**

Run: `meson compile -C builddir hello_rect`
Expected: Compiles without errors

- [ ] **Step 4: Run all tests**

Run: `meson test -C builddir --print-errorlogs`
Expected: All PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/prism.hpp examples/hello_rect.cpp
git commit -m "feat: update umbrella header and rewrite example with layout"
```

---

### Task 8: Update design docs and roadmap

**Files:**
- Modify: `doc/design/README.md`

- [ ] **Step 1: Update design doc status table**

In `doc/design/README.md`, update the layout-engine row:

Change:
```
| [layout-engine.md](layout-engine.md) | `row()`, `column()`, `spacer()`, constraint regions, parallel solving | Planned (Phase 2) |
```
To:
```
| [layout-engine.md](layout-engine.md) | `row()`, `column()`, `spacer()`, stack-based layout, hit testing | **Implemented (Phase 2)** |
```

Also update the widget-model row to note that layout integration is done:

Change:
```
| [input-events.md](input-events.md) | Input queue, event forwarding, hit testing | **Implemented** (queue + forwarding + keyboard), hit regions planned Phase 2 |
```
To:
```
| [input-events.md](input-events.md) | Input queue, event forwarding, hit testing | **Implemented** (queue + forwarding + keyboard + hit_test) |
```

- [ ] **Step 2: Commit**

```bash
git add doc/design/README.md
git commit -m "docs: update design doc status for layout + hit regions"
```
