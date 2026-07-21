# Resizable Split Panes — Design Spec

## Summary

Add a `vb.handle()` marker primitive that, when placed between children of an `hstack`/`vstack`, makes the adjacent panes user-resizable by dragging. No new top-level container call is needed — `hstack`/`vstack` detect the marker automatically and switch that container into "split mode." Supports arbitrary N panes separated by multiple `Handle` markers in one stack (e.g. the tree/detail split, or a 3+ pane resizable layout).

## Motivation

Composed layouts like the tree browser's row-list + detail-panel split are currently fixed: the detail panel gets whatever width its drawn content happens to produce, and there's no way for the user to drag a divider to reclaim space for either side. Any non-trivial app with side-by-side panes (file browser, inspector, dashboard) wants this.

## Data Types

### New LayoutKind

`LayoutNode::Kind` (in `include/prism/ui/layout.hpp`) gains `Handle` alongside the existing `Scroll`/`VirtualList`/`Table`/`Tabs` variants. A `Handle` node is a fixed-thickness, non-expanding leaf: `constexpr Width handle_thickness{6.f}` on the main axis (Row) or the equivalent `Height` (Column), full extent on the cross axis.

### SplitState (internal, mirrors ScrollState)

Stored in the *container's* `WidgetNode::edit_state`, defined beside `ScrollState` in `include/prism/ui/delegate.hpp`:

```cpp
struct SplitState {
    std::vector<float> pane_sizes;  // empty = "not yet user-adjusted"
};
```

Propagated into `LayoutNode` via the same side-channel `build_layout` already uses for `scroll_offset` (a new `LayoutNode::split_sizes` field, populated from `std::any_cast<SplitState>(wn.edit_state).pane_sizes` when present).

### SplitDrag (transient, mirrors ScrollbarDrag)

Held in `WidgetTree`, not persisted across frames:

```cpp
struct SplitDrag {
    WidgetId container_id;
    size_t handle_index;            // which Handle child, among Handle children in the row
    float anchor;                   // mouse coord at drag start (X for Row, Y for Column)
    float orig_before, orig_after;  // sizes of the two adjacent panes at drag start
};
```

### Theme

Two new `Theme` fields (`include/prism/ui/context.hpp`), alongside `scrollbar_thumb`:

```cpp
Color divider       = Color::rgba(90, 90, 100, 160);
Color divider_hover = Color::rgba(130, 130, 145, 200);
```

## Node & Layout Integration

### Measure phase

- `pane_sizes` empty → unchanged behavior. Panes measure exactly as today: expand-fill for an expanding child (e.g. the tree's `VirtualList` row list), content-bbox for a non-expanding one (e.g. the detail panel). `Handle` nodes measure as their fixed thickness, non-expanding.
- `pane_sizes` populated → each pane's `hint.preferred` is read directly from `pane_sizes[i]` instead of being recomputed (no expand, no content-bbox lookup). This is the only branch added to `layout_measure`.

**`layout_arrange`'s existing Row/Column loop is untouched.** Once every pane has an explicit `preferred`, the current "expand-fill the rest, else use preferred" arrange algorithm already produces the right result — there are no expanders left once `pane_sizes` is populated, so it just places each pane at its preferred size in sequence.

### First drag (pane_sizes empty → populated)

`begin_split_drag` snapshots the *actual allocated sizes from the last arrange pass* for every pane in the row into `pane_sizes` — whatever they were, expand-filled or content-derived. From that point on, every pane in the row (including any that used to be "expand") becomes a pinned size that only changes via dragging or the resize rule below. This preserves "content-preferred, like today" for the pre-drag state while giving well-defined behavior once dragging starts.

### Dragging a handle

`update_split_drag` adjusts only the two panes immediately adjacent to that handle: `pane_sizes[i] += delta`, `pane_sizes[i+1] -= delta`, clamped so neither drops below a global minimum, `constexpr Width min_pane_size{24.f}` (or `Height`, by orientation). Total is conserved between just those two panes; panes elsewhere in the row are untouched by this specific drag.

### Resize after at least one drag

If `sum(pane_sizes) + handle_thicknesses != available_main_axis`, every entry in `pane_sizes` is rescaled by the same ratio to fit, preserving relative proportions (e.g. a 30/70 split stays 30/70 across a window resize).

Note: this rescale does **not** enforce `min_pane_size` — a shrinking window can push a pane below the minimum, since refusing to shrink would mean the row no longer fits its allocation. The minimum is a drag-interaction constraint, not an absolute floor against arbitrary window sizes.

### Flatten

`Handle` needs no special flatten case. It's an ordinary leaf: drawn via `record()` as a themed thin bar with hover/pressed highlight via `node_vs(node)`, hit-tested through the normal geometry path (not the overlay mechanism scrollbar thumbs use, since a Handle reserves real layout space rather than floating over content it doesn't own).

## ViewBuilder API

### `vb.handle()`

New primitive alongside `vb.spacer()`/`vb.canvas()`:

```cpp
vb.hstack([&] {
    vb.tree(ctrl);        // or any pane content
    vb.handle();
    vb.widget(ctrl.detail);
});
```

Pushes a `Node{.layout_kind = LayoutKind::Handle}` leaf into `current_parent().children`. Works unchanged inside `vstack` — the drag axis (X vs Y) is inferred from the parent container's orientation at wire-time, not from the call site, so the same `vb.handle()` serves both.

### Container wiring

After the stack's body lambda runs, `hstack`/`vstack` scan `container.children` for any `Handle`-kind entries. If found, each Handle child gets its own `WidgetId` (like any leaf) and a custom `wire` closure — following the same pattern `ViewBuilder::tree()` already uses for the row-container's click wiring (a lambda capturing `container_id` and the handle's index among sibling handles, calling into `WidgetTree`):

```cpp
handle_node.wire = [&tree, container_id, handle_index](WidgetNode& self) {
    self.connections.push_back(self.on_input.connect(
        [&tree, container_id, handle_index](const InputEvent& ev) {
            if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == 1) {
                if (mb->pressed) tree.begin_split_drag(container_id, handle_index, /* anchor coord */);
                else tree.end_split_drag();
            }
        }));
};
```

### WidgetTree additions

Mirroring `begin/update/end_scrollbar_drag` (`include/prism/app/widget_tree.hpp`): `begin_split_drag(container_id, handle_index, anchor)`, `update_split_drag(pos)`, `end_split_drag()`, plus `in_split_drag()`. `begin_split_drag` also performs the first-drag `pane_sizes` snapshot described above if the container's `SplitState` is still empty.

## Input Handling

One new branch in the MouseMove-while-captured path in `include/prism/app/event_routing.hpp` (where `in_scrollbar_drag()` is currently checked, alongside `begin_scrollbar_drag`/`end_scrollbar_drag` on press/release): check `in_split_drag()` before falling through to normal `dispatch(captured_id, ...)`, same shape as the existing scrollbar check.

## What Changes

| File | Change |
|---|---|
| `include/prism/ui/node.hpp` | `LayoutKind::Handle` |
| `include/prism/ui/delegate.hpp` | `SplitState{pane_sizes}` beside `ScrollState` |
| `include/prism/ui/layout.hpp` | `LayoutNode::Kind::Handle`, `split_sizes` field; measure branch reading `pane_sizes` when populated; proportional-rescale step before arrange |
| `include/prism/app/widget_tree.hpp` | `vb.handle()`; post-body Handle-scan + wiring in `hstack`/`vstack`; `begin/update/end_split_drag`, `in_split_drag()`, `SplitDrag` |
| `include/prism/app/event_routing.hpp` | new `in_split_drag()` branch in the MouseMove-while-captured path, alongside the existing scrollbar-drag branch |
| `include/prism/ui/context.hpp` | `Theme::divider`, `Theme::divider_hover` |
| `tests/test_split.cpp` | **New** |
| `tests/meson.build` | Add `test_split.cpp` |

## What Stays Unchanged

- `layout_arrange`'s Row/Column loop (per the measure/arrange section above)
- `Field<T>`, `ScrollArea`, `VirtualList` machinery
- All existing `Delegate<T>` specializations
- Hit-testing (Handle uses the ordinary geometry path, not the overlay one scrollbar thumbs use)

## Testing

`tests/test_split.cpp`:

1. Handle detection turns on split mode for a container
2. Pre-drag layout matches plain hstack/vstack exactly (regression pin against today's behavior)
3. First drag snapshots current allocated sizes, including for a previously-expanding pane
4. Dragging adjusts exactly the two panes adjacent to that handle, clamped at `min_pane_size`
5. In a 3+ pane row, dragging handle N doesn't affect panes beyond N/N+1
6. Proportional rescale on parent resize preserves relative ratios
7. vstack works symmetrically (Y-axis drag, Height sizes)
8. Dragging marks the container dirty

## Out of Scope (v1)

- Cursor-shape change on hover (no OS cursor abstraction exists yet)
- Double-click-to-reset / auto-fit
- Keyboard-driven resize
- Persisting or programmatically observing/setting pane sizes from the app (internal state only, per design decision)
- Collapsing a pane to zero / snap points
