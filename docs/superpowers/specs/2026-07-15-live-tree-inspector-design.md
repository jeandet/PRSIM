# Live Tree Inspector — Design Spec

**Date:** 2026-07-15
**Status:** Approved (pending spec review)
**Roadmap:** Phase 4.5, strategic priority #5 (debugging/profiling story) — see `project-roadmap`/`project-strategic-direction` memory. First of several sub-projects under that priority; the others (dirty-region viewer, snapshot version viewer, thread-latency overlay, Tracy integration) are out of scope here and will get their own specs.

## Problem

PRISM has no way to look inside a running app's widget tree. Diagnosing "why isn't this
repainting", "why did focus land here", or "what does the tree actually look like after this
`vstack`/`Tabs`/`VirtualList` composition" currently means adding temporary `printf`s or reasoning
from source alone. Every other GUI toolkit with real adoption ships some form of live tree/element
inspector (browser DevTools, Qt's Widget Inspector, Unreal's Widget Reflector); PRISM needs the same
category of tool before the debugging/profiling priority can be considered addressed.

## Scope

This spec covers only the **live tree inspector**: a second window that shows the main window's
live `WidgetTree` as an indented, auto-refreshing list, with bidirectional click/hover highlighting
between the two windows. It does not cover dirty-region visualization, `SceneSnapshot` version
history, thread-latency measurement, or Tracy instrumentation — each is a separate subsystem and
gets its own design pass.

## Solution

### Architecture

Two independent rendering pipelines share one process, one app thread, and one
`stdexec::run_loop` — critically, **no cross-thread synchronization is needed**, unlike
`Inspector<T>`/`Shared<T>` (`project-live-inspector` memory), because both windows are driven by the
same thread:

- **Main window** — the app's existing `model_app()` pipeline, unchanged except for the two small
  hooks described below.
- **Debug window** — a second OS window opened via `Backend::create_window()` (already supports
  multiple windows: `SoftwareBackend` keeps a `std::unordered_map<WindowId, ...>`), running its own
  `model_app`-style loop over a `prism::debug::TreeInspectorModel`.
- **Correlation glue** — `prism::debug::TreeInspectorController` holds a raw pointer to the main
  `WidgetTree` (both live on the app thread; no ownership transfer) plus the selected/hovered
  `WidgetId`s, and drives the per-tick refresh described in Data flow.

### Activation

Compiled in by default, dormant until triggered — no app code required:

- A new meson option, `prism_debug_tools` (`auto` | `enabled` | `disabled`, default `auto` =
  enabled unless `buildtype` is `release`), gates a compile-time define
  `PRISM_DEBUG_TOOLS_ENABLED`.
- When defined, `model_app()` installs a hotkey listener (Ctrl+Shift+I) on the input event stream.
  First press lazily constructs the `TreeInspectorController` (second window + second tree); a
  second press hides it. When the define is absent, none of this code is compiled — a release build
  with the option disabled carries zero footprint, not just a dormant hotkey.
- `meson_options.txt` doesn't exist yet in this repo; this is the first option added to the project.

### Components

- **`prism::debug::NodeRow`** (new, `include/prism/widgets/debug/tree_inspector.hpp`) — flat row:
  `WidgetId id`, `std::string name`, `std::string layout_kind_name`, `int depth`, `Rect rect`,
  `bool dirty/hovered/focused/pressed`, `bool has_children/expanded`.

- **`prism::debug::flatten_tree(const WidgetTree&, const std::set<WidgetId>& expanded) ->
  std::vector<NodeRow>`** — pure depth-first walk that skips children of any node not in
  `expanded`. No rendering dependency; the primary unit-test target.

- **`prism::debug::TreeInspectorModel`** — the debug window's own model:
  `Field<List<NodeRow>> rows` (feeds the existing `VirtualList`), `Field<std::optional<WidgetId>>
  selected`.

- **`WidgetNode::debug_name`** (new field, `#ifdef PRISM_DEBUG_TOOLS_ENABLED` only) —
  human-readable label captured at tree-build time (reflected member identifier where available,
  delegate/sentinel type name otherwise). `WidgetNode` currently has no such field; without it
  `flatten_tree` would only have `WidgetId`/`layout_kind` to show, which isn't useful for
  recognizing a node in a real app's tree.

- **`WidgetTree::set_debug_highlight(std::optional<WidgetId>)`** (new) — records the id; during the
  next `record()` pass, if the id resolves to a live node, injects a `RectOutline` at that node's
  rect into the tree's root overlay `DrawList` (same mechanism the existing popup/overlay system
  already uses — see `project-live-inspector`/overlay memory).

- **`WidgetTree::hovered_id() const`** (new accessor) — exposes the hover id `model_app` already
  tracks internally via its existing `MouseMove → hit_test → update_hover` path
  (`project-roadmap` Phase 3), currently not publicly readable.

- **`prism::debug::TreeInspectorController`** — owns the second `Window` + second `WidgetTree`, the
  hotkey listener, and the per-tick refresh/correlation glue below.

### Data flow

1. Each debug-window tick, the controller re-runs `flatten_tree(main_tree, expanded_set)` and
   wholesale-replaces `TreeInspectorModel::rows`. Rebuilding is O(visible nodes) — hundreds to a few
   thousand in realistic trees — and only happens while the debug window is open, so this is not
   more expensive than the main tree's own per-dirty-frame walk.
2. **Debug → main:** clicking a row selects its `WidgetId` → `main_tree.set_debug_highlight(id)` →
   next main-window `record()` draws the highlight outline in its root overlay.
3. **Main → debug:** hovering in the main window updates `main_tree.hovered_id()` via the existing
   hover path → the controller reads it each tick → sets `TreeInspectorModel::selected` and scrolls
   `VirtualList` to that row.

Dependency to confirm during planning: `VirtualList` doesn't yet have a scroll-to-index primitive
(`project-virtual-list` memory lists insert/remove/update signals, not a programmatic scroll target)
— this needs to be added or an existing mechanism identified before step 3 can be implemented.

### Error handling

- The debug window is entirely opt-in/dormant; if `create_window()` fails (a platform without
  multi-window support), log once and continue — a debugging tool must never be able to crash the
  app it is inspecting.
- `set_debug_highlight` and row lookups always re-resolve by `WidgetId` against the *current* tree
  each tick — never cache a raw `WidgetNode*`/reference across frames. Dynamic content
  (`Table`/`Tabs`/`VirtualList` recycling) can change tree shape between ticks; a stale pointer here
  would be a dangling-reference bug in the tool meant to help find such bugs elsewhere.
- If a selected/hovered id no longer resolves (node removed/recycled since last tick), both the
  highlight injection and the debug-window selection silently clear rather than erroring.

### Non-goals

- Dirty-region visualization, `SceneSnapshot` version history, thread-latency overlay, Tracy
  integration — separate specs.
- Editing tree state from the inspector (this is `Inspector<T>`'s job for model data; the tree
  inspector is read-only structural/visual introspection).
- Multi-app / multi-process inspection — single process only, matching the rest of PRISM's
  same-process threading model.
- Persisting expand/collapse state across app restarts.

## Testing

- **`flatten_tree()`** — pure function tests over a `WidgetTree` fixture: depth ordering,
  expand/collapse filtering, empty tree. No rendering needed.
- **Highlight round-trip** — build a tree, call `set_debug_highlight(id)`, assert the resulting
  overlay `DrawList` contains a `RectOutline` at that node's rect. Headless
  (`NullBackend`/`SoftwareRenderer`), no SDL.
- **Controller integration** — two `WidgetTree` instances (main + debug) driven by the existing
  synchronous test scheduler: simulate a debug-window row click, assert the main tree's overlay
  reflects the highlight; simulate a main-window hover, assert the debug tree's selected row
  updates. Uses existing `HeadlessWindow`/`NullBackend` — no real SDL window required.
- Same-thread design introduces no new TSan/sanitizer surface.
