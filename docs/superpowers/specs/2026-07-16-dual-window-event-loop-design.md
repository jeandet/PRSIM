# Dual-Window Event Loop Foundation — Design Spec

**Date:** 2026-07-16
**Status:** Approved (pending spec review)
**Blocks:** [Live Tree Inspector](2026-07-15-live-tree-inspector-design.md) (sub-project 1 of 3 under Phase 4.5 #5 — see that spec's "Split during plan-writing" note)

## Problem

`model_app()` (`include/prism/app/model_app.hpp`) is hard-wired to exactly one `Window`/`WidgetTree`
pair: `AppContext` holds a single `Window*`, the event-dispatch loop calls `detail::route_*`
against one hardcoded `tree`, and `WindowEvent.window` is read from the event but never used to
route anywhere (`model_app.hpp:169`). `SoftwareBackend` can already track multiple windows in its
`windows_` map, but nothing above it can drive a second one.

The tree inspector needs a second, lazily-created OS window sharing the main app's thread and
`stdexec::run_loop`, opened automatically on a hotkey with zero app code — which surfaced a real
threading constraint during research: `SDL_CreateWindow` is documented main-thread-only
(`SDL_video.h:1175`), window creation today only ever happens before the SDL-owning thread
(`backend_thread`) spawns, and there is no existing mechanism for the app thread to ask the SDL
thread to create a window after the loop has already started.

## Scope

This spec covers only the generic mechanism: routing events to more than one window/tree pair
sharing one app thread, and safely creating a window after the loop has started. It does not cover
anything about *what* the second window shows — that's the tree inspector's job
(sub-project 3). It also does not generalize `model_app()` into a public multi-window API — see
"Non-goals."

## Solution

### Architecture

- `model_app()`'s public signature and behavior are byte-identical when `PRISM_DEBUG_TOOLS_ENABLED`
  is not defined — this feature adds no cost or risk to a build without the flag.
- When defined, `model_app()` internally constructs a `WindowRegistry` seeded with the main
  window/tree as entry 0. The dispatch loop consults the registry (1 or 2 entries) instead of a
  hardcoded single `tree`.
- A hotkey listener (Ctrl+Shift+I) sits alongside the existing Tab/Shift+Tab key dispatch
  (`model_app.hpp:104-117` is the reference pattern). On trigger it calls
  `attach_secondary_window`, which drives a cross-thread window-creation request into
  `SoftwareBackend` via a new lock-free MPSC queue (reusing the Phase 1 primitive — see
  `project-architecture` memory) plus the existing `wake()`/`SDL_PushEvent` mechanism
  (`software_backend.cpp:226-230`), blocking briefly on the app thread until the SDL thread signals
  the window is live — mirroring the existing `wait_ready()` pattern already used at startup
  (`software_backend.cpp:232-234`).
- A second hotkey press removes the entry from the registry and calls `Window::close()`.

### Components

- **`BackendBase::request_window(WindowConfig) -> WindowId`** (new pure virtual) — blocking,
  callable from any thread. `SoftwareBackend`'s implementation does the real cross-thread
  MPSC-queue-and-wake dance. `NullBackend`/`TestBackend` implement it as a plain synchronous call —
  no real thread boundary to cross there. `TestBackend` is currently hardcoded to one
  `HeadlessWindow window_{0, {}}` member (`test_backend.hpp:9-31`); it needs to become a small
  id-keyed collection (mirroring `SoftwareBackend::windows_`) so a second `request_window` call
  means something in headless tests.

- **`prism::app::detail::WindowRegistry`** (new, `include/prism/app/window_registry.hpp`) —
  ```cpp
  struct Entry { Window* window; WidgetTree tree; };
  class WindowRegistry {
  public:
      WindowId add(Window& window, WidgetTree tree);           // returns the new entry's id
      void remove(WindowId id);
      void dispatch(const WindowEvent& we);                     // routes to the matching entry
      void for_each_dirty(std::invocable<WidgetId, WidgetTree&> auto&& fn);
  private:
      std::unordered_map<WindowId, Entry> entries_;
  };
  ```
  `dispatch` looks up `we.window` via `find` (never `at`/`operator[]`) and forwards `we.event` to
  the existing free functions in `namespace prism::app::detail`
  (`route_mouse_move`/`route_mouse_button`/`route_mouse_scroll`/`route_key_press`/
  `route_text_input`, `model_app.hpp:39-122`) against that entry's tree — reused as-is, not
  duplicated.

- **`prism::debug::attach_secondary_window(WindowRegistry&, Backend&, WindowConfig, Model&) ->
  WindowId`** — calls `backend.request_window(cfg)`, builds a `WidgetTree` for `Model&` via the
  existing `WidgetTree::build_node_tree`, registers the pair via `WindowRegistry::add`.

- **`namespace prism::input::mods`**: add `inline constexpr uint16_t ctrl = 0x00C0;` (matching
  `SDL_KMOD_CTRL`, `SDL_keycode.h:332,333,342`) alongside the existing `shift = 0x0003`
  (`input_event.hpp:45-47`).

- **`namespace prism::input::keys`**: add at least `inline constexpr int32_t i = 0x69;` (matching
  `SDLK_I`) — no letter keys exist today (`input_event.hpp:28-43` only has
  tab/space/enter/backspace/delete/arrows/home/end/escape/page_up/page_down).

- **`meson_options.txt`** (new file — none exists in this repo yet):
  ```meson
  option('prism_debug_tools', type : 'feature', value : 'auto',
         description : 'Compile in the live debug-tools hotkey (Ctrl+Shift+I)')
  ```
  Threaded into a `PRISM_DEBUG_TOOLS_ENABLED` compile define in `meson.build`, `auto` meaning
  enabled unless `buildtype` is `release`.

### Data flow

1. App thread runs `model_app()`'s existing startup (create main window + tree) exactly as today.
2. If the flag is enabled: `WindowRegistry` is seeded with entry 0 = main window/tree; the hotkey
   listener is installed alongside the existing key dispatch.
3. SDL thread pumps events; each `WindowEvent` hops to the app thread via the existing
   `schedule|then` mechanism (unchanged), which now calls `registry.dispatch(we)` instead of
   calling `detail::route_*(tree, ...)` directly.
4. Ctrl+Shift+I detected in the main tree's key dispatch → app thread calls
   `attach_secondary_window(registry, backend, cfg, second_model)` → pushes an MPSC request +
   `backend.wake()` → blocks on a condition variable → SDL thread's `SDL_EVENT_USER` branch
   (currently a no-op at `software_backend.cpp:201-202`) drains the queue, calls `ensure_created()`
   + `SDL_StartTextInput` for the new window (the same steps already done for the initial window set
   at startup, `software_backend.cpp:59-62`, just triggered later), signals the condition variable →
   app thread unblocks, registry gains entry 1.
5. The submit pass at the end of each processed event iterates the registry's dirty trees via
   `for_each_dirty`, building and submitting a snapshot per window id — previously always exactly
   one tree, now one or two.

### Error handling

- `request_window` failure (a platform that can't open a second window) returns the sentinel
  invalid id `0` (matching the existing "unassigned id" convention, e.g. `WidgetId next_id_ = 1` in
  `widget_tree.hpp:701`) rather than throwing. `attach_secondary_window` checks for `0` and logs
  once + no-ops rather than registering a bad entry.
- A `request_window` call racing shutdown (`backend.quit()`) must not hang forever: the
  condition-variable wait predicate also checks a shared `quitting_` atomic flag set by `quit()`,
  returning the invalid sentinel immediately instead of blocking indefinitely.
- `WindowRegistry::dispatch` always uses `find`, never `at`/`operator[]` — a stale
  `WindowEvent.window` (e.g. an OS event arriving for a window already removed from the registry)
  must never crash the app.

### Non-goals

- No public multi-window API on `model_app()`. The `WindowRegistry`/`request_window` mechanism is
  written generically enough to support one later (see the tree-inspector spec's brainstorming
  discussion), but nothing here exposes it publicly beyond the narrow `prism::debug` entry point.
- No support for more than 2 windows in this iteration — `WindowRegistry` is a generic
  `unordered_map`, so nothing structurally prevents a 3rd, but `attach_secondary_window`'s hotkey
  wiring only ever attaches/detaches one.
- No persistence of the debug window's position/size across app restarts.
- No headless-CI coverage of the real cross-thread SDL creation path (see Testing) — accepted gap,
  not silently glossed over.

## Testing

- **`WindowRegistry`** — pure unit tests: register two trivial models (e.g. two simple
  `struct Counter { Field<int> value{0}; };` instances) under two ids, dispatch synthetic
  `WindowEvent`s for each id, assert routing hits only the matching tree's dirty state. Headless, no
  backend — same `headless_tests` meson group as `test_widget_tree.cpp`
  (`tests/meson.build:16-53`).
- **Cross-thread `request_window`** — only meaningful against `SoftwareBackend`, which needs a real
  SDL video driver; not headless-CI-testable (see accepted gap above). Headless coverage instead
  exercises `TestBackend`'s synchronous `request_window` implementation once it supports a second
  window, proving the registry/attach-flow logic end-to-end without the real SDL threading dance.
- **Hotkey detection** — synthesize `KeyPress{.key = keys::i, .mods = mods::ctrl | mods::shift}`
  and assert the attach path is invoked via an injected test seam (a `std::function` the test
  substitutes for `attach_secondary_window`), no real window needed.
- **Shutdown race** — a dedicated test that calls `quit()` concurrently with a pending
  `request_window` call (via `TestBackend`'s synchronous path plus a manually-set `quitting_` flag)
  and asserts the call returns `0` rather than hanging, bounded with a test timeout.
