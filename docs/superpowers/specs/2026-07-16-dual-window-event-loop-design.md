# Dual-Window Event Loop Foundation — Design Spec

**Date:** 2026-07-16 (revised during plan-writing)
**Status:** Approved (pending spec review)
**Blocks:** [Live Tree Inspector](2026-07-15-live-tree-inspector-design.md) (sub-project 1 of 3 under Phase 4.5 #5 — see that spec's "Split during plan-writing" note)

## Problem

`model_app()` (`include/prism/app/model_app.hpp`) is hard-wired to exactly one `Window`/`WidgetTree`
pair: `AppContext` holds a single `Window*`, the event-dispatch loop calls `detail::route_*`
against one hardcoded `tree`, and `WindowEvent.window` is read from the event but never used to
route anywhere (`model_app.hpp:169`). `SoftwareBackend` can already track multiple windows in its
`windows_` map, but nothing above it can drive a second one.

The tree inspector (sub-project 3) needs a second, lazily-created OS window sharing the main app's
thread and `stdexec::run_loop` — which surfaced a real threading constraint during research:
`SDL_CreateWindow` is documented main-thread-only (`SDL_video.h:1175`), window creation today only
ever happens before the SDL-owning thread (`backend_thread`) spawns, and there is no existing
mechanism for the app thread to ask the SDL thread to create a window after the loop has already
started.

## Scope

This spec covers only the generic mechanism: routing events to more than one window/tree pair
sharing one app thread, and safely creating a window after the loop has started. **It owns no
"debug tools" concept at all** — no hotkey, no build flag, no knowledge of what a second window
would show. See "Revision" below for why.

## Revision (during plan-writing)

The original version of this spec had `model_app()` itself install a Ctrl+Shift+I listener and call
`attach_secondary_window(..., Model& model)`. Working out the exact code revealed a real gap:
`attach_secondary_window` needs *some* model to build the second `WidgetTree` from, but
`model_app<Model>()` only ever has the *app's own* `Model` in scope — it has no way to know about
`TreeInspectorModel` (sub-project 3's type) without depending on sub-project 3, which would invert
the intended dependency direction (sub-project 3 depends on sub-project 1, not the other way round).

Resolution: sub-project 1 exposes generic primitives only —
`AppContext::registry()`/`AppContext::backend()` (so *something* can call `attach_secondary_window`)
and a generic `AppContext::set_global_key_handler(fn)` hook, consulted for every `KeyPress` on every
window before per-window dispatch. Sub-project 3 registers its own Ctrl+Shift+I handler through that
hook and builds its own `TreeInspectorModel` — sub-project 3 achieves "zero app code" by using these
primitives, rather than sub-project 1 promising a specific hotkey it can't correctly wire up.

A second discovery during plan-writing: `WidgetTree` is neither copyable nor movable (its copy
constructor is explicitly deleted, which suppresses the implicit move constructor too — confirmed by
the absence of any declared move constructor in `widget_tree.hpp`). This rules out a `WindowRegistry`
that stores `WidgetTree` by value in a way that requires moving one in after construction. The fix:
each registry entry owns a `std::unique_ptr<WidgetTree>` (the `unique_ptr` is movable even though the
pointee isn't) and constructs the tree in place via `std::make_unique<WidgetTree>(model)`.

A third decision from brainstorming: the dispatch loop always routes through `WindowRegistry` (a
registry holding exactly one entry when nothing has attached a second window), rather than an
`#ifdef`-gated dual code path. This trades a small, constant, per-**input-event** (not per-frame)
cost for one clean code path in a core, widely-used function — input events are user-input-rate, not
a hot path.

## Solution

### Architecture

- `model_app()` always constructs a `WindowRegistry` seeded with the main window/tree as its only
  entry at startup. The event-dispatch loop always routes through the registry instead of a
  hardcoded single `tree` — this is unconditional, not gated by any flag.
- `AppContext` gains three additions: `registry()` (returns `WindowRegistry&`), `backend()` (returns
  `Backend&` — not exposed today), and `set_global_key_handler(std::function<void(const KeyPress&)>)`
  — a single optional handler, consulted for every `KeyPress` event on every window, before that
  window's normal `detail::route_key_press` dispatch. This is the *only* new "extension point";
  everything else in this spec is internal to `model_app()`/`WindowRegistry`.
- Closing a window: `WindowClose` on the *primary* entry (the one `model_app()` created at startup)
  still calls `loop.finish()`, quitting the app, exactly as today. `WindowClose` on any other entry
  just removes it from the registry — the app keeps running. This distinction has to exist as soon
  as a second window can exist at all, or closing a secondary window would incorrectly quit the app.

### Components

- **`prism::app::detail::WindowRegistry`** (new, `include/prism/app/window_registry.hpp`):
  ```cpp
  struct Entry {
      Window* window;
      std::unique_ptr<WidgetTree> tree;
      std::shared_ptr<const SceneSnapshot> current_snap;
      int width = 0;
      int height = 0;
      uint64_t version = 0;
  };

  class WindowRegistry {
  public:
      template <typename Model>
      WindowId add(Window& window, Model& model) {
          WindowId id = window.id();
          Entry e;
          e.window = &window;
          e.tree = std::make_unique<WidgetTree>(model);
          auto [w, h] = window.size();
          if (window.decoration_mode() == DecorationMode::Custom)
              h -= static_cast<int>(WindowChrome::title_bar_h.raw());
          e.width = w;
          e.height = h;
          entries_.emplace(id, std::move(e));
          return id;
      }
      void remove(WindowId id) { entries_.erase(id); }
      [[nodiscard]] Entry* find(WindowId id) {
          auto it = entries_.find(id);
          return it != entries_.end() ? &it->second : nullptr;
      }
      template <typename Fn>
      void for_each_dirty(Fn&& fn) {
          for (auto& [id, entry] : entries_)
              if (entry.tree->any_dirty()) fn(id, entry);
      }
  private:
      std::unordered_map<WindowId, Entry> entries_;
  };
  ```
  `Entry` is movable (thanks to its `unique_ptr<WidgetTree>` member) even though `WidgetTree` itself
  is not — `unordered_map::emplace` only ever moves the `unique_ptr`, never the tree it points to.

- **`include/prism/app/event_routing.hpp`** (new — extracted from `model_app.hpp:37-124`, mechanical
  move, no behavior change): the existing `namespace prism::app::detail` free functions
  (`route_mouse_move`, `route_mouse_button`, `route_mouse_scroll`, `route_key_press`,
  `route_text_input`). Needed so `window_registry.hpp` and `model_app.hpp` can both use them without
  a circular include (`window_registry.hpp` needs `WidgetTree`; `model_app.hpp` needs
  `WindowRegistry`).

- **`BackendBase::request_window(WindowConfig) -> WindowId`** — new **virtual with a default body**
  (`{ return create_window(std::move(cfg)).id(); }`), **not pure**. Ten-plus `BackendBase`
  subclasses exist across the test suite (`test_ui.cpp`, `test_model_app.cpp`); a pure virtual would
  force every one of them to add a matching override even though none of them need it. Only
  `SoftwareBackend` (real cross-thread creation) and `TestBackend` (needs genuine multi-window
  storage for headless registry tests) override it; every other subclass inherits the default
  unchanged.

- **`SoftwareBackend::request_window`** override — the cross-thread mechanism: a new
  `mpsc_queue<std::shared_ptr<PendingWindowRequest>> window_requests_` member
  (`PendingWindowRequest{WindowConfig cfg; WindowId result_id = 0; bool done = false; std::mutex m;
  std::condition_variable cv;}`). `request_window` pushes a request, calls `wake()`, then
  `cv.wait_for(lock, 2s, [&]{ return req->done; })` — a *bounded* wait (not indefinite), returning
  the sentinel `0` on timeout as well as on explicit failure. The `SDL_EVENT_USER` case in `run()`
  (currently a no-op, `software_backend.cpp:201-202`) drains the queue and, for each request still
  live (`running_.load()`), performs the same steps already done for the initial window set at
  startup (`ensure_created()` + `SDL_StartTextInput`, `software_backend.cpp:59-62`) before signaling
  `done`. The same drain also runs once more right after `run()`'s main loop exits, so any request
  that arrives exactly at shutdown is resolved to failure (`0`) rather than left to time out.

- **`TestBackend::request_window`** override + storage change: `HeadlessWindow window_{0, {}}`
  becomes `std::unordered_map<WindowId, HeadlessWindow> windows_` plus a `WindowId primary_id_ = 0`
  (set by `create_window`, used by `run()` exactly as `window_.id()` was — preserves existing test
  behavior byte-for-byte since every current test calls `create_window` exactly once). Both
  `create_window` and `request_window` insert into `windows_`, synchronously (`TestBackend` has no
  real background thread, so there's no threading concern to solve here — just storage).

### Data flow

1. App thread runs `model_app()`'s existing startup (create main window, `registry.add(window,
   model)`).
2. SDL thread pumps events; each `WindowEvent` hops to the app thread via the existing
   `schedule|then` mechanism (unchanged), which now looks up `registry.find(we.window)` and, if a
   `KeyPress` and a global key handler is set, calls it first, then routes to that entry's tree via
   the (now-shared) `detail::route_*` functions using *that entry's own* `current_snap` for
   hit-testing.
3. The submit pass at the end of each processed event calls `for_each_dirty`, building and
   submitting a snapshot per dirty entry — previously always exactly one tree, now one or more.
4. If some future caller (sub-project 3) calls `ctx.registry().add(...)` after obtaining a `WindowId`
   from `ctx.backend().request_window(cfg)`, the new entry starts receiving routed events on the very
   next `WindowEvent` that names its id — no other change needed, since routing was already
   registry-driven from step 2.

### Error handling

- `request_window` failure (a platform that can't open a second window, or a shutdown race) returns
  the sentinel invalid id `0` (matching the existing "unassigned id" convention, e.g. `WidgetId
  next_id_ = 1` in `widget_tree.hpp:701`) rather than throwing. Callers must check for `0` before
  calling `registry.add(...)`.
- The bounded (2s) wait in `SoftwareBackend::request_window` means a caller can never hang forever,
  even in an edge case not otherwise anticipated (e.g. the backend thread having already exited its
  loop for an unrelated reason at the exact moment of the request).
- `WindowRegistry::find` returns `nullptr` for an unknown id — every call site checks for `nullptr`
  before dereferencing; a stale `WindowEvent.window` (e.g. an OS event arriving for a window already
  removed from the registry) must never crash the app.

### Non-goals

- No hotkey, no `mods::ctrl`/letter-key constants, no build flag, no "debug tools" concept of any
  kind — all of that moved to sub-project 3, which is the one that actually knows what a second
  window should show. This spec ends at "here is how you'd safely add and route a second window if
  you had something to put in it."
- No public multi-window API on `model_app()` beyond the three `AppContext` additions.
- No support for more than 2 windows in *this* spec's own testing, though `WindowRegistry` itself is
  a generic `unordered_map` with no structural limit.
- No persistence of any secondary window's position/size across app restarts.
- No headless-CI coverage of the real cross-thread SDL creation path (see Testing) — accepted gap,
  not silently glossed over.

## Testing

- **`WindowRegistry`** — pure unit tests: register two trivial models (e.g. two simple
  `struct Counter { Field<int> value{0}; };` instances) under two ids, dispatch synthetic
  `WindowEvent`s for each id, assert routing hits only the matching tree's dirty state. Headless, no
  backend — same `headless_tests` meson group as `test_widget_tree.cpp` (`tests/meson.build:16-53`).
- **`request_window` via `TestBackend`** — synchronous path: request a second window, assert it gets
  a distinct id from the first and that both are independently reachable. Proves the
  registry/attach-flow logic end-to-end without the real SDL threading dance (see accepted gap
  above for why the real cross-thread path isn't headless-testable).
- **`set_global_key_handler`** — synthesize a `KeyPress`, assert the registered handler fires before
  the normal per-window dispatch, and that when no handler is set, behavior is identical to today.
- **Secondary-window close doesn't quit the app** — attach a second entry, synthesize `WindowClose`
  for its id, assert the registry entry is removed and `loop.finish()` was *not* called; synthesize
  `WindowClose` for the primary id, assert `loop.finish()` *was* called.
- **Shutdown race** — a dedicated test that calls `quit()` concurrently with a pending
  `request_window` call (via `TestBackend`'s synchronous path plus a manually-set state) and asserts
  the call returns `0` rather than hanging, bounded with a test timeout.
