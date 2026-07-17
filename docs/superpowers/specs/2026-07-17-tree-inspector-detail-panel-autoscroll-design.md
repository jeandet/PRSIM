# Tree Inspector Detail Panel & Auto-scroll — Design Spec

**Date:** 2026-07-17
**Status:** Approved (pending spec review)
**Extends:** [Live Tree Inspector](2026-07-15-live-tree-inspector-design.md) — two items its own
"Still not implemented, deliberately deferred" note called out (`scroll_to`) plus a gap found during
post-merge dogfooding (no per-node detail beyond the row's own name/type).

## Problem

The tree inspector debug window (`include/prism/widgets/debug/tree_inspector.hpp`) currently shows
only a flat, scrollable list of `NodeRow`s — indent, expand marker, name, dirty accent. Two gaps
remain from the original spec and its post-merge fixes:

1. **No detail beyond the row itself.** `NodeRow` already carries `id`, `layout_kind_name`, `rect`,
   `dirty`, `hovered`, `focused`, `pressed`, `has_children`/`expanded` — but nothing surfaces most of
   it. Selecting a row gives no more information than its name and a highlight.
2. **No auto-scroll.** Hovering a node in the main window drives `NodeRow::hovered` (via
   `flatten_tree`, `tree_inspector.hpp:62`) and the visual highlight in `Widget<NodeRow>::record`
   (`tree_inspector.hpp:103`), but if the corresponding row is off-screen in the debug window's
   `VirtualList`, nothing brings it into view. Documented as deferred in
   `TreeInspectorController::refresh()`'s existing comment (`tree_inspector.hpp:176-181`).

## Scope

Both features live entirely inside the two files the tree inspector already touches
(`include/prism/widgets/debug/tree_inspector.hpp`, `include/prism/app/model_app.hpp`), plus one new
public method on `WidgetTree` (`include/prism/app/widget_tree.hpp`). No new files.

## Solution

### Architecture

1. **Detail panel** — `TreeInspectorModel::view()` changes from a single `vb.list(...)` call to an
   `hstack` of the existing list plus a new read-only detail pane. The pane shows every `NodeRow`
   field for whichever row was last clicked, refreshed live on every tick.
2. **Auto-scroll** — `TreeInspectorController` gains a reference to the debug window's own
   `WidgetTree` (a constructor signature change anticipated by the existing deferred-work comment)
   and, on every `refresh()`, scrolls the debug list to reveal the row matching the main tree's
   `hovered_id()` if it isn't already visible.

Both ride the same `refresh()`/`on_row_clicked()` entry points `TreeInspectorController` already
exposes — no new call sites into the controller are needed from `model_app.hpp` beyond passing the
extra constructor argument.

### Components

- **`TreeInspectorModel`** (`tree_inspector.hpp:124-138`) gains one field:
  ```cpp
  Field<std::optional<NodeRow>> detail;
  ```
  `view()` becomes:
  ```cpp
  void view(WidgetTree::ViewBuilder& vb) {
      vb.hstack([&] {
          vb.list<NodeRow>(rows, [this](size_t index, const NodeRow& row) {
              if (on_click) on_click(index, row);
          });
          vb.widget(detail);
      });
  }
  ```

- **New `Widget<std::optional<NodeRow>>`** in `prism::ui` (hand-written, same style as the existing
  `Widget<NodeRow>` in this file, not the `FieldMirror<T>`/annotation machinery built for
  `Inspector<T>`/`Shared<T>` live editing — that machinery targets user-editable model state; every
  field here is inherently read-only debug output, and marking six fields `readonly` via annotations
  just to reuse it would be more machinery than the six `dl.text(...)` calls it replaces).
  `record()` renders "No selection" when `std::nullopt`, else one line per field: name, layout kind,
  rect (`x, y, w, h`), dirty, hovered, focused, pressed. `handle_input` is a no-op, matching
  `Widget<NodeRow>::handle_input` (`tree_inspector.hpp:114-117`).

- **`TreeInspectorController`** (`tree_inspector.hpp:150-198`):
  - Constructor gains a third parameter:
    ```cpp
    TreeInspectorController(WidgetTree& main_tree, WidgetTree& debug_tree,
                             TreeInspectorModel& debug_model)
    ```
    On construction, captures the debug tree's `VirtualList` container id as `list_container_id_ =
    debug_tree.root().children.front().id`. This relies on `ViewBuilder::finalize()`'s single-child
    Row/Column hoist (`widget_tree.hpp:474-480`): `TreeInspectorModel::view()`'s top level is now a
    single `hstack` call, i.e. one `Row`-kind child of the model's wrapper node — exactly the
    hoist's trigger condition — so the hoist fires and replaces `debug_tree.root()` with that `Row`
    directly, splicing the `hstack`'s two children (list, detail) straight onto `root()`. `root()`
    is therefore the `Row` itself, and `.children.front()`/`.children.back()` are the list's
    `VirtualList` container and the detail widget, in that order — not an extra nesting level.
  - New private member: `std::optional<WidgetId> detail_selected_`.
  - `on_row_clicked()` (`tree_inspector.hpp:186-192`) additionally sets `detail_selected_ = row.id`
    and pushes `row` into `debug_model_->detail` immediately (no need to wait for the next
    `refresh()` — the click already has the row's current data in hand).
  - `refresh()` (`tree_inspector.hpp:167-184`) additionally, after re-flattening into `rows`:
    - If `detail_selected_` is set, scans the freshly-flattened rows for a matching `id`. Found →
      push that row into `debug_model_->detail` (keeps rect/dirty/focused/pressed live). Not found
      (its ancestor collapsed, or it no longer exists) → set `debug_model_->detail` to `nullopt`.
    - Locates the row matching `main_tree_->hovered_id()` in the same scan; if found, calls the new
      `debug_tree_->scroll_row_into_view(list_container_id_, index, Widget<NodeRow>::row_h)` — reusing
      the row height already declared `static constexpr` on `Widget<NodeRow>` (`tree_inspector.hpp:94`).
      `scroll_row_into_view` itself is a no-op when the row is already visible, so the controller
      doesn't need its own visibility check. If the hovered id isn't found (its row isn't currently
      expanded/visible), skip the call entirely — no scroll, no error.

- **`WidgetTree::scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h)`**
  (new public method, `widget_tree.hpp`, alongside `scroll_at`/`scroll_to` at lines 515-554).
  Generalizes the inline logic already used for `Table`'s selected-row scrolling
  (`widget_tree.hpp:1504-1515`) via the existing private `get_scroll_view()` helper
  (`widget_tree.hpp:808-816`), which already normalizes `Scroll`/`VirtualList`/`Table` into one
  `ScrollView{offset, viewport_h, content_h, ...}` shape — no new per-layout-kind branching needed:
  ```cpp
  void scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h) {
      auto it = index_.find(container_id);
      if (it == index_.end()) return;
      auto sv = get_scroll_view(*it->second);
      if (!sv) return;
      DY row_top{static_cast<float>(row_index) * row_h.raw()};
      DY row_bottom = row_top + DY{row_h.raw()};
      DY vp_top = sv->offset;
      DY vp_bottom = vp_top + DY{sv->viewport_h.raw()};
      DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};
      if (row_bottom > vp_bottom)
          scroll_to(container_id, DY{std::clamp(row_bottom.raw() - sv->viewport_h.raw(), 0.f, max_off.raw())});
      else if (row_top < vp_top)
          scroll_to(container_id, DY{std::clamp(row_top.raw(), 0.f, max_off.raw())});
  }
  ```
  Reuses the existing public `scroll_to(WidgetId, DY)` (`widget_tree.hpp:545-554`) to apply the
  clamped offset and mark the container dirty, rather than duplicating that clamp/dirty logic here.

- **`model_app.hpp`** (`model_app.hpp:209-219`): after `debug_window_id = registry.add(*win,
  debug_model);`, looks up the new entry the same way `primary_entry` is already looked up
  (`auto* debug_entry = registry.find(*debug_window_id);`) and passes its tree into the controller:
  ```cpp
  debug_controller.emplace(*primary_entry->tree, *debug_entry->tree, debug_model);
  ```

### Data flow

1. **Detail panel**: debug-window row click → `on_row_clicked` → `detail_selected_` set, `detail`
   pushed immediately → panel re-renders this tick. Every subsequent post-dispatch tick →
   `refresh()` re-resolves `detail_selected_` against the fresh flatten → `detail` updated (or
   cleared) → panel stays live or reverts to "No selection".
2. **Auto-scroll**: main-window hover → existing `hovered_id()`/highlight mirroring in `refresh()`
   (unchanged) **+** new: look up the hovered row's index in the same fresh flatten →
   `scroll_row_into_view` on the debug tree, using the container id captured at construction.

### Error handling

- `detail_selected_` row no longer present in the flatten (ancestor collapsed, node removed): `detail`
  resets to `nullopt` — panel shows "No selection" rather than stale data. Never crashes, never shows
  data for a row that no longer exists.
- Hovered row not present in the flatten: `scroll_row_into_view` is simply not called that tick — no
  special-casing needed, mirrors the existing `if (hovered != 0)` guard style already in `refresh()`.
- `scroll_row_into_view` itself: `index_.find` miss or `get_scroll_view` returning `nullopt` (wrong
  node id, or a node with no scroll state) → early return, no-op. Matches `scroll_at`/`scroll_to`'s
  existing defensive style (`widget_tree.hpp:515-554`).
- Debug window close/reopen: `TreeInspectorController` is destroyed and freshly reconstructed each
  time via the existing `reset_debug_inspector`/`debug_controller.emplace(...)` lifecycle
  (`model_app.hpp:88-96`, `209-219`) — `detail_selected_` and `list_container_id_` reset naturally
  with it, no explicit teardown required.

### Non-goals

- No editing of any field from the detail panel — read-only, matching the tree inspector's existing
  "read-only structural/visual introspection" scope (`2026-07-15-live-tree-inspector-design.md`'s
  Non-goals).
- No generalization of `scroll_row_into_view` beyond what this needs (e.g. no horizontal scroll, no
  animated/eased scrolling — instant snap only, consistent with the existing `Table` prior art it's
  modeled on).
- No persistence of `detail_selected_`/scroll position across debug window close/reopen.
- Does not address the separately-known, still-open limitation that an app's own `setup()` calling
  `set_global_key_handler`/`set_post_dispatch_hook` silently overrides the debug hotkey wiring
  (`model_app.hpp:201-205`) — out of scope for this spec.

## Testing

- **`scroll_row_into_view` unit tests** (new, alongside existing `WidgetTree` scroll tests): small
  `VirtualList` fixture with a fixed small viewport; assert scrolling down reveals an
  off-screen-below row (offset increases to align the row's bottom with the viewport bottom), up for
  off-screen-above (offset decreases to align the row's top with the viewport top), and a no-op when
  the row is already fully visible (offset unchanged).
- **Auto-scroll integration test** (extends the existing controller/end-to-end test in
  `tests/test_model_app.cpp` or `tests/test_tree_inspector_controller.cpp`): synthesize enough
  main-window hover events to move the hovered node's row off the debug window's visible range,
  assert the debug tree's scroll offset moves to reveal it after the next `refresh()`.
- **Detail panel — click sets it**: click a row, assert `debug_model_.detail` holds that row's data.
- **Detail panel — live update**: click a row, mutate the underlying main-tree state so that row's
  `dirty`/`rect`/etc. changes, assert the next `refresh()` updates `detail`'s fields accordingly.
- **Detail panel — switch selection**: click row A then row B, assert `detail` reflects B, not A.
- **Detail panel — selection disappears**: click a row, then collapse its ancestor (making it
  unreachable via `flatten_tree`), assert `detail` resets to `nullopt` on the next `refresh()`.
- **`Widget<std::optional<NodeRow>>` rendering test**: `nullopt` renders the "No selection" text;
  `some` renders each field as a distinct text line — same `DrawList`-inspection style already used
  for `Widget<NodeRow>`'s own tests.
- **No-regression**: full existing suite (currently `Ok: 52 / Fail: 0`) must still pass — in
  particular the existing hotkey-attach/hover-select/row-click-highlight/detach end-to-end test,
  since `TreeInspectorController`'s constructor signature change touches its only production call
  site (`model_app.hpp`) and its own dedicated tests.
