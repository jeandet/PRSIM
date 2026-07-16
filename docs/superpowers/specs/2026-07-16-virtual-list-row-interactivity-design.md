# VirtualList Row Interactivity Fix — Design Spec

**Date:** 2026-07-16
**Status:** Approved (pending spec review)
**Blocks:** [Live Tree Inspector](2026-07-15-live-tree-inspector-design.md) (sub-project 2 of 3 under Phase 4.5 #5 — see that spec's "Split during plan-writing" note)

## Problem

`ViewBuilder::list<T>` (`include/prism/app/widget_tree.hpp:191-220`) renders a `List<T>` via
`VirtualList`, materializing one pooled `WidgetNode` per visible row (`vlist_bind_row`). Each row's
value is copied into a **detached** `Field<T>` (`auto field_ptr = std::make_shared<Field<T>>(items[index]);`
— `List<T>::operator[]` only exposes `const T&`, so this is necessarily a copy). `wire` connects
`on_input` to `Widget<T>::handle_input(*field_ptr, ev, node)`, which mutates that detached copy —
but nothing ever writes the result back to `items`. **Today, clicking any interactive row in any
`.list()` silently does nothing observable.** No existing code anywhere exercises this path
(confirmed via search — display-only lists are the only usage in the codebase).

This blocks the tree inspector's row-click-to-select interaction, but is a real, general bug: any
future interactive row (a checkbox-per-item list, a delete button per row, inline-editable text)
would hit the same dead end.

## Scope

A contained fix inside `ViewBuilder::list<T>`/`vlist_bind_row` only. No new files, no new public
types beyond one new optional parameter. Does not touch `Table`'s separate `selected_row` mechanism
or `Widget<T>::handle_input`'s contract for any existing type.

## Solution

### Architecture

Two independent additions to the existing `wire` closure inside `vlist_bind_row`
(`widget_tree.hpp:213-219`):
1. A new connection to `field_ptr->on_change()` that writes the mutated value back to the source
   `List<T>` via `items.set(index, field_ptr->get())`.
2. An optional third connection, gated by a new `on_row_click` parameter, that detects a row press
   and reports `(index, value)` to the caller — independent of whatever `Widget<T>::handle_input`
   does with the same event.

Row-recycling safety was verified before this design: `vlist_unbind_row`
(`widget_tree.hpp:222-230`) always calls `wn.connections.clear()` — which genuinely disconnects via
`Connection::~Connection()`/`disconnect()` (`include/prism/core/connection.hpp:17,31-35`) — before a
pooled node is later re-bound to a different index via `vlist_bind_row`. So a write-back closure
capturing `index` by value is safe: no stale closure from a previous binding can ever fire after
rebinding.

### Components

- **`ViewBuilder::list<T>`** (`widget_tree.hpp:191`, modified signature):
  ```cpp
  template <typename T>
  void list(List<T>& items,
            std::function<void(size_t index, const T& value)> on_row_click = nullptr);
  ```
  Default `nullptr` — every existing call site (e.g. `examples/model_dashboard.cpp:222`,
  `tests/test_virtual_list.cpp`) is unaffected, both in signature and behavior.

- **`vlist_bind_row`** (`widget_tree.hpp:199-220`, modified). Uses the verified `Field<T>` API
  (`include/prism/core/field.hpp:18,21,31`: `const T& get() const`, `void set(T new_value)`,
  `SenderHub<const T&>& on_change()`):
  ```cpp
  container.vlist_bind_row = [&items, on_row_click](WidgetNode& wn, size_t index) {
      auto field_ptr = std::make_shared<Field<T>>(items[index]);
      wn.edit_state = std::shared_ptr<void>(field_ptr);
      wn.focus_policy = Widget<T>::focus_policy;
      wn.dirty = true;
      wn.is_container = false;
      wn.draws.clear();
      wn.overlay_draws.clear();
      wn.record = [field_ptr](WidgetNode& node) {
          node.draws.clear();
          node.overlay_draws.clear();
          Widget<T>::record(node.draws, *field_ptr, node);
      };
      wn.record(wn);
      wn.wire = [field_ptr, &items, index, on_row_click](WidgetNode& node) {
          node.connections.push_back(
              node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                  Widget<T>::handle_input(*field_ptr, ev, node);
              })
          );
          node.connections.push_back(
              field_ptr->on_change().connect([field_ptr, &items, index](const T&) {
                  if (index < items.size()) items.set(index, field_ptr->get());
              })
          );
          if (on_row_click) {
              node.connections.push_back(
                  node.on_input.connect([on_row_click, &items, index](const InputEvent& ev) {
                      auto* mb = std::get_if<MouseButton>(&ev);
                      if (mb && mb->pressed && mb->button == 1 && index < items.size())
                          on_row_click(index, items[index]);
                  })
              );
          }
      };
  };
  ```

### Data flow

1. **Write-back**: user clicks/types on a row → `on_input` fires → `Widget<T>::handle_input`
   mutates the detached `Field<T>` via the widget's own existing `field.set(...)` call (e.g.
   `Checkbox::checked` toggles) → `Field<T>`'s equality-guarded `set()` fires `on_change()` only
   because the value actually differs → the new `on_change` connection writes it back via
   `items.set(index, field_ptr->get())` → `List<T>::on_update()` fires → the row re-renders on the
   next pass with the persisted value.
2. **Selection**: the same `MouseButton` press additionally reaches the `on_row_click` connection,
   independent of step 1 → `on_row_click(index, items[index])` runs synchronously on the app thread,
   in the same event-handling pass.

### Error handling

- Both new closures guard `index < items.size()` before reading/writing — the list could shrink
  between a row's bind and a later input event reaching its (by-then stale) closure; an
  out-of-range access must silently no-op, never crash.
- No behavior change when `on_row_click` is `nullptr` (the default) — existing display-only
  `.list()` usages are byte-identical in behavior.

### Non-goals

- No generic `ListOptions<T>` struct — a single optional callback parameter is sufficient; no other
  row-level hooks are being speculatively added.
- No change to `Widget<T>::handle_input`'s signature or contract for any existing type.
- Does not touch `Table`'s separate, pre-existing `selected_row` mechanism.

## Testing

- **Write-back regression test** (fails today, passes after the fix — the direct reproducer):
  `List<Checkbox> items; items.push_back({.checked = false});` build a snapshot, synthesize a click
  on row 0's checkbox rect, assert `items[0].checked == true` afterward.
- **Click-index callback**: `List<Row> items` (`struct Row { int id; };`, no meaningful
  `handle_input`), pass `on_row_click` capturing into a local vector, synthesize clicks on rows 0
  and 2, assert the callback fired with `(0, items[0])` then `(2, items[2])`, in order.
- **Out-of-range guard**: bind a row, erase it from `items` before the next input event reaches its
  stale closure (simulating a recycling/shrink race), assert no crash and no out-of-bounds access.
- **No-regression**: existing `tests/test_virtual_list.cpp` scroll/materialization/reactivity tests
  must still pass unmodified, confirming the default-`nullptr` path changes nothing observable for
  current callers.
