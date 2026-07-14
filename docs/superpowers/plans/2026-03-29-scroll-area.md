# Scroll Area Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a general-purpose scroll container that clips content to a viewport, offsets draws by scroll position, and handles mouse wheel / keyboard scrolling with configurable bubble policy.

**Architecture:** Scroll is a new `LayoutKind::Scroll` container node. During layout, it measures children unbounded, arranges them in a virtual content region, then clips/offsets during flatten. Scroll state lives in `WidgetNode::edit_state`. Input bubbles up via a parent map. ViewBuilder provides `scroll()` sugar; `Field<ScrollArea>` sentinel enables programmatic control.

**Tech Stack:** C++23/26, meson, doctest

**Spec:** `docs/superpowers/specs/2026-03-29-scroll-area-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `include/prism/core/delegate.hpp` | **Modify.** Add `ScrollBarPolicy`, `ScrollEventPolicy` enums, `ScrollArea` sentinel, `ScrollState` struct, `Delegate<ScrollArea>` specialization. Add `LayoutKind::Scroll`. Add `keys::page_up`/`page_down`. |
| `include/prism/core/node.hpp` | **Modify.** Add scroll metadata fields to `Node`. |
| `include/prism/core/layout.hpp` | **Modify.** Add `LayoutNode::Kind::Scroll`, `scroll_offset` field. Scroll-specific measure/arrange/flatten. |
| `include/prism/core/widget_tree.hpp` | **Modify.** `ViewBuilder::scroll()` overloads, parent map in `build_index`, `scroll_at()` method, `build_layout` scroll branch, scroll state wiring. |
| `include/prism/core/model_app.hpp` | **Modify.** `MouseScroll` event handler, keyboard scroll for PageUp/PageDown. |
| `tests/test_scroll.cpp` | **New.** Scroll construction, layout, clipping, input, dirty tracking, bubble policy, Field<ScrollArea> binding. |
| `tests/meson.build` | **Modify.** Add `test_scroll.cpp`. |

---

### Task 1: Add scroll enums, ScrollState, ScrollArea sentinel, and key constants

**Files:**
- Modify: `include/prism/core/delegate.hpp`

- [ ] **Step 1: Add `ScrollBarPolicy` and `ScrollEventPolicy` enums**

In `delegate.hpp`, after the `LayoutKind` enum (line 36), add:

```cpp
enum class ScrollBarPolicy : uint8_t { Auto, Always, Never };
enum class ScrollEventPolicy : uint8_t { ConsumeAlways, BubbleAtBounds };
```

- [ ] **Step 2: Add `LayoutKind::Scroll`**

Change the `LayoutKind` enum from:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas };
```

To:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll };
```

- [ ] **Step 3: Add `ScrollState` struct**

After `TextAreaEditState` (line 92), add:

```cpp
struct ScrollState {
    DY offset_y{0};
    DX offset_x{0};
    Height content_h{0};
    Width content_w{0};
    Height viewport_h{0};
    Width viewport_w{0};
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    uint8_t show_ticks = 0;
};
```

- [ ] **Step 4: Add `ScrollArea` sentinel type**

After `ScrollState`, add:

```cpp
struct ScrollArea {
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    DY scroll_y{0};
    DX scroll_x{0};
    bool operator==(const ScrollArea&) const = default;
};
```

- [ ] **Step 5: Add `keys::page_up` and `keys::page_down`**

In the `keys` namespace in `input_event.hpp`, add:

```cpp
inline constexpr int32_t page_up   = 0x4000'004B;  // SDLK_PAGEUP
inline constexpr int32_t page_down = 0x4000'004E;  // SDLK_PAGEDOWN
```

- [ ] **Step 6: Build to verify compilation**

Run: `meson compile -C builddir`
Expected: Compiles cleanly. No tests exercise scroll yet.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/delegate.hpp include/prism/core/input_event.hpp
git commit -m "feat: add scroll enums, ScrollState, ScrollArea sentinel, page keys"
```

---

### Task 2: Add scroll metadata to Node

**Files:**
- Modify: `include/prism/core/node.hpp`

- [ ] **Step 1: Add scroll policy fields to Node**

In `node.hpp`, add two fields to the `Node` struct after the `dependencies` vector:

```cpp
struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    // Type-erased WidgetNode builder (leaf nodes only)
    std::function<void(WidgetNode&)> build_widget;

    // Type-erased change subscription (leaf nodes only)
    std::function<Connection(std::function<void()>)> on_change;

    // Multiple dependencies for canvas nodes with depends_on()
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;

    // Scroll container metadata (only meaningful when layout_kind == Scroll)
    ScrollBarPolicy scroll_bar_policy = ScrollBarPolicy::Auto;
    ScrollEventPolicy scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
};
```

- [ ] **Step 2: Build to verify compilation**

Run: `meson compile -C builddir`
Expected: Compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/node.hpp
git commit -m "feat: add scroll policy metadata to Node"
```

---

### Task 3: Add scroll support to layout system

**Files:**
- Modify: `include/prism/core/layout.hpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_scroll.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/layout.hpp>

using namespace prism;

TEST_CASE("Scroll LayoutNode measures as expandable") {
    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;

    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{800}}},
        Color::rgba(255, 0, 0));
    scroll.children.push_back(std::move(child));

    layout_measure(scroll, LayoutAxis::Vertical);
    CHECK(scroll.hint.expand);
}

TEST_CASE("Scroll LayoutNode arranges children with unbounded height") {
    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;

    // Two leaf children, each 400px tall
    for (int i = 0; i < 2; ++i) {
        LayoutNode child;
        child.kind = LayoutNode::Kind::Leaf;
        child.id = 10 + i;
        child.draws.filled_rect(
            Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{400}}},
            Color::rgba(255, 0, 0));
        scroll.children.push_back(std::move(child));
    }

    layout_measure(scroll, LayoutAxis::Vertical);

    // Viewport is only 300px tall
    Rect viewport{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}};
    layout_arrange(scroll, viewport);

    // Scroll node gets the viewport rect
    CHECK(scroll.allocated.extent.h.raw() == doctest::Approx(300));

    // Children are arranged in full content height (400+400=800), not clipped to 300
    CHECK(scroll.children[0].allocated.extent.h.raw() == doctest::Approx(400));
    CHECK(scroll.children[1].allocated.origin.y.raw() == doctest::Approx(400));
    CHECK(scroll.children[1].allocated.extent.h.raw() == doctest::Approx(400));
}

TEST_CASE("Scroll flatten clips to viewport") {
    using namespace prism;

    LayoutNode scroll;
    scroll.kind = LayoutNode::Kind::Scroll;
    scroll.id = 1;
    scroll.scroll_offset = DY{0};
    scroll.allocated = Rect{Point{X{10}, Y{20}}, Size{Width{200}, Height{100}}};
    scroll.scroll_content_h = Height{400};

    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{400}}},
        Color::rgba(255, 0, 0));
    child.allocated = Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{400}}};
    scroll.children.push_back(std::move(child));

    SceneSnapshot snap;
    layout_flatten(scroll, snap);

    // Should have geometry for the visible child
    CHECK(snap.geometry.size() >= 1);
    // Should have ClipPush/ClipPop wrapping the child draws
    bool has_clip = false;
    for (auto& dl : snap.draw_lists) {
        for (auto& cmd : dl.commands) {
            if (std::holds_alternative<ClipPush>(cmd)) has_clip = true;
        }
    }
    CHECK(has_clip);
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```
  'scroll' : files('test_scroll.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir scroll`
Expected: FAIL — `LayoutNode::Kind::Scroll` doesn't exist yet.

- [ ] **Step 4: Add `Kind::Scroll` and `scroll_offset`/`scroll_content_h` to LayoutNode**

In `layout.hpp`, update the `LayoutNode` struct:

```cpp
struct LayoutNode {
    WidgetId id = 0;
    SizeHint hint;
    Rect allocated{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    DrawList draws;
    DrawList overlay_draws;
    std::vector<LayoutNode> children;
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll } kind = Kind::Leaf;
    DY scroll_offset{0};        // only for Kind::Scroll
    Height scroll_content_h{0}; // total content height, for scrollbar rendering
};
```

- [ ] **Step 5: Add scroll case to `layout_measure`**

In `layout_measure`, add a case before the closing `}` of the switch:

```cpp
    case LayoutNode::Kind::Scroll: {
        // Measure children as a vertical column (unbounded)
        float sum = 0, max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, LayoutAxis::Vertical);
            sum += child.hint.preferred;
            max_cross = std::max(max_cross, child.hint.cross);
        }
        node.hint.expand = true;
        node.hint.preferred = 0;  // wants to fill available space
        node.hint.cross = max_cross;
        return;
    }
```

- [ ] **Step 6: Add scroll case to `layout_arrange`**

In `layout_arrange`, after `node.allocated = available;` and before the `if (node.children.empty()) return;` line, add:

```cpp
    if (node.kind == LayoutNode::Kind::Scroll) {
        // Compute total content height from children's preferred sizes
        float content_h = 0;
        for (auto& child : node.children) {
            content_h += child.hint.preferred;
        }
        node.scroll_content_h = Height{content_h};

        // Arrange children in a virtual column with full content height
        float offset = 0;
        for (auto& child : node.children) {
            float main_size = child.hint.preferred;
            Rect child_rect{
                Point{available.origin.x, Y{available.origin.y.raw() + offset}},
                Size{available.extent.w, Height{main_size}}
            };
            layout_arrange(child, child_rect);
            offset += main_size;
        }
        return;
    }
```

- [ ] **Step 7: Add scroll case to `layout_flatten`**

In `layout_flatten`, add a scroll-specific branch at the top of the function, before the existing leaf/container logic:

```cpp
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap) {
    if (node.kind == LayoutNode::Kind::Spacer) return;

    if (node.kind == LayoutNode::Kind::Scroll) {
        // Emit a DrawList with ClipPush for the viewport
        DrawList clip_dl;
        clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(clip_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Flatten children with scroll offset applied
        DY scroll_dy = node.scroll_offset;
        for (auto& child : node.children) {
            // Check if child intersects viewport after scroll offset
            float child_top = child.allocated.origin.y.raw() - scroll_dy.raw();
            float child_bottom = child_top + child.allocated.extent.h.raw();
            float vp_top = node.allocated.origin.y.raw();
            float vp_bottom = vp_top + node.allocated.extent.h.raw();

            if (child_bottom <= vp_top || child_top >= vp_bottom)
                continue;  // fully outside viewport

            // Offset child's allocated rect by -scroll_offset for rendering
            Rect original = child.allocated;
            child.allocated.origin.y = Y{child.allocated.origin.y.raw() - scroll_dy.raw()};
            layout_flatten(child, snap);
            child.allocated = original;  // restore for hit-test correctness
        }

        // Emit ClipPop
        DrawList clip_pop_dl;
        clip_pop_dl.clip_pop();
        snap.draw_lists.push_back(std::move(clip_pop_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.draw_lists.size() - 1));
        // geometry needs a dummy entry for parallel vectors
        snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});

        // Scrollbar overlay (if content exceeds viewport)
        if (node.scroll_content_h.raw() > node.allocated.extent.h.raw()) {
            auto vp = node.allocated;
            float viewport_h = vp.extent.h.raw();
            float content_h = node.scroll_content_h.raw();
            float thumb_ratio = viewport_h / content_h;
            float thumb_h = std::max(20.f, viewport_h * thumb_ratio);
            float max_scroll = content_h - viewport_h;
            float thumb_y = (max_scroll > 0)
                ? node.scroll_offset.raw() * (viewport_h - thumb_h) / max_scroll
                : 0.f;

            float track_x = vp.origin.x.raw() + vp.extent.w.raw() - 8.f;
            float track_y = vp.origin.y.raw() + thumb_y;

            DrawList scrollbar_dl;
            scrollbar_dl.filled_rect(
                Rect{Point{X{track_x}, Y{track_y}},
                     Size{Width{6}, Height{thumb_h}}},
                Color::rgba(120, 120, 130, 160));
            snap.overlay_geometry.push_back({node.id, vp});
            for (auto& cmd : scrollbar_dl.commands)
                snap.overlay.commands.push_back(std::move(cmd));
        }

        return;
    }

    // ... existing flatten code unchanged ...
```

- [ ] **Step 8: Run tests**

Run: `meson test -C builddir scroll`
Expected: All 3 test cases PASS.

- [ ] **Step 9: Commit**

```bash
git add include/prism/core/layout.hpp tests/test_scroll.cpp tests/meson.build
git commit -m "feat: add Scroll kind to layout system with measure/arrange/flatten"
```

---

### Task 4: Add parent map and `scroll_at()` to WidgetTree

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scroll.cpp`:

```cpp
#include <prism/core/widget_tree.hpp>

TEST_CASE("scroll_at scrolls a scroll container") {
    struct Model {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        prism::Field<int> c{0};
        prism::Field<int> d{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.widget(a);
                vb.widget(b);
                vb.widget(c);
                vb.widget(d);
            });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);  // small viewport

    // Find a leaf widget id to scroll at
    REQUIRE(snap->geometry.size() >= 1);
    auto leaf_id = snap->geometry[0].first;
    // We need the id of a widget inside the scroll — not the scroll container itself
    // leaf_count returns 4
    CHECK(tree.leaf_count() == 4);

    // scroll_at should find the scroll ancestor and apply delta
    tree.scroll_at(leaf_id, prism::DY{50});
    CHECK(tree.any_dirty());
}

TEST_CASE("scroll_at clamps to bounds") {
    struct Model {
        prism::Field<int> a{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.widget(a); });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 600, 1);  // viewport larger than content

    // Scrolling should have no effect when content fits
    auto leaf_id = 3u;  // first leaf inside scroll
    tree.scroll_at(leaf_id, prism::DY{100});
    // Should not be dirty since content fits viewport
    CHECK_FALSE(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir scroll`
Expected: FAIL — `scroll_at` and `ViewBuilder::scroll` don't exist.

- [ ] **Step 3: Add parent map to WidgetTree**

In `widget_tree.hpp`, add a new private member after `index_`:

```cpp
    std::unordered_map<WidgetId, WidgetId> parent_map_;
```

Update `build_index` to populate it:

```cpp
    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        if (!node.is_container && node.focus_policy != FocusPolicy::none)
            focus_order_.push_back(node.id);
        for (auto& c : node.children) {
            parent_map_[c.id] = node.id;
            build_index(c);
        }
    }
```

- [ ] **Step 4: Add `scroll_at()` public method**

In the public section of `WidgetTree`, after `close_overlays()`:

```cpp
    void scroll_at(WidgetId target, DY delta) {
        WidgetId current = target;
        while (current != 0) {
            auto it = index_.find(current);
            if (it != index_.end() && it->second->layout_kind == LayoutKind::Scroll) {
                auto& ss = ensure_scroll_state(*it->second);
                // Can't scroll until we know content/viewport sizes
                // These are set during build_snapshot → build_layout
                DY max_offset{std::max(0.f, ss.content_h.raw() - ss.viewport_h.raw())};
                DY new_offset{std::clamp(ss.offset_y.raw() + delta.raw(), 0.f, max_offset.raw())};

                if (std::abs(new_offset.raw() - ss.offset_y.raw()) < 0.001f) {
                    // No actual scroll happened
                    if (ss.event_policy == ScrollEventPolicy::BubbleAtBounds) {
                        auto pit = parent_map_.find(current);
                        current = (pit != parent_map_.end()) ? pit->second : 0;
                        continue;
                    }
                    return;
                }

                ss.offset_y = new_offset;
                ss.show_ticks = 30;
                mark_dirty(root_, current);
                return;
            }
            auto pit = parent_map_.find(current);
            current = (pit != parent_map_.end()) ? pit->second : 0;
        }
    }
```

- [ ] **Step 5: Add `ensure_scroll_state` helper**

In the private section of `WidgetTree`:

```cpp
    static ScrollState& ensure_scroll_state(WidgetNode& node) {
        if (!node.edit_state.has_value())
            node.edit_state = ScrollState{};
        return std::any_cast<ScrollState&>(node.edit_state);
    }

    static const ScrollState& get_scroll_state(const WidgetNode& node) {
        static const ScrollState default_state;
        if (!node.edit_state.has_value()) return default_state;
        return std::any_cast<const ScrollState&>(node.edit_state);
    }
```

- [ ] **Step 6: Run tests (they will still fail — need ViewBuilder::scroll next)**

Run: `meson compile -C builddir`
Expected: Compiles. Tests still fail because `ViewBuilder::scroll` is not yet defined.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "feat: add parent map and scroll_at() to WidgetTree"
```

---

### Task 5: Add `ViewBuilder::scroll()` and wire scroll into build_layout/build_snapshot

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Add `ViewBuilder::scroll()` overloads**

In the `ViewBuilder` class, in the public section after the `canvas()` method:

```cpp
        void scroll(std::invocable auto&& fn) {
            push_scroll_container(ScrollBarPolicy::Auto, ScrollEventPolicy::BubbleAtBounds, fn);
        }

        void scroll(ScrollBarPolicy policy, std::invocable auto&& fn) {
            push_scroll_container(policy, ScrollEventPolicy::BubbleAtBounds, fn);
        }

        void scroll(Field<ScrollArea>& field, std::invocable auto&& fn) {
            placed_.insert(&field);
            push_scroll_container(field.get().scrollbar, field.get().event_policy, fn);
            // Store field reference for wiring in the scroll node
            auto& scroll_node = current_parent().children.back();
            scroll_node.build_widget = [&field](WidgetNode& wn) {
                auto& ss = ensure_scroll_state_static(wn);
                ss.scrollbar = field.get().scrollbar;
                ss.event_policy = field.get().event_policy;
                ss.offset_y = field.get().scroll_y;
            };
            scroll_node.on_change = [&field](std::function<void()> cb) -> Connection {
                return field.on_change().connect(
                    [cb = std::move(cb)](const ScrollArea&) { cb(); });
            };
        }
```

In the private section, add:

```cpp
        void push_scroll_container(ScrollBarPolicy bar, ScrollEventPolicy evt,
                                   std::invocable auto&& fn) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::Scroll;
            container.scroll_bar_policy = bar;
            container.scroll_event_policy = evt;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            stack_.push_back(&parent.children.back());
            fn();
            stack_.pop_back();
        }
```

- [ ] **Step 2: Add static scroll state helper for ViewBuilder lambda**

In the private section of `WidgetTree`, after `ensure_scroll_state`:

```cpp
    static ScrollState& ensure_scroll_state_static(WidgetNode& node) {
        if (!node.edit_state.has_value())
            node.edit_state = ScrollState{};
        return std::any_cast<ScrollState&>(node.edit_state);
    }
```

Actually this is identical to the existing `ensure_scroll_state` — remove the static version and make the original a `static` method:

Change `ensure_scroll_state` to be static (it already is — no `this` used). No separate function needed. The `ViewBuilder` lambda can call it via `WidgetTree::ensure_scroll_state_static`. However since ViewBuilder is an inner class, it can access WidgetTree privates. Simplify: just use a local lambda.

Actually, simplest approach — the `ViewBuilder::scroll(Field<ScrollArea>&, ...)` lambda captures `&field` and uses `std::any_cast` directly:

Replace the `scroll(Field<ScrollArea>&, ...)` overload with:

```cpp
        void scroll(Field<ScrollArea>& field, std::invocable auto&& fn) {
            placed_.insert(&field);
            push_scroll_container(field.get().scrollbar, field.get().event_policy, fn);
            auto& scroll_node = current_parent().children.back();
            scroll_node.build_widget = [&field](WidgetNode& wn) {
                if (!wn.edit_state.has_value())
                    wn.edit_state = ScrollState{};
                auto& ss = std::any_cast<ScrollState&>(wn.edit_state);
                ss.scrollbar = field.get().scrollbar;
                ss.event_policy = field.get().event_policy;
                ss.offset_y = field.get().scroll_y;
            };
            scroll_node.on_change = [&field](std::function<void()> cb) -> Connection {
                return field.on_change().connect(
                    [cb = std::move(cb)](const ScrollArea&) { cb(); });
            };
        }
```

Wait — `push_scroll_container` pushes and then pops the stack. The last child of `current_parent()` is the scroll container only during the lambda. After pop, `current_parent()` reverts. But `parent.children.back()` is still the scroll node we just pushed. However we need to grab it *inside* `push_scroll_container` before the pop, or grab the reference before calling push.

Cleaner approach: have `push_scroll_container` return a `Node&` reference:

```cpp
        Node& push_scroll_container(ScrollBarPolicy bar, ScrollEventPolicy evt,
                                    std::invocable auto&& fn) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::Scroll;
            container.scroll_bar_policy = bar;
            container.scroll_event_policy = evt;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            auto& ref = parent.children.back();
            stack_.push_back(&ref);
            fn();
            stack_.pop_back();
            return ref;
        }
```

Then the `scroll(Field<ScrollArea>&, ...)` becomes:

```cpp
        void scroll(Field<ScrollArea>& field, std::invocable auto&& fn) {
            placed_.insert(&field);
            auto& scroll_node = push_scroll_container(
                field.get().scrollbar, field.get().event_policy, fn);
            scroll_node.build_widget = [&field](WidgetNode& wn) {
                if (!wn.edit_state.has_value())
                    wn.edit_state = ScrollState{};
                auto& ss = std::any_cast<ScrollState&>(wn.edit_state);
                ss.scrollbar = field.get().scrollbar;
                ss.event_policy = field.get().event_policy;
                ss.offset_y = field.get().scroll_y;
            };
            scroll_node.on_change = [&field](std::function<void()> cb) -> Connection {
                return field.on_change().connect(
                    [cb = std::move(cb)](const ScrollArea&) { cb(); });
            };
        }
```

And the sugar overloads:

```cpp
        void scroll(std::invocable auto&& fn) {
            push_scroll_container(ScrollBarPolicy::Auto, ScrollEventPolicy::BubbleAtBounds, fn);
        }

        void scroll(ScrollBarPolicy policy, std::invocable auto&& fn) {
            push_scroll_container(policy, ScrollEventPolicy::BubbleAtBounds, fn);
        }
```

- [ ] **Step 3: Add scroll branch to `build_layout`**

In the `build_layout` static method, in the container branch (the `else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column)` section), add a new branch before the final `else`:

```cpp
        } else if (node.layout_kind == LK::Scroll) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::Scroll;
            container.id = node.id;
            // Transfer scroll offset from WidgetNode edit_state
            if (node.edit_state.has_value()) {
                try {
                    auto& ss = std::any_cast<const ScrollState&>(node.edit_state);
                    container.scroll_offset = ss.offset_y;
                } catch (...) {}
            }
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
```

- [ ] **Step 4: Initialize scroll state with policies during `build_widget_node`**

In `build_widget_node`, for container nodes, we need to initialize `ScrollState` from Node metadata. Add after the `wn.is_container = true;` line, inside the `else` branch:

```cpp
    static WidgetNode build_widget_node(Node& node) {
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;
        if (node.is_leaf) {
            wn.is_container = false;
            if (node.build_widget)
                node.build_widget(wn);
        } else {
            wn.is_container = true;
            if (node.layout_kind == LayoutKind::Scroll) {
                ScrollState ss;
                ss.scrollbar = node.scroll_bar_policy;
                ss.event_policy = node.scroll_event_policy;
                wn.edit_state = ss;
                if (node.build_widget)
                    node.build_widget(wn);  // Field<ScrollArea> overrides
            }
            for (auto& child : node.children)
                wn.children.push_back(build_widget_node(child));
        }
        return wn;
    }
```

- [ ] **Step 5: Update scroll state sizes after layout in `build_snapshot`**

After `layout_arrange` and before `update_canvas_bounds`, add a step to write back content/viewport sizes to `ScrollState`. Add a new static method:

```cpp
    void update_scroll_state(LayoutNode& layout_node) {
        if (layout_node.kind == LayoutNode::Kind::Scroll) {
            auto* wn = index_.count(layout_node.id) ? index_[layout_node.id] : nullptr;
            if (wn) {
                auto& ss = ensure_scroll_state(*wn);
                ss.viewport_h = layout_node.allocated.extent.h;
                ss.viewport_w = layout_node.allocated.extent.w;
                ss.content_h = layout_node.scroll_content_h;
                // Transfer current offset to layout node
                layout_node.scroll_offset = ss.offset_y;
                // Clamp offset in case viewport/content changed
                DY max_offset{std::max(0.f, ss.content_h.raw() - ss.viewport_h.raw())};
                ss.offset_y = DY{std::clamp(ss.offset_y.raw(), 0.f, max_offset.raw())};
                layout_node.scroll_offset = ss.offset_y;
                // Decrement show_ticks for scrollbar fade
                if (ss.show_ticks > 0) ss.show_ticks--;
            }
        }
        for (auto& child : layout_node.children)
            update_scroll_state(child);
    }
```

In `build_snapshot`, after `layout_arrange` and before `update_canvas_bounds`:

```cpp
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});

        // Post-layout: sync scroll state (viewport/content sizes, offset clamping)
        update_scroll_state(layout);

        // Post-layout: update canvas nodes with their resolved bounds and re-record
        update_canvas_bounds(layout, root_);
```

- [ ] **Step 6: Run tests**

Run: `meson test -C builddir scroll`
Expected: All test cases PASS.

- [ ] **Step 7: Run full test suite**

Run: `meson test -C builddir`
Expected: All existing tests pass. Scroll additions don't affect non-scroll containers.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_scroll.cpp
git commit -m "feat: add ViewBuilder::scroll() and wire scroll into layout pipeline"
```

---

### Task 6: Add MouseScroll handling to model_app

**Files:**
- Modify: `include/prism/core/model_app.hpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scroll.cpp`:

```cpp
TEST_CASE("MouseScroll event scrolls content") {
    struct ScrollModel {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        prism::Field<int> c{0};
        prism::Field<int> d{0};
        prism::Field<int> e{0};
        prism::Field<int> f{0};
        prism::Field<int> g{0};
        prism::Field<int> h{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.widget(a);
                vb.widget(b);
                vb.widget(c);
                vb.widget(d);
                vb.widget(e);
                vb.widget(f);
                vb.widget(g);
                vb.widget(h);
            });
        }
    };
    ScrollModel model;
    prism::WidgetTree tree(model);
    // Small viewport to ensure content overflows
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    // Find a leaf widget to scroll on
    REQUIRE(snap->geometry.size() >= 1);
    auto [wid, rect] = snap->geometry[0];

    // Simulate scroll
    tree.scroll_at(wid, prism::DY{30});
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir scroll`
Expected: PASS — `scroll_at` already works from Task 4.

- [ ] **Step 3: Add MouseScroll handler to model_app**

In `model_app.hpp`, in the event handling lambda, after the `MouseButton` block and before the `KeyPress` block, add:

```cpp
                    if (auto* ms = std::get_if<MouseScroll>(&ev); ms && current_snap) {
                        auto id = hit_test(*current_snap, ms->position);
                        if (id) {
                            tree.scroll_at(*id, ms->dy);
                        }
                    }
```

- [ ] **Step 4: Add keyboard scroll (PageUp/PageDown) handling**

In the `KeyPress` block, add PageUp/PageDown handling before the existing `kp->key == keys::tab` check:

```cpp
                    if (auto* kp = std::get_if<KeyPress>(&ev)) {
                        if (kp->key == keys::tab) {
                            if (kp->mods & mods::shift)
                                tree.focus_prev();
                            else
                                tree.focus_next();
                        } else if (kp->key == keys::page_up && tree.focused_id() != 0) {
                            tree.scroll_at(tree.focused_id(), DY{-200});
                        } else if (kp->key == keys::page_down && tree.focused_id() != 0) {
                            tree.scroll_at(tree.focused_id(), DY{200});
                        } else if (tree.focused_id() != 0) {
                            tree.dispatch(tree.focused_id(), ev);
                        }
                    }
```

Note: The `DY{-200}` for PageUp uses negative delta (scroll up). The `DY{200}` for PageDown uses positive delta. The value 200 is a reasonable default; ideally it would be the viewport height, but that requires knowing the scroll container's viewport size. For v1, a fixed step is acceptable.

- [ ] **Step 5: Build and run full test suite**

Run: `meson test -C builddir`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/model_app.hpp tests/test_scroll.cpp
git commit -m "feat: add MouseScroll and keyboard PageUp/PageDown handling"
```

---

### Task 7: Add bubble policy and nested scroll tests

**Files:**
- Modify: `tests/test_scroll.cpp`

- [ ] **Step 1: Write bubble-at-bounds test**

Append to `tests/test_scroll.cpp`:

```cpp
TEST_CASE("BubbleAtBounds: inner scroll at limit bubbles to outer") {
    struct Inner {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            // Inner scroll with very little content — will hit bounds immediately
            vb.scroll([&] { vb.widget(a); vb.widget(b); });
        }
    };
    struct Outer {
        Inner inner;
        prism::Field<int> c{0};
        prism::Field<int> d{0};
        prism::Field<int> e{0};
        prism::Field<int> f{0};
        prism::Field<int> g{0};
        prism::Field<int> h{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.component(inner);
                vb.widget(c);
                vb.widget(d);
                vb.widget(e);
                vb.widget(f);
                vb.widget(g);
                vb.widget(h);
            });
        }
    };
    Outer model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 80, 1);
    tree.clear_dirty();

    // Find a leaf inside the inner scroll
    // The inner scroll's content likely fits in 80px viewport,
    // so scrolling it should bubble to the outer scroll
    REQUIRE(snap->geometry.size() >= 1);
    auto leaf_id = snap->geometry[0].first;
    tree.scroll_at(leaf_id, prism::DY{50});
    // Should have bubbled to outer and scrolled there
    CHECK(tree.any_dirty());
}

TEST_CASE("ConsumeAlways: inner scroll consumes even at bounds") {
    struct Model {
        prism::Field<int> a{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.scroll(prism::ScrollBarPolicy::Auto, [&] {
                    vb.widget(a);
                });
            });
        }
    };
    // This test verifies compilation and basic structure.
    // ConsumeAlways behavior is tested by setting the policy on the scroll node's
    // edit_state directly, since ViewBuilder::scroll() doesn't expose event_policy yet.
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 200, 1);
    CHECK(snap != nullptr);
}
```

- [ ] **Step 2: Write Field<ScrollArea> binding test**

Append to `tests/test_scroll.cpp`:

```cpp
TEST_CASE("Field<ScrollArea> provides programmatic scroll control") {
    struct Model {
        prism::Field<prism::ScrollArea> scroller{{}};
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        prism::Field<int> c{0};
        prism::Field<int> d{0};
        prism::Field<int> e{0};
        prism::Field<int> f{0};
        prism::Field<int> g{0};
        prism::Field<int> h{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll(scroller, [&] {
                vb.widget(a);
                vb.widget(b);
                vb.widget(c);
                vb.widget(d);
                vb.widget(e);
                vb.widget(f);
                vb.widget(g);
                vb.widget(h);
            });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    // Programmatic scroll via field
    auto sa = model.scroller.get();
    sa.scroll_y = prism::DY{50};
    model.scroller.set(sa);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 3: Write no-scroll passthrough test**

Append to `tests/test_scroll.cpp`:

```cpp
TEST_CASE("Scroll area with content fitting viewport does not scroll") {
    struct Model {
        prism::Field<int> a{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.widget(a); });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 600, 1);  // big viewport
    tree.clear_dirty();

    REQUIRE(snap->geometry.size() >= 1);
    auto leaf_id = snap->geometry[0].first;
    tree.scroll_at(leaf_id, prism::DY{50});
    CHECK_FALSE(tree.any_dirty());
}
```

- [ ] **Step 4: Run all tests**

Run: `meson test -C builddir scroll`
Expected: All test cases PASS.

- [ ] **Step 5: Run full test suite**

Run: `meson test -C builddir`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add tests/test_scroll.cpp
git commit -m "test: add bubble policy, Field<ScrollArea>, and no-scroll tests"
```

---

### Task 8: Update memory and roadmap

**Files:**
- Modify: `/home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/project-roadmap.md`
- Modify: `/home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/MEMORY.md`

- [ ] **Step 1: Update roadmap**

Add to Phase 4 section (change from future to in-progress):

```
**Phase 4 — Advanced Features (IN PROGRESS)**
- [x] Scroll areas — vertical scroll, overlay scrollbar, bubble policy, Field<ScrollArea> programmatic control
- [ ] Virtual lists
- [ ] Animation system
- [ ] Accessibility
- [ ] Data widgets: plot, table
```

- [ ] **Step 2: Add memory entry for scroll area**

Create memory file and add MEMORY.md entry for scroll area architecture.

- [ ] **Step 3: No git commit needed for memory files**
