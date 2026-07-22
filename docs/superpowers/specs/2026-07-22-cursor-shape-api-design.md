# Cursor Shape API — Design Spec

## Summary

Add a small, backend-agnostic `CursorShape` enum and a `Window::set_cursor(CursorShape)` virtual so widgets and window chrome can change the OS mouse cursor (I-beam over text fields, resize arrows over a split-pane `Handle` or a custom-chrome window edge). Two independent resolvers feed the same `Window::set_cursor()` call — one for widget content (`WidgetTree`, main thread), one for window chrome (`SoftwareBackend`, backend thread) — split by mouse position so they never contend for the same frame.

## Motivation

No cursor-shape abstraction exists today; every "cursor" hit in the codebase is the text-entry caret, not the OS pointer. This was called out as explicit out-of-scope in the resizable-split-panes spec ("no OS cursor abstraction exists yet"). Now that `Handle` (draggable divider) and text-entry widgets both have an obvious, common cursor need, and the project intends to target additional backends (headless/test today, Wasm later per the roadmap), the abstraction should live on the existing `Window` interface rather than be bolted directly onto SDL.

## Data Types

### `CursorShape` (new header)

`include/prism/ui/cursor.hpp` — zero dependencies, so both `prism::ui` (delegates) and `prism::app`/`prism::backends` (Window, chrome) can depend on it without a layering inversion:

```cpp
namespace prism::ui {
enum class CursorShape : uint8_t { Default, Text, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE };
}
```

Six values, exactly covering the two known widget-level consumers (`Text`, `ResizeNS`/`ResizeEW` for `Handle`) plus the four window-chrome resize directions (edges collapse to `ResizeNS`/`ResizeEW`, corners to `ResizeNESW`/`ResizeNWSE`). Not meant to be exhaustive — e.g. `Pointer`/`Move`/`NotAllowed` have no consumer yet and are left out; adding one later is a one-line enum addition plus one row in the SDL mapping table, not a redesign.

### `WidgetVisualState` (extended)

`include/prism/ui/delegate.hpp`, alongside the existing `hovered`/`pressed`/`focused`:

```cpp
struct WidgetVisualState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    CursorShape cursor = CursorShape::Default;
};
```

This is a **static, per-node property**, not something recomputed every frame from hover state — a node declares what cursor it wants *when* hovered/captured, once, when it's built or first recorded. `WidgetTree` is what decides *whether* that declared cursor is currently active (see below).

## Component Integration

### `Handle` (`include/prism/app/widget_tree.hpp`)

`wire_split_handles` already computes `vertical` (true when the parent is a `Column`, i.e. the divider drags along Y) before building each handle child's `build_widget` closure. One line added to that closure:

```cpp
child.build_widget = [&tree = tree_, container_id, index, vertical](WidgetNode& wn) {
    wn.focus_policy = FocusPolicy::none;
    wn.visual_state.cursor = vertical ? CursorShape::ResizeNS : CursorShape::ResizeEW;
    wn.record = &draw_divider;
    ...
```

The bare, unwired `handle()` (used outside a Row/Column split, per its own comment) has no known axis and is left at `CursorShape::Default` — no special-casing needed, it's simply an unset field.

### `TextField` / `Password` / `TextArea` (`include/prism/delegates/text_delegates.hpp`)

These are `Widget<Sentinel>` specializations dispatched by type, not built via a `build_widget` closure like `Handle`. Their `record()` methods already take a **mutable** `WidgetNode&` (the const-node `detail::text_field_record`/`text_area_record` helpers they delegate to are called *from* `record()`, not the other way round), so the cursor is set at the top of each, before delegating:

```cpp
template <StringLike T>
void Widget<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field, WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    detail::text_field_record(dl, field, node, [](const std::string& v) { return v; });
}
```

Same one-line addition in `Widget<Password<T>>::record` and `Widget<TextArea<T>>::record`. Reassigning the same value every repaint is harmless (idempotent) and needs no new machinery.

### `WidgetTree::desired_cursor()` (new)

`include/prism/app/widget_tree.hpp`, alongside `hovered_id()`/`captured_id()`:

```cpp
[[nodiscard]] CursorShape desired_cursor() const {
    WidgetId active = captured_id_ != 0 ? captured_id_ : hovered_id_;
    if (active == 0) return CursorShape::Default;
    auto it = index_.find(active);
    return it != index_.end() ? it->second->visual_state.cursor : CursorShape::Default;
}
```

`captured_id_` takes priority over `hovered_id_` because it's already how the codebase expresses "actively pressed/dragging" (e.g. `Handle`'s own `MouseButton` handler causes `route_mouse_button` to call `set_pressed(id, true)`, which sets `captured_id_ = id` generically — no `Handle`-specific capture logic needed). This keeps the resize cursor active for the whole drag even if the mouse outruns the handle's thin hit-rect mid-drag.

### `Window::set_cursor` (new pure virtual)

`include/prism/app/window.hpp` — added alongside the other per-window OS operations (`set_title`, `minimize`, etc.), same style, `#include <prism/ui/cursor.hpp>` added:

```cpp
virtual void set_cursor(CursorShape shape) = 0;
```

Two implementations exist today:

- **`SdlWindow`** (`src/backends/sdl_window.cpp`): a small static `CursorShape → SDL_SystemCursor` table, lazily caching one `SDL_Cursor*` per shape via `SDL_CreateSystemCursor` (cursors are process-global in SDL; caching once and reusing avoids redundant creation across multiple windows), then `SDL_SetCursor`. This follows the *existing* convention in this file — `set_title`/`set_size`/`minimize`/etc. already call raw `SDL_*` functions directly regardless of which thread invokes them (there is no marshaling/queue for these). `set_cursor` does the same; no new threading primitive is introduced.
- **`HeadlessWindow`** (`include/prism/app/window_registry`-adjacent, `include/prism/app/headless_window.hpp`): stores the shape (`cursor_` member) and exposes `CursorShape cursor() const`, giving `TestBackend`-driven tests a directly assertable side effect.

### `model_app` wiring (`include/prism/app/model_app.hpp`)

`WindowRegistry::Entry` gains `CursorShape last_cursor = CursorShape::Default;` (`include/prism/app/window_registry.hpp`). After the existing mouse-event dispatch block (`route_mouse_move`/`route_mouse_button`/`route_mouse_scroll`) in the `WindowEvent` handler:

```cpp
if (auto shape = entry->tree->desired_cursor(); shape != entry->last_cursor) {
    entry->last_cursor = shape;
    entry->window->set_cursor(shape);
}
```

One call site, covers every input event that could plausibly change hover or capture.

## Window Chrome Integration (`WindowChrome::HitZone` → cursor)

This is the part that needs care: chrome resize-edge/corner hit-testing happens **synchronously on the backend thread**, directly against raw SDL coordinates, entirely before any `WidgetTree` involvement (see `SoftwareBackend::run()`'s `SDL_EVENT_MOUSE_MOTION` case). Widget-content cursor resolution happens **asynchronously on the main thread**, via `model_app`'s scheduled event handling. If both independently called `Window::set_cursor()` for overlapping screen positions (the 6px resize border overlaps client-area coordinates near every edge), whichever fires last for a given motion event would win — in practice the widget layer always runs after the chrome check for the same SDL event (since it's deferred through the scheduler), so a naive implementation would make the chrome resize cursor never stick, or flicker.

The fix avoids the race by construction instead of coordinating between threads: **the two layers own mutually exclusive screen regions, and only one of them ever sees a given motion event.**

- **`WindowChrome::cursor_for(HitZone) -> CursorShape`** (new static, `include/prism/ui/window_chrome.hpp`, alongside the existing `hit_test`/`render` statics — needs `#include <prism/ui/cursor.hpp>`):

  | HitZone | CursorShape |
  |---|---|
  | `ResizeN`, `ResizeS` | `ResizeNS` |
  | `ResizeE`, `ResizeW` | `ResizeEW` |
  | `ResizeNW`, `ResizeSE` | `ResizeNWSE` |
  | `ResizeNE`, `ResizeSW` | `ResizeNESW` |
  | `TitleBar`, `Close`, `Minimize`, `Maximize`, `Client` | `Default` |

- **`SoftwareBackend::run()`**, `SDL_EVENT_MOUSE_MOTION` case, inside the existing `DecorationMode::Custom` branch, before the current unconditional `event_cb(WindowEvent{wid, MouseMove{...}})`: hit-test the raw (non-offset) position against `WindowChrome::hit_test`. If the zone isn't `Client` and the window isn't already mid-resize-drag (`in_resize()`), call `it_w->second->set_cursor(WindowChrome::cursor_for(zone))` and `break` — **do not** forward the event to the app.

  This changes no observable app-facing behavior: today, hovering the title bar or edge strip already produces a `MouseMove` that the app forwards but that hits nothing (its coordinates fall outside any widget's rect, since content starts at local Y 0 which is *below* the title bar). The only change is that the app no longer receives a pointless event for a position it was never going to react to — and in exchange, that motion event becomes the single, uncontested owner of the cursor for that position.

  When the mouse re-enters the client area, the very next motion event resumes forwarding to the app, and `desired_cursor()` reclaims the cursor on that event — no explicit "reset" needed.

  Native-decoration windows are unaffected (the whole check is nested inside the existing `Custom` branch) — the OS already supplies edge-resize cursors for native chrome.

  Mid-drag (`in_resize()` true): the cursor was already set by the hover that preceded the mouse-down, and the zone can't change mid-drag, so `update_resize`/`end_resize` need no cursor logic of their own.

## What Changes

| File | Change |
|---|---|
| `include/prism/ui/cursor.hpp` | **New.** `CursorShape` enum |
| `include/prism/ui/delegate.hpp` | `WidgetVisualState::cursor` field |
| `include/prism/ui/window_chrome.hpp` | `WindowChrome::cursor_for(HitZone)` static |
| `include/prism/app/widget_tree.hpp` | `wire_split_handles`'s `build_widget` sets `visual_state.cursor`; new `WidgetTree::desired_cursor()` |
| `include/prism/delegates/text_delegates.hpp` | `Widget<TextField<T>>::record`, `Widget<Password<T>>::record`, `Widget<TextArea<T>>::record` each set `node.visual_state.cursor = CursorShape::Text` |
| `include/prism/app/window.hpp` | `Window::set_cursor(CursorShape)` pure virtual |
| `include/prism/app/headless_window.hpp` | `HeadlessWindow::set_cursor`/`cursor()` |
| `include/prism/backends/sdl_window.hpp`, `src/backends/sdl_window.cpp` | `SdlWindow::set_cursor`; static shape→`SDL_SystemCursor` table with lazily-cached `SDL_Cursor*` |
| `include/prism/app/window_registry.hpp` | `WindowRegistry::Entry::last_cursor` |
| `include/prism/app/model_app.hpp` | push `desired_cursor()` to `Window::set_cursor()` on change, after the mouse-event dispatch block |
| `src/backends/software_backend.cpp` | `SDL_EVENT_MOUSE_MOTION` case: chrome hit-test before forwarding; non-`Client` zones set cursor and stop forwarding instead of always forwarding |
| `tests/test_cursor.cpp` | **New** |
| `tests/meson.build` | Add `test_cursor.cpp` |

## What Stays Unchanged

- `begin_resize`/`update_resize`/`end_resize` themselves — no cursor logic added to them, the motion-event hit-test handles it
- `Field<T>`, `ScrollArea`, `SplitState`/split-drag machinery
- Click handling in `SDL_EVENT_MOUSE_BUTTON_DOWN`/`UP` (chrome click zones already `break` without forwarding; unaffected)
- `NullBackend`/`CapturingBackend`/`TestBackend` (`HeadlessWindow` gains the new virtual override, nothing else about them changes)

## Testing

`tests/test_cursor.cpp`, driving `WidgetTree` directly (build a tree, feed it via `route_mouse_move`/`route_mouse_button` against a `SceneSnapshot`, matching the project's "drive real hit_test/route_mouse_button" testing convention):

1. Hovering a `TextField` → `desired_cursor() == Text`
2. Hovering a `Handle` inside an `hstack` → `ResizeEW`; inside a `vstack` → `ResizeNS`
3. Pressing (capturing) a `Handle` then moving the mouse off its hit-rect while still pressed → cursor stays the resize shape (captured-priority)
4. Releasing the drag off the handle → cursor updates to whatever is now hovered (or `Default`)
5. Hovering a plain container (Row/Column/Spacer) → `Default`
6. A bare, unwired `handle()` (no parent split container) → `Default`

`HeadlessWindow::cursor()` is exercised via a `TestBackend`-driven `model_app` test to confirm the `desired_cursor()` → `Window::set_cursor()` wiring itself, not just the pure resolution logic.

Chrome-edge cursor logic (`WindowChrome::cursor_for`, the `SoftwareBackend::run()` motion-event branch) has no automated test seam — chrome hit-testing and resize only run inside `SoftwareBackend::run()`'s real SDL event loop, which `TestBackend`/`NullBackend` never execute. This is a pre-existing gap (`begin_resize`/`update_resize` have no coverage today either), not one introduced by this change. `WindowChrome::cursor_for` itself, as a pure `HitZone → CursorShape` function, is trivially unit-testable in isolation even though the SDL wiring around it isn't.

## Out of Scope

- `Pointer`/`Move`/`NotAllowed`/`Wait`/`Crosshair` shapes — no current consumer (e.g. no hyperlink-style or drag-and-drop widget exists yet); adding one is a one-line enum change plus one SDL table row when a real consumer shows up
- Custom-drawn (software-rendered) cursors — this is strictly OS/backend cursor delegation, matching the existing SDL-native-rendering approach
- Per-widget user-overridable cursor for custom `Widget<T>` types beyond "set `visual_state.cursor` yourself" — that capability falls out of the design for free (it's just a public field) but no new sentinel/API surface is added to make it more ergonomic
- A Wasm/browser backend implementation of `Window::set_cursor` — out of scope until that backend exists, but the interface is shaped (per-`Window`, not global) so a canvas-per-window browser backend can implement it via `style.cursor` without redesigning the abstraction
