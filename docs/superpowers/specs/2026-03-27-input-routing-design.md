# Input Routing: Hit Test to Field Mutation

**Date:** 2026-03-27
**Status:** Approved design
**Scope:** Wire hit testing to widget-level input dispatch via SenderHub, enabling interactive Field<T> mutation from mouse events

## Context

PRISM has a complete pipeline from model struct to pixels: `Field<T>` + reflection -> `WidgetTree` -> layout -> `SceneSnapshot` -> `SoftwareBackend`. Hit testing (`hit_test()`) resolves a screen point to a `WidgetId`. But nothing connects that WidgetId back to a field or an action. The event loop in `model_app` has `// Future: hit_test + event routing to widget senders`.

This spec closes that gap: mouse click at point P -> hit test -> widget found -> type-erased handler fires -> `field.set()` -> dirty flag -> repaint.

## Design

### WidgetNode gains `on_input` and `record`

Each `WidgetNode` gets two new members:

```cpp
struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    DrawList draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;   // input dispatch
    std::function<void(WidgetNode&)> record;  // re-records draws from field state
};
```

`on_input` is the per-widget input signal. During `build_leaf()`, the reflection walk knows the concrete `Field<T>` type and connects a type-specific handler:

- `Field<bool>`: toggle on mouse press
- Other types: no handler connected (empty hub, events are silently dropped)

`record` captures the field reference and re-records the node's `DrawList` from current field state. Called once during construction (initial recording) and again whenever the node is dirty before snapshot rebuild.

### Widget index for O(1) dispatch

`WidgetTree` maintains a flat index for direct WidgetId -> WidgetNode lookup:

```cpp
class WidgetTree {
    WidgetNode root_;
    WidgetId next_id_ = 1;
    std::unordered_map<WidgetId, WidgetNode*> index_;
};
```

The index is populated in a single post-construction pass (`build_index()`) after the tree is fully built. This avoids pointer invalidation from `std::vector` reallocation during tree construction.

Public dispatch method:

```cpp
void dispatch(WidgetId id, const InputEvent& ev) {
    if (auto it = index_.find(id); it != index_.end())
        it->second->on_input.emit(ev);
}
```

### Dirty re-recording

Before building a snapshot, dirty nodes re-record their DrawList via their stored `record` function:

```cpp
void refresh_dirty(WidgetNode& node) {
    if (node.dirty && node.record)
        node.record(node);
    for (auto& c : node.children)
        refresh_dirty(c);
}
```

`build_snapshot()` calls `refresh_dirty(root_)` as its first step. This ensures the snapshot reflects the latest field values.

For `Field<bool>`, `record_field_widget` reads `field.get()` and changes the background color accordingly — toggling produces a visible change with no new widget types.

### model_app event routing

The event loop in `model_app` gains input routing:

1. Keep a `shared_ptr<const SceneSnapshot>` of the latest snapshot (for hit testing against current geometry).
2. On `MouseButton` events, run `hit_test(*current_snap, position)` and dispatch to the widget.

```cpp
std::shared_ptr<const SceneSnapshot> current_snap;

while (auto ev = input_queue.pop()) {
    // ... WindowClose, WindowResize handling unchanged ...

    if (auto* mb = std::get_if<MouseButton>(&*ev); mb && current_snap) {
        if (auto id = hit_test(*current_snap, mb->position))
            tree.dispatch(*id, *ev);
    }
}

// After rebuild:
auto snap = tree.build_snapshot(w, h, ++version);
current_snap = snap;
backend.submit(std::move(snap));
```

`build_snapshot` returns `shared_ptr` instead of `unique_ptr` so `current_snap` can hold a reference while the backend consumes the same snapshot.

Only `MouseButton` is routed initially. `MouseMove` (hover state), `KeyPress` (focus/text input) use the same `dispatch()` path when their widget handlers exist.

### Field<bool> handler

The concrete handler connected during `build_leaf<bool>()`:

```cpp
node.connections.push_back(
    node.on_input.connect([&field](const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            field.set(!field.get());
    })
);
```

The connection is owned by the node — disconnects when the node is destroyed.

**Note on connection timing:** `on_input` lives inside `WidgetNode`. `SenderHub::connect()` captures `this` (the hub's address) in the detach lambda. Since nodes are moved during tree construction (`build_leaf` returns by value, pushed into parent's `children` vector), connections to `on_input` must be made **after** the node is in its final location.

This does not affect the existing `on_change` connections — those connect to `field.on_change()`, a `SenderHub` on the user's model struct which doesn't move.

The fix: `WidgetNode` gains a `std::function<void(WidgetNode&)> wire` member that captures the field reference. After tree construction, a `build_index()` pass walks every node, populates `index_`, and calls `node.wire(node)` to connect `on_input` handlers when addresses are stable.

## What Changes

| File | Change |
|------|--------|
| `widget_tree.hpp` | `WidgetNode` gains `on_input`, `record`, `wire`. `WidgetTree` gains `index_`, `build_index()`, `dispatch()`, `refresh_dirty()`. `build_snapshot` calls `refresh_dirty` first. |
| `model_app.hpp` | Event loop routes `MouseButton` via `hit_test` -> `dispatch`. Keeps `current_snap`. `build_snapshot` returns `shared_ptr`. |

## What Doesn't Change

- `hit_test.hpp` — unchanged, already returns `optional<WidgetId>`
- `field.hpp`, `connection.hpp` — unchanged
- `scene_snapshot.hpp`, `draw_list.hpp` — unchanged
- Backend interface, layout engine, concurrency primitives — unchanged
- `app.hpp` / `ui.hpp` (manual API) — unaffected

## Tests

| Test | Verifies |
|------|----------|
| `dispatch` emits on correct node | WidgetId -> SenderHub routing works |
| `dispatch` to unknown id is no-op | No crash, no side effects |
| `Field<bool>` toggle via dispatch | MouseButton press toggles field value |
| `refresh_dirty` re-records draws | Dirty node's DrawList updates from field state |
| Integration: inject MouseButton at geometry | Full loop: event -> hit_test -> dispatch -> field.set -> dirty -> new snapshot |

## Future Extensions (not in this spec)

- `MouseMove` -> hover state on widgets (needs `WidgetState` tracking)
- `KeyPress` -> focused widget text input (needs focus manager)
- Delegate-specific handlers (slider drag, dropdown open) when sentinels are implemented
- `on_click` / `on_hover` convenience senders on widget handles
