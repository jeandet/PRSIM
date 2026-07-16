# Live Tree Inspector — Design Spec

**Date:** 2026-07-15 (fully revised 2026-07-16 after sub-projects 1-2 shipped)
**Status:** Approved (pending spec review) — sub-project 3 of 3, prerequisites COMPLETE
**Roadmap:** Phase 4.5, strategic priority #5 (debugging/profiling story) — see `project-roadmap`/`project-strategic-direction` memory. First of several sub-projects under that priority; the others (dirty-region viewer, snapshot version viewer, thread-latency overlay, Tracy integration) are out of scope here and will get their own specs.

## Revision history

1. **Split (2026-07-16, during initial plan-writing):** this spec originally assumed a dual-window
   event loop and working `VirtualList` row interactivity both already existed. Neither did. Split
   into three sub-projects: [dual-window event loop foundation](2026-07-16-dual-window-event-loop-design.md)
   and [VirtualList row interactivity fix](2026-07-16-virtual-list-row-interactivity-design.md), both
   built and shipped first; this spec covers only the tree inspector itself.
2. **Hotkey ownership moved (2026-07-16):** sub-project 1's `model_app()` can't call
   `attach_secondary_window(..., Model&)` itself — it has no way to know about `TreeInspectorModel`
   without inverting the dependency direction. Sub-project 1 ships only generic primitives
   (`AppContext::registry()`/`backend()`/`set_global_key_handler()`); this spec owns the hotkey,
   build flag, and window-attach call.
3. **Full revision (2026-07-16, after sub-projects 1-2 shipped and this spec's own plan-writing
   began):** verifying this spec's assumptions against the *real*, merged code surfaced one more
   gap and firmed up two previously-open questions:
   - **New gap — nothing propagates "the main tree changed" to the debug window.** Neither
     `model_app()`'s per-event dispatch nor its animation-tick loop had a general "something
     happened, let other entries react" hook. Resolved by adding
     `AppContext::set_post_dispatch_hook(fn)` and changing per-event publish from "publish the one
     entry that got this event" to "publish all dirty entries" — see Architecture.
   - **Highlight injection, now concrete.** There's no existing per-node-by-id overlay hook (the
     only overlay-injection path, `update_canvas_bounds`, is self-scoped to a leaf's own
     `record()`). Resolved by extending `WidgetTree::build_snapshot()` itself with one more step
     after `layout_flatten` — see Architecture.
   - **Window teardown, now concrete** — closes the gap sub-project 1's final review explicitly
     flagged: `SoftwareBackend` had no way to close a secondary window without quitting the whole
     app. Resolved with a new `BackendBase::close_window(WindowId)` and two small,
     backward-compatible changes to `SoftwareBackend::run()`'s close-button handling — see
     Architecture.
   - **Scroll-to-row, confirmed already possible.** `WidgetTree::scroll_to(WidgetId container_id,
     DY offset)` already exists (`widget_tree.hpp`); combined with `VirtualListState::item_height`
     (public), no new primitive is needed.

See `project-tree-inspector-foundations` memory for the as-shipped sub-project 1/2 API surface this
spec builds on.

## Problem

PRISM has no way to look inside a running app's widget tree. Diagnosing "why isn't this
repainting", "why did focus land here", or "what does the tree actually look like after this
`vstack`/`Tabs`/`VirtualList` composition" currently means adding temporary `printf`s or reasoning
from source alone. Every other GUI toolkit with real adoption ships some form of live tree/element
inspector (browser DevTools, Qt's Widget Inspector, Unreal's Widget Reflector); PRISM needs the same
category of tool before the debugging/profiling priority can be considered addressed.

## Scope

This spec covers the **live tree inspector**: a second window that shows the main window's live
`WidgetTree` as an indented, auto-refreshing list, with bidirectional click/hover highlighting
between the two windows. It does not cover dirty-region visualization, `SceneSnapshot` version
history, thread-latency measurement, or Tracy instrumentation — each is a separate subsystem and
gets its own design pass.

## Solution

### Architecture

Two independent rendering pipelines share one process, one app thread, and one `stdexec::run_loop`
— no cross-thread synchronization is needed (unlike `Inspector<T>`/`Shared<T>`), because both
windows are driven by the same thread and the same `WindowRegistry`:

- **Main window** — the app's existing `model_app()` pipeline, plus two small, generic additions
  (below) that benefit any future multi-entry consumer, not just this feature.
- **Debug window** — a second `WindowRegistry` entry: `Window* w = ctx.backend().request_window(cfg);
  if (w) { auto id = ctx.registry().add(*w, tree_inspector_model); ... }`. From that point on it's a
  completely normal registry entry — routed, published, and torn down exactly like the main window.
- **Refresh propagation (new, generic, lands in `model_app.hpp`):**
  - `AppContext::set_post_dispatch_hook(std::function<void()> fn)` — fires once after every
    processed `WindowEvent`, for any window, right before the existing `schedule_tick()` call at the
    end of the dispatch continuation. Same shape as `set_global_key_handler`, one more settable
    callback.
  - The per-event publish step changes from "publish only the entry that received this event"
    (`if (entry->tree->any_dirty() || needs_publish) publish_entry(*entry, wid);`) to "publish every
    dirty entry" (`registry.for_each_dirty(...)`). This was only ever correct for exactly one entry;
    now that a second, cross-updating entry can exist, it needs to generalize. No behavior change
    for the single-window case (there's only ever one dirty entry to publish either way).
  - The tree inspector installs the hook to re-run `flatten_tree(main_tree, expanded_set)` and
    refresh `TreeInspectorModel::rows` after anything happens anywhere — this matches, rather than
    exceeds, the refresh guarantee the rest of PRISM already has (even `Shared<T>`'s cross-thread
    `drain_shared()` today only runs inside the same per-event continuation, so it already only
    refreshes when *some* window event reaches the loop).
- **Highlight injection (new, lands in `WidgetTree`):** `WidgetTree::set_debug_highlight(
  std::optional<WidgetId>)` stores the id and marks the tree dirty (same `set_dirty(...)` primitive
  `scroll_at`/`scroll_to` already use) — without this, `any_dirty()` could stay false and
  `build_snapshot()` would never run again on its own, so the highlight would never actually appear.
  Storage itself is unconditionally compiled — one `std::optional<WidgetId>` member, negligible cost.
  `WidgetTree::build_snapshot()` gets one more step at the very end: after `layout_flatten` has
  produced `snap->geometry` (current-frame-accurate, not stale), look up the highlighted id there and
  append a `RectOutline` directly to `snap->overlay`. No new per-node hook needed — `build_snapshot()`
  is PRISM's own code and already computes exactly the data this needs.
- **Window teardown (new, lands in `BackendBase`/`SoftwareBackend`):** `BackendBase::close_window(
  WindowId)` — new default no-op virtual; `SoftwareBackend`'s override is fire-and-forget (no return
  value needed, unlike `request_window`), using a small dedicated `mpsc_queue<WindowId>` drained on
  `SDL_EVENT_USER` alongside the existing window-creation queue. `SoftwareBackend::run()` gains a
  `SDL_EVENT_WINDOW_CLOSE_REQUESTED` case (native-decoration close — didn't exist before, since only
  one window ever existed) that just forwards `WindowEvent{wid, WindowClose{}}`; the existing
  custom-chrome Close-button handler stops unilaterally calling `running_.store(false)` and does the
  same. Both changes are backward-compatible: for a single window, `wid` is always the primary id, so
  `model_app()`'s existing `if (wid == primary_id) loop.finish();` still fires immediately, just via
  one more round-trip through the app thread (`loop.finish()` → `backend.quit()` → `running_.store(
  false)`) instead of the backend thread deciding unilaterally.

### Activation

Compiled in by default, dormant until triggered — no app code required, using `AppContext`'s
primitives:

- A new meson option, `prism_debug_tools` (`auto` | `enabled` | `disabled`, default `auto` = enabled
  unless `buildtype` is `release`), gates a compile-time define `PRISM_DEBUG_TOOLS_ENABLED`.
- New constants in `namespace prism::input`: `mods::ctrl` and `keys::i` (neither exists today).
- When the define is present, `model_app()`'s setup path (inside an `#ifdef` block, entirely inside
  `model_app.hpp`) calls `ctx.set_global_key_handler(...)` with a handler that checks for
  `KeyPress{.key == keys::i, .mods == mods::ctrl | mods::shift}`. First press lazily constructs the
  debug window + `TreeInspectorController` (via `ctx.backend().request_window` +
  `ctx.registry().add` + `ctx.set_post_dispatch_hook`); a second press tears it down
  (`ctx.registry().remove(id)` + `ctx.backend().close_window(id)`). When the define is absent, none
  of this compiles — a release build with the option disabled carries zero footprint.

### Components

- **`prism::debug::NodeRow`** (new, `include/prism/widgets/debug/tree_inspector.hpp`) — flat row:
  `WidgetId id`, `std::string name`, `std::string layout_kind_name`, `int depth`, `Rect rect`,
  `bool dirty/hovered/focused/pressed`, `bool has_children/expanded`.

- **`prism::debug::flatten_tree(const WidgetTree&, const std::set<WidgetId>& expanded) ->
  std::vector<NodeRow>`** — pure depth-first walk that skips children of any node not in
  `expanded`. No rendering dependency; the primary unit-test target.

- **`prism::debug::TreeInspectorModel`** — the debug window's own model:
  `Field<List<NodeRow>> rows` (feeds `VirtualList`, using sub-project 2's `on_row_click` for
  selection), `Field<std::optional<WidgetId>> selected`.

- **`WidgetNode::debug_name`** (new field, `#ifdef PRISM_DEBUG_TOOLS_ENABLED` only — an unconditional
  `std::string` per node would be real per-node memory overhead across every tree in every build,
  unlike the tree-level `highlight_id_`) — human-readable label captured at tree-build time,
  populated in `build_node_tree`'s reflection branch using the same `has_identifier`-guarded
  `identifier_of(m)` pattern already used by `check_unplaced_fields` (`widget_tree.hpp`), falling
  back to the delegate/sentinel type name where no reflected member identifier applies.

- **`WidgetTree::set_debug_highlight`**, **`build_snapshot()`'s highlight step**, **`WidgetTree::
  hovered_id() const`** (new accessor exposing the existing private `hovered_id_`) — see
  Architecture.

- **`prism::debug::TreeInspectorController`** — owns the second `Window` + entry id, installs the
  post-dispatch hook, and drives the correlation glue in Data flow.

### Data flow

1. On every post-dispatch hook firing, the controller re-runs `flatten_tree(main_tree,
   expanded_set)` and wholesale-replaces `TreeInspectorModel::rows`. O(visible nodes) — hundreds to
   a few thousand in realistic trees — only runs while the debug window is open, and only as often
   as *some* window event already fires (not more expensive than the main tree's own per-event
   work).
2. **Debug → main:** clicking a row (via `on_row_click(index, const NodeRow&)`, sub-project 2)
   selects its `WidgetId` → `main_tree.set_debug_highlight(id)` → that id's rect is highlighted in
   the main window's overlay on its next `build_snapshot()` call (triggered by the same post-dispatch
   hook, since setting the highlight also needs to mark the main entry dirty so it actually
   republishes even if nothing else changed).
3. **Main → debug:** hovering in the main window updates `main_tree.hovered_id()` via the existing
   hover path → the controller reads it on each hook firing → sets `TreeInspectorModel::selected`
   and calls `debug_tree.scroll_to(vlist_container_id, DY{row_index * item_height.raw()})` to bring
   it into view.
4. Closing the debug window (hotkey toggle, or its native/custom close button —
   `SDL_EVENT_WINDOW_CLOSE_REQUESTED` / custom-chrome Close, both now just forward `WindowClose`)
   removes it from the registry and calls `ctx.backend().close_window(id)` for real OS-level
   teardown.

### Error handling

- The debug window is entirely opt-in/dormant; if `request_window` returns `nullptr` (a platform
  without multi-window support, or the bounded-wait timeout), log once and continue — a debugging
  tool must never be able to crash the app it is inspecting.
- `set_debug_highlight` and row lookups always re-resolve by `WidgetId` against the *current* tree's
  freshly-computed `snap->geometry` each `build_snapshot()` call — never a cached `WidgetNode*`/rect
  across frames. Dynamic content (`Table`/`Tabs`/`VirtualList` recycling) can change tree shape
  between refreshes; a stale pointer here would be a dangling-reference bug in the tool meant to help
  find such bugs elsewhere.
- If a selected/hovered id no longer resolves (node removed/recycled since last refresh), both the
  highlight injection and the debug-window selection silently clear rather than erroring.
- `on_row_click`'s callback receives a live `const NodeRow&` into the row list (sub-project 2's
  documented contract) — the click handler must not mutate `TreeInspectorModel::rows` synchronously
  from inside the callback; it only reads `value.id` and stores it, which is safe.

### Non-goals

- Dirty-region visualization, `SceneSnapshot` version history, thread-latency overlay, Tracy
  integration — separate specs.
- Editing tree state from the inspector (that's `Inspector<T>`'s job for model data; the tree
  inspector is read-only structural/visual introspection).
- Multi-app / multi-process inspection — single process only, matching the rest of PRISM's
  same-process threading model.
- Persisting expand/collapse state across app restarts.
- Generalizing `close_window`/the post-dispatch hook into more than what this feature needs (e.g. no
  multiple simultaneous post-dispatch hooks — one settable callback, same as `set_global_key_handler`).

## Testing

- **`flatten_tree()`** — pure function tests over a `WidgetTree` fixture: depth ordering,
  expand/collapse filtering, empty tree. No rendering needed.
- **Highlight round-trip** — build a tree, call `set_debug_highlight(id)`, call `build_snapshot()`,
  assert the resulting `snap->overlay` contains a `RectOutline` at that node's `snap->geometry` rect.
  Headless, no SDL.
- **Post-dispatch hook + all-dirty publish** — headless `model_app()` test (two registry entries via
  the same trivial-model pattern used in sub-project 1's own tests): assert the hook fires once per
  processed event regardless of which window the event targeted, and that dirtying a *non-event*
  entry still gets it published on the next event.
- **Window teardown** — via `TestBackend`'s synchronous `close_window` (needs a small addition
  mirroring `request_window`'s Task 4 addition from sub-project 1): assert `registry.remove` +
  `backend.close_window` together leave no trace of the closed entry, and that a subsequent event
  for its old id is a safe no-op (`registry.find` returns `nullptr`).
- **Controller integration** — two `WidgetTree` instances (main + debug) driven by the existing
  synchronous test scheduler: simulate a debug-window row click, assert the main tree's next
  `build_snapshot()` overlay reflects the highlight; simulate a main-window hover, assert the debug
  tree's selected row updates on the next post-dispatch hook firing.
- Same-thread design introduces no new TSan/sanitizer surface.
