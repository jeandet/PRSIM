# Text Field Widget Design

## Overview

Add a single-line editable text field to PRISM's delegate system. The text field is activated via the `TextField<T>` sentinel type, receives character input through a new `TextInput` event (forwarding SDL's text input), and stores ephemeral cursor state in a new `std::any` slot on `WidgetNode`.

## Decisions

- **Text input:** New `TextInput { std::string text }` event forwarding `SDL_TextInputEvent` (proper Unicode/IME support).
- **Text measurement:** Monospace approximation (`0.6 * font_size`) for cursor positioning. Structured so real `TextMetrics` can replace it later as a localized change.
- **Cursor state:** Ephemeral `std::any edit_state` on `WidgetNode`. Not part of the model — cursor position is UI state, not application data.
- **Sentinel required:** `Field<TextField<>>` opts into editing. `Field<std::string>` stays read-only.
- **Editing scope (v1):** Character insert, backspace, delete, left/right arrow, Home/End, click-to-position cursor. No selection, clipboard, or undo.

## New Types

### `TextField<T>` sentinel (delegate.hpp)

```cpp
template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};  // grayed text when empty
    size_t max_length = 0;      // 0 = unlimited
};
```

### `TextInput` event (input_event.hpp)

```cpp
struct TextInput { std::string text; };  // UTF-8 from SDL_TextInputEvent
```

Added to the `InputEvent` variant.

### Key constants (input_event.hpp)

```cpp
namespace keys {
    inline constexpr int32_t backspace = 0x08;       // SDLK_BACKSPACE
    inline constexpr int32_t delete_   = 0x7F;       // SDLK_DELETE
    inline constexpr int32_t right     = 0x4000'004F; // SDLK_RIGHT
    inline constexpr int32_t left      = 0x4000'0050; // SDLK_LEFT
    inline constexpr int32_t home      = 0x4000'004A; // SDLK_HOME
    inline constexpr int32_t end       = 0x4000'004D; // SDLK_END
}
```

Values match SDL scancodes. Core headers remain SDL-free.

### `TextEditState` (delegate.hpp, internal)

```cpp
struct TextEditState {
    size_t cursor = 0;
    float scroll_offset = 0.f;  // horizontal scroll in pixels
};
```

### `TextEditable` concept (delegate.hpp)

```cpp
template <typename T>
concept TextEditable = requires(const T& t) {
    { t.value } -> StringLike;
};
```

### Monospace helper (delegate.hpp)

```cpp
inline float char_width(float font_size) { return 0.6f * font_size; }
```

Single replacement point for future real `TextMetrics`.

## Delegate Signature Change

Both `record()` and `handle_input()` change to receive `WidgetNode&` instead of `WidgetVisualState&`:

```cpp
// Before
static void record(DrawList&, const Field<T>&, const WidgetVisualState&);
static void handle_input(Field<T>&, const InputEvent&, WidgetVisualState&);

// After
static void record(DrawList&, const Field<T>&, const WidgetNode&);
static void handle_input(Field<T>&, const InputEvent&, WidgetNode&);
```

All existing delegates updated mechanically — access `node.visual_state` where they previously used `vs`.

This gives delegates access to `node.edit_state` for ephemeral UI state, and to any future node data without further signature changes.

## Delegate<TextField<T>> Behavior

### record()

1. Background rect (white; darker on hover; blue border on focus)
2. `ClipPush` to inner text area (with padding)
3. If empty and unfocused: render `placeholder` in gray
4. Otherwise: render `value` offset by `-scroll_offset`
5. If focused: render cursor as 2px-wide filled rect at `cursor * char_width - scroll_offset`
6. `ClipPop`

### handle_input()

- **`TextInput`:** Insert `text` at cursor position, advance cursor. Clamp to `max_length` if set.
- **`KeyPress`:**
  - Backspace: erase char before cursor, move cursor left
  - Delete: erase char at cursor
  - Left/Right: move cursor ±1 (clamped)
  - Home/End: cursor to 0 / `value.size()`
- **`MouseButton` (pressed):** Compute cursor position from click x: `(click_x - text_origin + scroll_offset) / char_width`, clamped to `[0, value.size()]`

After any mutation: `field.set()` with new value, update `TextEditState` in `node.edit_state`, adjust `scroll_offset` to keep cursor visible within clipped area.

## WidgetNode Change

One new field:

```cpp
std::any edit_state;
```

No other structural changes. `build_leaf` passes `node` to delegates instead of `node.visual_state`.

## Backend Changes

### software_backend.cpp

- Call `SDL_StartTextInput(window)` after window creation (enables `SDL_EVENT_TEXT_INPUT`)
- Map `SDL_EVENT_TEXT_INPUT` → `TextInput { event.text.text }`, schedule on app thread

### model_app.hpp

- Forward `TextInput` events to the focused widget via `tree.dispatch(focused_id, ev)`
- `KeyPress` for non-character keys (arrows, backspace, delete, home, end) continues to be forwarded as before

## Future TextMetrics Upgrade Path

Replace `char_width(float)` with a `TextMetrics` interface. Since delegates already receive `WidgetNode&`, the metrics can be provided through the node (or a context object on the node) without any delegate signature changes. The change is localized to:

1. Add `TextMetrics*` or similar to `WidgetNode` (or a context it carries)
2. Replace `char_width(font_size)` calls in `Delegate<TextField<T>>` with `metrics->measure(text, font_size).width`

No other delegates are affected.

## Testing

### Unit tests (test_text_field.cpp)

1. **Cursor logic:** Insert/delete/move at boundaries (cursor at 0, at end, empty string)
2. **record() output:** Verify DrawList commands — background rect, clipped text at correct offset, cursor rect when focused
3. **handle_input():** Synthetic `TextInput`, `KeyPress`, `MouseButton` → verify field value and cursor position
4. **max_length:** Insert past limit → verify truncation
5. **Scroll offset:** Type past widget width → verify scroll_offset keeps cursor visible
6. **Placeholder:** Gray text when empty+unfocused, hidden when focused or non-empty

### Integration test

7. **Full round-trip:** Model with `Field<TextField<>>`, build WidgetTree, Tab to focus, dispatch `TextInput`, verify field value and snapshot update

### Existing tests

8. **Delegate signature update:** Mechanical change from `WidgetVisualState&` to `WidgetNode&` in all existing delegate tests

## Files Changed

| File | Change |
|------|--------|
| `input_event.hpp` | `TextInput` struct, variant member, key constants |
| `delegate.hpp` | `TextField<T>`, `TextEditState`, `TextEditable`, `char_width()`, `Delegate<TextField<T>>`, signature change for all delegates |
| `widget_tree.hpp` | `std::any edit_state` on `WidgetNode`, `build_leaf` passes `node` to delegates |
| `model_app.hpp` | Forward `TextInput` to focused widget |
| `software_backend.cpp` | `SDL_StartTextInput()`, forward `SDL_EVENT_TEXT_INPUT` |
| `tests/` | New `test_text_field.cpp`, update existing delegate tests |
| `examples/model_dashboard.cpp` | Add `Field<TextField<>>` demo |

## Not Changed

DrawList, SceneSnapshot, layout, BackendBase vtable, focus management logic.
