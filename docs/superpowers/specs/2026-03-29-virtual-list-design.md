# Virtual List Design Spec

## Overview

Virtualized list rendering for `List<T>` collections. Only items in the visible viewport (plus overscan buffer) are materialized as WidgetNodes. A node recycling pool avoids allocation churn. Scroll is implicit — kicks in automatically when content exceeds available space.

## API

```cpp
// Default — each item rendered via Delegate<T>
vb.list(my_strings);

// Custom row layout
vb.list(my_items, [](ViewBuilder& vb, Field<Item>& item, ItemIndex i) {
    vb.hstack(item.name, item.score);
});
```

`ViewBuilder::list` overloads:

```cpp
template <typename T>
void list(List<T>& items);

template <typename T>
void list(List<T>& items,
          std::function<void(ViewBuilder&, Field<T>&, ItemIndex)> row_builder);
```

## Strong Types

Existing `Scalar<Tag>` is float-based (coordinates). Item indices are integers, so we add an integer counterpart:

```cpp
template <typename Tag>
struct IntScalar {
    constexpr IntScalar() : v_(0) {}
    constexpr explicit IntScalar(size_t v) : v_(v) {}
    [[nodiscard]] constexpr size_t raw() const { return v_; }
    constexpr auto operator<=>(const IntScalar&) const = default;
private:
    size_t v_;
};

using ItemIndex = IntScalar<struct ItemIndexTag>;
using ItemCount = IntScalar<struct ItemCountTag>;
```

All internal computations (visible range, pool indexing, signal handlers) use `ItemIndex`/`ItemCount`. No raw `size_t` in the API surface. Arithmetic between `ItemIndex` and `ItemCount` follows the same affine pattern as coordinates: `ItemIndex + ItemCount → ItemIndex`, `ItemIndex - ItemIndex → ItemCount`.

## Data Model

### VirtualListState

Stored in `WidgetNode::edit_state` (same pattern as `ScrollState`):

```cpp
struct VirtualListState {
    ItemCount item_count{0};
    Height item_height;          // uniform, measured from first item
    DY scroll_offset{0};
    Height viewport_h{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};    // one-past-last
    ItemCount overscan{2};       // extra items above/below viewport
    ScrollBarPolicy scrollbar{ScrollBarPolicy::Auto};
};
```

### Node Representation

`vb.list(items)` creates a `Node` with:

- `layout_kind = LayoutKind::VirtualList`
- Type-erased capture of `List<T>&`
- Row factory function: `(ItemIndex) → WidgetNode`
  - Default path: leaf node with `Delegate<T>::record` / `handle_input`
  - Custom path: runs user's `row_builder` through a fresh `ViewBuilder`, producing a subtree

## Layout Integration

### LayoutKind::VirtualList

New variant alongside existing `Default`, `Row`, `Column`, `Spacer`, `Canvas`, `Scroll`.

**Measure phase**:
- Reports `preferred_height = item_count * item_height` as logical content height
- Container expands to fill available space (like Scroll)
- Cross-axis: max child width

**Arrange phase**:
- Only materialized children (visible window) are arranged
- Each child gets `item_height` tall, positioned at `index * item_height` relative to content origin
- No work for off-screen items

**Flatten phase**:
- `clip_push` for viewport rectangle
- Apply `-scroll_offset` to children
- Draw scrollbar overlay proportional to `viewport_h / (item_count * item_height)`
- Scrollbar only shown when content exceeds viewport (implicit scroll)

### Item Height Measurement

On first layout, the list materializes one item, measures it via the delegate's `record()` output bounding box, and stores the height. All subsequent items use that uniform height.

## Node Recycling Pool

### Pool Structure

`std::vector<WidgetNode>` of detached nodes, owned by the virtual list's `WidgetNode`.

### Lifecycle

**Bind** (item scrolls into view):
1. Take node from pool (or create fresh if pool empty)
2. Assign a `Field<T>` mirroring `items[index]`
3. Call `build_widget(node)` — sets up `record`, `wire`, delegates
4. Connect `on_change` subscription on the mirrored field
5. Mark node dirty

**Unbind** (item scrolls out of view):
1. Disconnect all subscriptions (clear `connections`)
2. Clear `draws` and `edit_state`
3. Move node back to pool

**Rebind** (data changes for visible item):
1. Update mirrored `Field<T>` value via `field.set(items[index])`
2. Delegate re-renders via normal dirty tracking

### Temporary Field<T>

Each pool node owns a `Field<T>` instance that mirrors `items[i]`. On bind, `field.set(items[index])`. Delegates operate on this field — no changes to delegate machinery.

### Visible Range Computation

Pure function:

```
first = clamp(scroll_offset / item_height - overscan, 0, item_count)
last  = clamp((scroll_offset + viewport_h) / item_height + overscan, 0, item_count)
```

When `first`/`last` change vs previous values:
- Indices that left the range → unbind, return to pool
- Indices that entered the range → bind from pool

Pool stabilizes at `visible_count + 2 * overscan` nodes after first full scroll.

## List<T> Signal Handling

Wired during `connect_dirty()`, same phase as `Field<T>` subscriptions:

- **`on_insert(i)`**: increment `item_count`. If `i < visible_end`, shift visible items and rebind affected nodes. Mark dirty.
- **`on_remove(i)`**: decrement `item_count`. If `i >= visible_start && i < visible_end`, shift and rebind. Clamp `scroll_offset` if content shrank below viewport. Mark dirty.
- **`on_update(i)`**: if `i >= visible_start && i < visible_end`, update mirrored field for that node. Mark dirty.

## Scroll Handling

Reuses existing `scroll_at()` infrastructure:

- Mouse wheel on the list node → `scroll_at(list_id, delta)`
- `scroll_at` finds the `VirtualList` node (same parent-chain walk as Scroll)
- Updates `VirtualListState::scroll_offset`, clamped to `[0, max(content_h - viewport_h, 0)]`
- Recomputes visible range, recycles nodes as needed
- Marks dirty

Scrollbar overlay rendered in flatten phase when `content_h > viewport_h`.

## Input & Focus

### Hover / Press

Materialized nodes have geometry in the snapshot. `hit_test()`, hover tracking, and press tracking work unchanged.

### Focus Navigation

- **Tab / Shift+Tab**: cycles through materialized focusable nodes
- **Edge scroll**: when focus would move past last/first materialized item, the list scrolls by one item, materializes the target, and focuses it
- **Click-to-focus**: works unchanged (clicked item is materialized)
- **Arrow Up/Down**: move focus to prev/next item, scrolling if needed
- **PageUp/PageDown**: jump by `floor(viewport_h / item_height)` items

### Focus Hook

When focus navigation hits the edge of the materialized range:
1. Virtual list scrolls by one item in the navigation direction
2. New node is materialized via the pool
3. Focus system receives the new node as the target

## Scroll Policy

Scroll is implicit — no user configuration needed for the common case. The scrollbar appears automatically when `item_count * item_height > viewport_h`.

Optional override via additional `list()` overload (future, not in first version):

```cpp
vb.list(my_items, ListConfig{.scrollbar = ScrollBarPolicy::Always});
```

## Testing Strategy

- **Headless tests** (no SDL): verify visible range computation, pool bind/unbind lifecycle, `List<T>` signal handling (insert/remove/update), scroll offset clamping
- **Layout tests**: verify measure reports correct logical height, arrange only positions materialized children, flatten clips correctly
- **Focus tests**: Tab at edge triggers scroll, Arrow keys navigate items
- **Integration test**: `List<string>` with default delegate, scroll through items, verify correct rendering
