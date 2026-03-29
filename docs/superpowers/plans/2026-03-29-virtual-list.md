# Virtual List Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add virtualized list rendering for `List<T>` collections — only visible items are materialized as WidgetNodes, with a recycling pool and implicit scrolling.

**Architecture:** New `LayoutKind::VirtualList` with dedicated measure/arrange/flatten paths. `ViewBuilder::list()` creates a container node that holds a type-erased row factory and `List<T>` reference. A `VirtualListState` (stored in `edit_state`) manages scroll offset, visible range, and a recycling pool of WidgetNodes. The existing `scroll_at()` machinery is extended to handle VirtualList nodes.

**Tech Stack:** C++23, doctest, prism core (no SDL dependency)

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Modify | `include/prism/core/types.hpp` | Add `IntScalar<Tag>`, `ItemIndex`, `ItemCount` |
| Modify | `include/prism/core/delegate.hpp` | Add `LayoutKind::VirtualList` to enum |
| Modify | `include/prism/core/node.hpp` | Add virtual list fields to `Node` |
| Modify | `include/prism/core/layout.hpp` | Add `LayoutNode::Kind::VirtualList`, measure/arrange/flatten |
| Modify | `include/prism/core/widget_tree.hpp` | Add `VirtualListState`, `ViewBuilder::list()`, pool logic, `build_layout`/`build_widget_node`/`connect_dirty`/`scroll_at`/`update_scroll_state` extensions |
| Create | `tests/test_virtual_list.cpp` | All virtual list tests |
| Modify | `tests/meson.build` | Register new test |

---

### Task 1: IntScalar Strong Types

**Files:**
- Modify: `include/prism/core/types.hpp`
- Create: `tests/test_virtual_list.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing test for IntScalar**

Create `tests/test_virtual_list.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>

using namespace prism;

TEST_CASE("IntScalar default constructs to zero") {
    ItemIndex idx;
    CHECK(idx.raw() == 0);
}

TEST_CASE("IntScalar explicit construction and raw access") {
    ItemIndex idx{42};
    CHECK(idx.raw() == 42);
}

TEST_CASE("IntScalar comparison") {
    CHECK(ItemIndex{1} < ItemIndex{2});
    CHECK(ItemIndex{3} == ItemIndex{3});
    CHECK(ItemCount{5} != ItemCount{6});
}

TEST_CASE("IntScalar arithmetic: ItemIndex + ItemCount = ItemIndex") {
    auto result = ItemIndex{3} + ItemCount{2};
    static_assert(std::is_same_v<decltype(result), ItemIndex>);
    CHECK(result.raw() == 5);
}

TEST_CASE("IntScalar arithmetic: ItemIndex - ItemIndex = ItemCount") {
    auto result = ItemIndex{7} - ItemIndex{3};
    static_assert(std::is_same_v<decltype(result), ItemCount>);
    CHECK(result.raw() == 4);
}

TEST_CASE("IntScalar arithmetic: ItemCount + ItemCount = ItemCount") {
    auto result = ItemCount{2} + ItemCount{3};
    static_assert(std::is_same_v<decltype(result), ItemCount>);
    CHECK(result.raw() == 5);
}
```

- [ ] **Step 2: Register test in meson.build**

In `tests/meson.build`, add to the `headless_tests` dictionary (after the `'scroll'` entry):

```
  'virtual_list' : files('test_virtual_list.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: compilation error — `ItemIndex`, `ItemCount` not defined

- [ ] **Step 4: Implement IntScalar in types.hpp**

In `include/prism/core/types.hpp`, after the `Scalar` operators block (after line 104, before the `// Composite types` comment at line 106), add:

```cpp
// Integer scalar (for item indices and counts)
template <typename Tag>
struct IntScalar {
    constexpr IntScalar() : v_(0) {}
    constexpr explicit IntScalar(size_t v) : v_(v) {}
    [[nodiscard]] constexpr size_t raw() const { return v_; }
    constexpr auto operator<=>(const IntScalar&) const = default;
private:
    size_t v_;
};

struct ItemIndexTag {};
struct ItemCountTag {};

using ItemIndex = IntScalar<ItemIndexTag>;
using ItemCount = IntScalar<ItemCountTag>;

// Affine arithmetic rules for IntScalar (mirrors Scalar patterns)
template <typename LTag, typename RTag> struct IntAddResult;
template <typename LTag, typename RTag> struct IntSubResult;

template <> struct IntAddResult<ItemIndexTag, ItemCountTag> { using type = ItemIndexTag; };
template <> struct IntAddResult<ItemCountTag, ItemCountTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemIndexTag, ItemIndexTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemCountTag, ItemCountTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemIndexTag, ItemCountTag> { using type = ItemIndexTag; };

template <typename L, typename R> concept IntAddable = requires { typename IntAddResult<L, R>::type; };
template <typename L, typename R> concept IntSubtractable = requires { typename IntSubResult<L, R>::type; };

template <typename L, typename R> requires IntAddable<L, R>
constexpr IntScalar<typename IntAddResult<L, R>::type> operator+(IntScalar<L> a, IntScalar<R> b) {
    return IntScalar<typename IntAddResult<L, R>::type>{a.raw() + b.raw()};
}

template <typename L, typename R> requires IntSubtractable<L, R>
constexpr IntScalar<typename IntSubResult<L, R>::type> operator-(IntScalar<L> a, IntScalar<R> b) {
    return IntScalar<typename IntSubResult<L, R>::type>{a.raw() - b.raw()};
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: all 6 tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/types.hpp tests/test_virtual_list.cpp tests/meson.build
git commit -m "feat: add IntScalar<Tag> strong types for ItemIndex/ItemCount"
```

---

### Task 2: LayoutKind::VirtualList Enum Variant

**Files:**
- Modify: `include/prism/core/delegate.hpp:36`
- Modify: `include/prism/core/layout.hpp:30`

- [ ] **Step 1: Add VirtualList to LayoutKind enum**

In `include/prism/core/delegate.hpp`, line 36, change:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll };
```

to:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList };
```

- [ ] **Step 2: Add VirtualList to LayoutNode::Kind enum**

In `include/prism/core/layout.hpp`, line 30, change:

```cpp
enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll } kind = Kind::Leaf;
```

to:

```cpp
enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList } kind = Kind::Leaf;
```

- [ ] **Step 3: Run existing tests to verify nothing broke**

Run: `meson test -C builddir --print-errorlogs`
Expected: all existing tests PASS (new enum value is unused)

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/delegate.hpp include/prism/core/layout.hpp
git commit -m "feat: add LayoutKind::VirtualList enum variant"
```

---

### Task 3: VirtualListState and Node Fields

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (VirtualListState struct, near ScrollState)
- Modify: `include/prism/core/node.hpp` (virtual list fields)
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing test for VirtualListState**

Append to `tests/test_virtual_list.cpp`:

```cpp
#include <prism/core/widget_tree.hpp>

TEST_CASE("VirtualListState default construction") {
    prism::VirtualListState state;
    CHECK(state.item_count.raw() == 0);
    CHECK(state.scroll_offset.raw() == doctest::Approx(0));
    CHECK(state.viewport_h.raw() == doctest::Approx(0));
    CHECK(state.visible_start.raw() == 0);
    CHECK(state.visible_end.raw() == 0);
    CHECK(state.overscan.raw() == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: compilation error — `VirtualListState` not defined

- [ ] **Step 3: Add VirtualListState to widget_tree.hpp**

In `include/prism/core/widget_tree.hpp`, after the `WidgetNode` struct (after line 49), add:

```cpp
struct VirtualListState {
    ItemCount item_count{0};
    Height item_height{0};
    DY scroll_offset{0};
    Height viewport_h{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};
    ScrollBarPolicy scrollbar{ScrollBarPolicy::Auto};
    uint8_t show_ticks = 0;

    // Recycling pool of detached WidgetNodes
    std::vector<WidgetNode> pool;

    // Row factory: creates/rebinds a WidgetNode for item at index
    std::function<void(WidgetNode&, size_t index)> bind_row;

    // Unbind: disconnect subscriptions, clear state
    std::function<void(WidgetNode&)> unbind_row;
};
```

- [ ] **Step 4: Add virtual list fields to Node**

In `include/prism/core/node.hpp`, after the scroll metadata fields (after line 30, before the closing `};`), add:

```cpp
    // Virtual list metadata (only meaningful when layout_kind == VirtualList)
    std::function<void(WidgetNode&, size_t index)> vlist_bind_row;
    std::function<void(WidgetNode&)> vlist_unbind_row;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_insert;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_remove;
    std::function<Connection(size_t, std::function<void()>)> vlist_on_update;
    size_t vlist_item_count = 0;
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp include/prism/core/node.hpp tests/test_virtual_list.cpp
git commit -m "feat: add VirtualListState and Node virtual list fields"
```

---

### Task 4: Visible Range Computation

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (free function)
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing tests for visible range**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("compute_visible_range — basic viewport") {
    // 100 items, 30px each, viewport 100px, no scroll, overscan 2
    auto [start, end] = prism::compute_visible_range(
        ItemCount{100}, Height{30}, DY{0}, Height{100}, ItemCount{2});
    // visible: items 0..3 (ceil(100/30)=4), with overscan: 0..min(6,100)
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 6);
}

TEST_CASE("compute_visible_range — scrolled down") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{100}, Height{30}, DY{90}, Height{100}, ItemCount{2});
    // first visible = floor(90/30) = 3, with overscan start = max(0, 3-2) = 1
    // last visible = ceil((90+100)/30) = 7, with overscan end = min(9, 100) = 9
    CHECK(start.raw() == 1);
    CHECK(end.raw() == 9);
}

TEST_CASE("compute_visible_range — clamp to item count") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{5}, Height{30}, DY{0}, Height{300}, ItemCount{2});
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 5);
}

TEST_CASE("compute_visible_range — empty list") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{0}, Height{30}, DY{0}, Height{100}, ItemCount{2});
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: compilation error — `compute_visible_range` not defined

- [ ] **Step 3: Implement compute_visible_range**

In `include/prism/core/widget_tree.hpp`, after the `VirtualListState` struct, add:

```cpp
inline std::pair<ItemIndex, ItemIndex> compute_visible_range(
    ItemCount item_count, Height item_height, DY scroll_offset,
    Height viewport_h, ItemCount overscan)
{
    if (item_count.raw() == 0 || item_height.raw() <= 0.f)
        return {ItemIndex{0}, ItemIndex{0}};

    float h = item_height.raw();
    size_t first_visible = static_cast<size_t>(std::max(0.f, scroll_offset.raw() / h));
    size_t last_visible = static_cast<size_t>(
        std::ceil((scroll_offset.raw() + viewport_h.raw()) / h));

    size_t start = (first_visible >= overscan.raw())
        ? first_visible - overscan.raw() : 0;
    size_t end = std::min(last_visible + overscan.raw(), item_count.raw());

    return {ItemIndex{start}, ItemIndex{end}};
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: all range tests PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: add compute_visible_range pure function"
```

---

### Task 5: ViewBuilder::list() Overloads

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (ViewBuilder class, ~lines 820-868)
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing test for vb.list() with default delegate**

Append to `tests/test_virtual_list.cpp`:

```cpp
#include <prism/core/list.hpp>

struct StringListModel {
    List<std::string> items;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.list(items);
    }
};

TEST_CASE("ViewBuilder::list creates VirtualList node") {
    StringListModel model;
    model.items.push_back("alpha");
    model.items.push_back("beta");
    model.items.push_back("gamma");

    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: compilation error — no `list` method on ViewBuilder

- [ ] **Step 3: Implement ViewBuilder::list()**

In `include/prism/core/widget_tree.hpp`, add `#include <prism/core/list.hpp>` to the includes (after `#include <prism/core/node.hpp>`).

In the ViewBuilder class, after the `canvas()` method (after line 868), add in the public section:

```cpp
        template <typename T>
        void list(List<T>& items) {
            list(items, [](ViewBuilder& vb, Field<T>& field, ItemIndex) {
                vb.widget(field);
            });
        }

        template <typename T, typename RowBuilder>
        void list(List<T>& items, RowBuilder row_builder) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::VirtualList;
            container.vlist_item_count = items.size();

            container.vlist_bind_row = [this, &items, row_builder](WidgetNode& wn, size_t index) {
                // Store a Field<T> in edit_state as the mirrored value
                wn.edit_state = Field<T>{items[index]};
                auto& field = std::any_cast<Field<T>&>(wn.edit_state);

                wn.focus_policy = Delegate<T>::focus_policy;
                wn.dirty = true;
                wn.is_container = false;
                wn.draws.clear();
                wn.overlay_draws.clear();

                wn.record = [&field](WidgetNode& node) {
                    node.draws.clear();
                    node.overlay_draws.clear();
                    Delegate<T>::record(node.draws, field, node);
                };
                wn.record(wn);

                wn.wire = [&field](WidgetNode& node) {
                    node.connections.push_back(
                        node.on_input.connect([&field, &node](const InputEvent& ev) {
                            Delegate<T>::handle_input(field, ev, node);
                        })
                    );
                };
            };

            container.vlist_unbind_row = [](WidgetNode& wn) {
                wn.connections.clear();
                wn.draws.clear();
                wn.overlay_draws.clear();
                wn.edit_state.reset();
                wn.wire = nullptr;
                wn.record = nullptr;
                wn.dirty = false;
            };

            container.vlist_on_insert = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_insert().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };
            container.vlist_on_remove = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_remove().connect(
                    [cb = std::move(cb)](size_t) { cb(); });
            };
            container.vlist_on_update = [&items](size_t, std::function<void()> cb) -> Connection {
                return items.on_update().connect(
                    [cb = std::move(cb)](size_t, const auto&) { cb(); });
            };

            current_parent().children.push_back(std::move(container));
        }
```

Note: The `row_builder` parameter for the custom overload will be integrated in a later task. The default overload uses `vb.widget(field)` directly via the bind lambda — it doesn't go through ViewBuilder for default delegates since each item is a single leaf.

- [ ] **Step 4: Run test to verify it compiles**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: likely fails at runtime because `build_widget_node`/`build_layout` don't handle VirtualList yet — that's expected. The test just needs to compile.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: add ViewBuilder::list() overloads"
```

---

### Task 6: build_widget_node and connect_dirty for VirtualList

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (~lines 1079-1128)
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing test**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("VirtualList materializes visible items") {
    StringListModel model;
    for (int i = 0; i < 20; ++i)
        model.items.push_back("item " + std::to_string(i));

    WidgetTree tree(model);
    // Viewport 100px tall — with ~24px per string item, about 4-5 visible + overscan
    auto snap = tree.build_snapshot(400, 100, 1);
    CHECK(snap != nullptr);
    // Should have some geometry entries but far fewer than 20
    CHECK(snap->geometry.size() < 20);
    CHECK(snap->geometry.size() > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: failure — VirtualList not handled in `build_widget_node`

- [ ] **Step 3: Extend build_widget_node for VirtualList**

In `include/prism/core/widget_tree.hpp`, in the `build_widget_node` method (~line 1079), add a VirtualList case. Change the container branch (after the Scroll case at line 1096):

```cpp
            if (node.layout_kind == LayoutKind::Scroll) {
                ScrollState ss;
                ss.scrollbar = node.scroll_bar_policy;
                ss.event_policy = node.scroll_event_policy;
                wn.edit_state = ss;
                if (node.build_widget)
                    node.build_widget(wn);  // Field<ScrollArea> overrides
            } else if (node.layout_kind == LayoutKind::VirtualList) {
                VirtualListState vls;
                vls.item_count = ItemCount{node.vlist_item_count};
                if (node.vlist_bind_row) vls.bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls.unbind_row = node.vlist_unbind_row;
                wn.edit_state = std::move(vls);
                // Don't build children from Node — they're materialized dynamically
            }
```

For VirtualList, do NOT iterate `node.children` (there are none). Children are materialized in the layout/snapshot phase.

Replace the child loop at line 1097-1098 with a conditional:

```cpp
            if (node.layout_kind != LayoutKind::VirtualList) {
                for (auto& child : node.children)
                    wn.children.push_back(build_widget_node(child));
            }
```

- [ ] **Step 4: Extend connect_dirty for VirtualList**

In the `connect_dirty` method (~line 1103), add handling for VirtualList containers. In the `else` (container) branch, after the scroll `on_change` check:

```cpp
        } else {
            // Scroll containers with Field<ScrollArea> have their own on_change
            if (node.on_change) {
                auto id = wn.id;
                wn.connections.push_back(
                    node.on_change([this, id]() { mark_dirty(root_, id); })
                );
            }

            // Virtual list: connect List<T> signals
            if (node.layout_kind == LayoutKind::VirtualList) {
                auto id = wn.id;
                if (node.vlist_on_insert) {
                    wn.connections.push_back(
                        node.vlist_on_insert(0, [this, id]() { mark_dirty(root_, id); })
                    );
                }
                if (node.vlist_on_remove) {
                    wn.connections.push_back(
                        node.vlist_on_remove(0, [this, id]() { mark_dirty(root_, id); })
                    );
                }
                if (node.vlist_on_update) {
                    wn.connections.push_back(
                        node.vlist_on_update(0, [this, id]() { mark_dirty(root_, id); })
                    );
                }
                return; // no child nodes to recurse into
            }

            assert(node.children.size() == wn.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
                connect_dirty(node.children[i], wn.children[i]);
        }
```

- [ ] **Step 5: Run test — will still fail (build_layout not done yet)**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: may crash or produce empty snapshot — `build_layout` doesn't handle VirtualList yet

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: build_widget_node and connect_dirty for VirtualList"
```

---

### Task 7: build_layout and Materialization

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (~lines 1308-1353, build_layout and new materialize helper)

This is where the pool gets populated. When `build_layout` encounters a VirtualList WidgetNode, it materializes children for the visible range, builds LayoutNodes from them, and creates a Scroll-like container.

- [ ] **Step 1: Add materialize_visible_items helper**

In `include/prism/core/widget_tree.hpp`, in the private section of WidgetTree (before `build_layout`), add:

```cpp
    void materialize_virtual_list(WidgetNode& node) {
        auto* vls = std::any_cast<VirtualListState>(&node.edit_state);
        if (!vls || !vls->bind_row) return;

        auto [new_start, new_end] = compute_visible_range(
            vls->item_count, vls->item_height, vls->scroll_offset,
            vls->viewport_h, vls->overscan);

        // Unbind children that are no longer visible
        // Move from children back to pool in reverse order
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
            // Remove from index
            index_.erase(it->id);
            parent_map_.erase(it->id);
            // Remove from focus order
            std::erase(focus_order_, it->id);
            if (vls->unbind_row) vls->unbind_row(*it);
            vls->pool.push_back(std::move(*it));
        }
        node.children.clear();

        // Bind children for visible range
        for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
            WidgetNode wn;
            if (!vls->pool.empty()) {
                wn = std::move(vls->pool.back());
                vls->pool.pop_back();
            } else {
                wn.id = next_id_++;
            }
            vls->bind_row(wn, i);
            // Wire input connections
            if (wn.wire) {
                wn.wire(wn);
                wn.wire = nullptr;
            }
            // Register in index
            index_[wn.id] = nullptr; // pointer fixed after push_back
            parent_map_[wn.id] = node.id;
            if (wn.focus_policy != FocusPolicy::none)
                focus_order_.push_back(wn.id);
            node.children.push_back(std::move(wn));
        }

        // Fix index pointers (invalidated by push_back)
        for (auto& c : node.children)
            index_[c.id] = &c;

        vls->visible_start = new_start;
        vls->visible_end = new_end;
    }
```

- [ ] **Step 2: Add VirtualList case to build_layout**

In `build_layout` (~line 1332), add the VirtualList case after the Scroll case:

```cpp
        } else if (node.layout_kind == LK::VirtualList) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::VirtualList;
            container.id = node.id;
            if (auto* vls = std::any_cast<VirtualListState>(&node.edit_state)) {
                container.scroll_offset = vls->scroll_offset;
                container.scroll_content_h = Height{
                    static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
            }
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
```

- [ ] **Step 3: Call materialize in build_snapshot**

In `build_snapshot` (~line 1035), after `refresh_dirty(root_)` and before building the layout tree, add a call to materialize all virtual lists:

```cpp
        refresh_dirty(root_);
        materialize_all_virtual_lists(root_);
```

And add the helper in the private section:

```cpp
    void materialize_all_virtual_lists(WidgetNode& node) {
        if (node.layout_kind == LayoutKind::VirtualList) {
            materialize_virtual_list(node);
        }
        for (auto& c : node.children)
            materialize_all_virtual_lists(c);
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: "VirtualList materializes visible items" should PASS (geometry count < 20 and > 0)

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "feat: virtual list materialization and build_layout"
```

---

### Task 8: Layout Measure/Arrange/Flatten for VirtualList

**Files:**
- Modify: `include/prism/core/layout.hpp`
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing layout tests**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("VirtualList layout — measures as expandable") {
    LayoutNode vlist;
    vlist.kind = LayoutNode::Kind::VirtualList;
    vlist.id = 1;
    vlist.scroll_content_h = Height{3000}; // 100 items * 30px

    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{30}}},
        Color::rgba(255, 0, 0));
    vlist.children.push_back(std::move(child));

    layout_measure(vlist, LayoutAxis::Vertical);
    CHECK(vlist.hint.expand);
}

TEST_CASE("VirtualList flatten clips and shows scrollbar") {
    LayoutNode vlist;
    vlist.kind = LayoutNode::Kind::VirtualList;
    vlist.id = 1;
    vlist.scroll_offset = DY{0};
    vlist.allocated = Rect{Point{X{0}, Y{0}}, Size{Width{400}, Height{100}}};
    vlist.scroll_content_h = Height{3000};

    // One materialized child
    LayoutNode child;
    child.kind = LayoutNode::Kind::Leaf;
    child.id = 2;
    child.draws.filled_rect(
        Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{30}}},
        Color::rgba(255, 0, 0));
    child.allocated = Rect{Point{X{0}, Y{0}}, Size{Width{400}, Height{30}}};
    vlist.children.push_back(std::move(child));

    SceneSnapshot snap;
    layout_flatten(vlist, snap);

    // Should have ClipPush
    bool has_clip = false;
    for (auto& dl : snap.draw_lists) {
        for (auto& cmd : dl.commands) {
            if (std::holds_alternative<ClipPush>(cmd)) has_clip = true;
        }
    }
    CHECK(has_clip);

    // Should have scrollbar overlay (content 3000 > viewport 100)
    CHECK(!snap.overlay_geometry.empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: tests fail — VirtualList not handled in measure/arrange/flatten

- [ ] **Step 3: Add VirtualList to layout_measure**

In `include/prism/core/layout.hpp`, in `layout_measure`, add a case before the closing `}` of the switch (after the Scroll case at line 108):

```cpp
    case LayoutNode::Kind::VirtualList: {
        float max_cross = 0;
        for (auto& child : node.children) {
            layout_measure(child, LayoutAxis::Vertical);
            max_cross = std::max(max_cross, child.hint.cross);
        }
        node.hint.expand = true;
        node.hint.preferred = 0;
        node.hint.cross = max_cross;
        return;
    }
```

- [ ] **Step 4: Add VirtualList to layout_arrange**

In `layout_arrange`, after the Scroll handling (after line 129), add:

```cpp
    if (node.kind == LayoutNode::Kind::VirtualList) {
        float item_h = 0;
        if (!node.children.empty()) {
            // Use first child's measured height as uniform item height
            item_h = node.children[0].hint.preferred;
        }
        float offset = 0;
        // Position materialized children at their absolute positions based on scroll
        // The scroll offset translation happens in flatten, just like Scroll
        for (auto& child : node.children) {
            layout_arrange(child, {
                Point{available.origin.x, Y{available.origin.y.raw() + offset}},
                Size{available.extent.w, Height{item_h > 0 ? item_h : child.hint.preferred}}
            });
            offset += (item_h > 0 ? item_h : child.hint.preferred);
        }
        return;
    }
```

Wait — the children are already positioned at their logical position based on index. We need to position them at `index * item_height`. But we don't know the item indices here. The simpler approach: arrange children sequentially from `visible_start * item_height`. We need to pass that information through the LayoutNode.

Add a field to LayoutNode (in layout.hpp, after `scroll_content_h`):

```cpp
    size_t vlist_visible_start = 0;  // first materialized item index (for VirtualList)
    float vlist_item_height = 0;     // uniform item height (for VirtualList)
```

Then in `layout_arrange`, the VirtualList case becomes:

```cpp
    if (node.kind == LayoutNode::Kind::VirtualList) {
        float item_h = node.vlist_item_height;
        if (item_h <= 0 && !node.children.empty()) {
            // Fallback: measure first child
            item_h = node.children[0].hint.preferred;
        }
        float start_y = available.origin.y.raw()
            + static_cast<float>(node.vlist_visible_start) * item_h;
        for (auto& child : node.children) {
            layout_arrange(child, {
                Point{available.origin.x, Y{start_y}},
                Size{available.extent.w, Height{item_h}}
            });
            start_y += item_h;
        }
        node.scroll_content_h = Height{
            static_cast<float>(node.scroll_content_h.raw())}; // already set from build_layout
        return;
    }
```

- [ ] **Step 5: Add VirtualList to layout_flatten**

In `layout_flatten`, after the Scroll block (before the leaf handling at line 251), add:

```cpp
    if (node.kind == LayoutNode::Kind::VirtualList) {
        // ClipPush for the viewport
        DrawList clip_dl;
        clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
        snap.geometry.push_back({node.id, node.allocated});
        snap.draw_lists.push_back(std::move(clip_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Flatten children with scroll offset
        DY scroll_dy = node.scroll_offset;
        DY neg_scroll{-scroll_dy.raw()};
        for (auto& child : node.children) {
            float child_top = child.allocated.origin.y.raw() - scroll_dy.raw();
            float child_bottom = child_top + child.allocated.extent.h.raw();
            float vp_top = node.allocated.origin.y.raw();
            float vp_bottom = vp_top + node.allocated.extent.h.raw();

            if (child_bottom <= vp_top || child_top >= vp_bottom)
                continue;

            detail::offset_subtree_y(child, neg_scroll);
            layout_flatten(child, snap);
            DY restore{scroll_dy.raw()};
            detail::offset_subtree_y(child, restore);
        }

        // ClipPop
        DrawList clip_pop_dl;
        clip_pop_dl.clip_pop();
        snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});
        snap.draw_lists.push_back(std::move(clip_pop_dl));
        snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

        // Scrollbar overlay
        if (node.scroll_content_h.raw() > node.allocated.extent.h.raw()) {
            constexpr float track_width = 6.f;
            constexpr float track_inset = 8.f;
            constexpr float min_thumb_h = 20.f;

            auto vp = node.allocated;
            float viewport_h = vp.extent.h.raw();
            float content_h = node.scroll_content_h.raw();
            float thumb_h = std::max(min_thumb_h, viewport_h * (viewport_h / content_h));
            float max_scroll = content_h - viewport_h;
            float thumb_y = (max_scroll > 0)
                ? node.scroll_offset.raw() * (viewport_h - thumb_h) / max_scroll
                : 0.f;

            Rect thumb_rect{
                Point{X{vp.origin.x.raw() + vp.extent.w.raw() - track_inset},
                      Y{vp.origin.y.raw() + thumb_y}},
                Size{Width{track_width}, Height{thumb_h}}};
            snap.overlay.filled_rect(thumb_rect, Color::rgba(120, 120, 130, 160));
            snap.overlay_geometry.push_back({node.id, thumb_rect});
        }

        return;
    }
```

- [ ] **Step 6: Update build_layout to pass vlist fields to LayoutNode**

In `build_layout` (widget_tree.hpp), update the VirtualList case to set the new LayoutNode fields:

```cpp
        } else if (node.layout_kind == LK::VirtualList) {
            LayoutNode container;
            container.kind = LayoutNode::Kind::VirtualList;
            container.id = node.id;
            if (auto* vls = std::any_cast<VirtualListState>(&node.edit_state)) {
                container.scroll_offset = vls->scroll_offset;
                container.scroll_content_h = Height{
                    static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
                container.vlist_visible_start = vls->visible_start.raw();
                container.vlist_item_height = vls->item_height.raw();
            }
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
```

- [ ] **Step 7: Run tests**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: all layout tests PASS

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/layout.hpp include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: VirtualList layout measure/arrange/flatten"
```

---

### Task 9: Item Height Measurement and update_scroll_state

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_virtual_list.cpp`

The item height is measured from the first item on the first `build_snapshot` call. The `update_scroll_state` method syncs viewport size back to `VirtualListState`.

- [ ] **Step 1: Write failing test**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("VirtualList measures item height from first item") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back("item " + std::to_string(i));

    WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 100, 1);
    auto snap2 = tree.build_snapshot(400, 100, 2);

    // After two snapshots, the list should have stabilized
    // and have consistent geometry
    CHECK(snap2->geometry.size() > 0);
    CHECK(snap2->geometry.size() == snap1->geometry.size());
}
```

- [ ] **Step 2: Run test to verify it fails or is flaky**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: may fail — item_height is 0, so materialization produces wrong range

- [ ] **Step 3: Implement item height measurement in materialize_virtual_list**

In `materialize_virtual_list`, before computing visible range, add height measurement logic:

```cpp
    void materialize_virtual_list(WidgetNode& node) {
        auto* vls = std::any_cast<VirtualListState>(&node.edit_state);
        if (!vls || !vls->bind_row) return;

        // Update item count (may have changed via List signals)
        // (item_count is updated by signal handlers — see connect_dirty)

        // Measure item height from first item if not yet known
        if (vls->item_height.raw() <= 0.f && vls->item_count.raw() > 0) {
            WidgetNode probe;
            probe.id = next_id_++;
            vls->bind_row(probe, 0);

            auto bb = probe.draws.bounding_box();
            vls->item_height = bb.extent.h;

            if (vls->unbind_row) vls->unbind_row(probe);
        }

        auto [new_start, new_end] = compute_visible_range(
            vls->item_count, vls->item_height, vls->scroll_offset,
            vls->viewport_h, vls->overscan);

        // ... rest of materialization (unchanged)
```

- [ ] **Step 4: Extend update_scroll_state for VirtualList**

In `update_scroll_state` (~line 1263), add a VirtualList case:

```cpp
    void update_scroll_state(LayoutNode& layout_node) {
        if (layout_node.kind == LayoutNode::Kind::Scroll) {
            // ... existing scroll code unchanged ...
        }
        if (layout_node.kind == LayoutNode::Kind::VirtualList) {
            auto it = index_.find(layout_node.id);
            if (it != index_.end()) {
                auto* vls = std::any_cast<VirtualListState>(&it->second->edit_state);
                if (vls) {
                    vls->viewport_h = layout_node.allocated.extent.h;
                    float content_h = static_cast<float>(vls->item_count.raw())
                        * vls->item_height.raw();
                    DY max_offset{std::max(0.f, content_h - vls->viewport_h.raw())};
                    vls->scroll_offset = DY{std::clamp(
                        vls->scroll_offset.raw(), 0.f, max_offset.raw())};
                    layout_node.scroll_offset = vls->scroll_offset;
                    if (vls->show_ticks > 0) vls->show_ticks--;
                }
            }
        }
        for (auto& child : layout_node.children)
            update_scroll_state(child);
    }
```

- [ ] **Step 5: Run tests**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: item height measurement and VirtualList scroll state sync"
```

---

### Task 10: scroll_at Support for VirtualList

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (~line 911, scroll_at)
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing test**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("scroll_at works on VirtualList") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back("item " + std::to_string(i));

    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto leaf_id = snap->geometry[0].first;

    tree.scroll_at(leaf_id, DY{50});
    CHECK(tree.any_dirty());
}

TEST_CASE("scroll_at clamps VirtualList to bounds") {
    StringListModel model;
    model.items.push_back("only one");

    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], DY{100});
    // Content fits viewport — no scroll should happen
    CHECK_FALSE(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: scroll_at doesn't find VirtualList containers

- [ ] **Step 3: Extend scroll_at for VirtualList**

In `scroll_at` (~line 911), add VirtualList handling alongside Scroll. Change the condition:

```cpp
    void scroll_at(WidgetId target, DY delta) {
        WidgetId current = target;
        while (current != 0) {
            auto it = index_.find(current);
            if (it != index_.end() && it->second->layout_kind == LayoutKind::Scroll) {
                // ... existing Scroll code unchanged ...
            }
            if (it != index_.end() && it->second->layout_kind == LayoutKind::VirtualList) {
                auto* vls = std::any_cast<VirtualListState>(&it->second->edit_state);
                if (vls) {
                    float content_h = static_cast<float>(vls->item_count.raw())
                        * vls->item_height.raw();
                    DY max_offset{std::max(0.f, content_h - vls->viewport_h.raw())};
                    DY new_offset{std::clamp(
                        vls->scroll_offset.raw() + delta.raw(), 0.f, max_offset.raw())};

                    if (std::abs(new_offset.raw() - vls->scroll_offset.raw()) < 0.001f) {
                        // Bubble to parent
                        auto pit = parent_map_.find(current);
                        current = (pit != parent_map_.end()) ? pit->second : 0;
                        continue;
                    }

                    vls->scroll_offset = new_offset;
                    constexpr uint8_t scrollbar_visible_frames = 30;
                    vls->show_ticks = scrollbar_visible_frames;
                    mark_dirty(root_, current);
                    return;
                }
            }
            auto pit = parent_map_.find(current);
            current = (pit != parent_map_.end()) ? pit->second : 0;
        }
    }
```

- [ ] **Step 4: Run tests**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: scroll_at support for VirtualList containers"
```

---

### Task 11: List<T> Signal Handling (Insert/Remove/Update)

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("VirtualList reacts to List push_back") {
    StringListModel model;
    model.items.push_back("initial");

    WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.push_back("added");
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 300, 2);
    CHECK(snap2->geometry.size() > snap1->geometry.size());
}

TEST_CASE("VirtualList reacts to List erase") {
    StringListModel model;
    model.items.push_back("a");
    model.items.push_back("b");
    model.items.push_back("c");

    WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.erase(1);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 300, 2);
    CHECK(snap2->geometry.size() < snap1->geometry.size());
}

TEST_CASE("VirtualList reacts to List set (update)") {
    StringListModel model;
    model.items.push_back("original");

    WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.set(0, "updated");
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: dirty checks may fail — signals mark dirty but item_count isn't updated

- [ ] **Step 3: Update connect_dirty to update item_count on signals**

The current signal handlers just mark dirty. They also need to update `VirtualListState::item_count`. Update the VirtualList signal connections in `connect_dirty`:

```cpp
            if (node.layout_kind == LayoutKind::VirtualList) {
                auto id = wn.id;
                auto* wn_ptr = &wn;
                if (node.vlist_on_insert) {
                    wn.connections.push_back(
                        node.vlist_on_insert(0, [this, id, wn_ptr]() {
                            if (auto* vls = std::any_cast<VirtualListState>(&wn_ptr->edit_state))
                                vls->item_count = ItemCount{vls->item_count.raw() + 1};
                            mark_dirty(root_, id);
                        })
                    );
                }
                if (node.vlist_on_remove) {
                    wn.connections.push_back(
                        node.vlist_on_remove(0, [this, id, wn_ptr]() {
                            if (auto* vls = std::any_cast<VirtualListState>(&wn_ptr->edit_state)) {
                                if (vls->item_count.raw() > 0)
                                    vls->item_count = ItemCount{vls->item_count.raw() - 1};
                            }
                            mark_dirty(root_, id);
                        })
                    );
                }
                if (node.vlist_on_update) {
                    wn.connections.push_back(
                        node.vlist_on_update(0, [this, id]() {
                            mark_dirty(root_, id);
                        })
                    );
                }
                return;
            }
```

Note: `wn_ptr` is safe because the WidgetTree owns the root and never moves it after construction.

- [ ] **Step 4: Run tests**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: all signal tests PASS

- [ ] **Step 5: Run all tests to verify no regressions**

Run: `meson test -C builddir --print-errorlogs`
Expected: all tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: List<T> signal handling for VirtualList insert/remove/update"
```

---

### Task 12: Integration Test — Full Scroll Workflow

**Files:**
- Modify: `tests/test_virtual_list.cpp`

- [ ] **Step 1: Write integration test**

Append to `tests/test_virtual_list.cpp`:

```cpp
TEST_CASE("VirtualList full scroll workflow") {
    StringListModel model;
    for (int i = 0; i < 100; ++i)
        model.items.push_back("item " + std::to_string(i));

    WidgetTree tree(model);

    // First snapshot — get initial state
    auto snap1 = tree.build_snapshot(400, 100, 1);
    REQUIRE(snap1 != nullptr);
    size_t initial_count = snap1->geometry.size();
    CHECK(initial_count > 0);
    CHECK(initial_count < 100); // virtualized

    // Scroll down
    auto leaf_id = snap1->geometry[1].first; // pick a visible item
    tree.scroll_at(leaf_id, DY{200});
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 100, 2);
    // Geometry should be roughly the same count (same viewport)
    CHECK(snap2->geometry.size() > 0);

    // Scrollbar overlay should be present
    CHECK(!snap2->overlay_geometry.empty());
}

TEST_CASE("VirtualList with custom row builder") {
    struct Item {
        std::string name;
        int score;
    };

    struct ItemListModel {
        List<Item> items;
        void view(WidgetTree::ViewBuilder& vb) {
            vb.list(items, [](WidgetTree::ViewBuilder& vb, Field<Item>& item, ItemIndex) {
                vb.widget(item);
            });
        }
    };

    ItemListModel model;
    model.items.push_back(Item{"Alice", 100});
    model.items.push_back(Item{"Bob", 200});

    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() > 0);
}
```

- [ ] **Step 2: Run tests**

Run: `meson test -C builddir virtual_list --print-errorlogs`
Expected: all tests PASS

- [ ] **Step 3: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`
Expected: all tests PASS — no regressions

- [ ] **Step 4: Commit**

```bash
git add tests/test_virtual_list.cpp
git commit -m "test: add VirtualList integration tests"
```

---

## Summary

| Task | What it does | Files touched |
|------|-------------|---------------|
| 1 | IntScalar strong types | types.hpp, test, meson |
| 2 | LayoutKind::VirtualList enum | delegate.hpp, layout.hpp |
| 3 | VirtualListState + Node fields | widget_tree.hpp, node.hpp, test |
| 4 | compute_visible_range | widget_tree.hpp, test |
| 5 | ViewBuilder::list() | widget_tree.hpp, test |
| 6 | build_widget_node + connect_dirty | widget_tree.hpp, test |
| 7 | build_layout + materialization | widget_tree.hpp |
| 8 | Layout measure/arrange/flatten | layout.hpp, widget_tree.hpp, test |
| 9 | Item height measurement + scroll state | widget_tree.hpp, test |
| 10 | scroll_at for VirtualList | widget_tree.hpp, test |
| 11 | List<T> signal handling | widget_tree.hpp, test |
| 12 | Integration tests | test |
