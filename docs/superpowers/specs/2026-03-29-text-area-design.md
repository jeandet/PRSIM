# TextArea<T> Sentinel — Design Spec

## Overview

`TextArea<T>` is a multiline text editor sentinel type for PRISM's MVB architecture. It follows the same patterns as `TextField<T>` and `Password<T>` — a templated sentinel with a dedicated `Delegate<TextArea<T>>` specialization — but with multiline editing, character wrapping, and vertical scrolling.

## Sentinel Type

```cpp
template <StringLike T = std::string>
struct TextArea {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;   // 0 = unlimited
    size_t rows = 6;         // visible lines, determines widget height
    bool operator==(const TextArea&) const = default;
};
```

- Templated with default `std::string`, same as TextField/Password
- `rows` controls widget height: `widget_h = padding * 2 + rows * line_height`
- `max_length` constrains total character count (0 = unlimited)
- `placeholder` shown when empty and unfocused

## Delegate Constants

```cpp
template <StringLike T>
struct Delegate<TextArea<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float line_height = font_size * 1.4f;  // 19.6px
    static constexpr float cursor_w = 2.f;
    // widget_h is dynamic: padding * 2 + field.get().rows * line_height
};
```

## Edit State

```cpp
struct TextAreaEditState {
    size_t cursor = 0;       // byte position in full string
    float scroll_y = 0.f;    // vertical scroll offset in pixels
};
```

Stored in `WidgetNode::edit_state` (std::any), lazily initialized on first input. No horizontal scroll — character wrapping eliminates horizontal overflow.

## Text Wrapping Model

Wrapping is computed on demand, not cached in edit state. The value string is the single source of truth.

### wrap_lines

```cpp
namespace detail {

struct LineSpan {
    size_t start;   // byte offset into string
    size_t length;  // byte count
};

auto wrap_lines(std::string_view text, float text_area_w, float char_w)
    -> std::vector<LineSpan>;

}
```

Algorithm:
1. Walk the string, splitting on `\n` to get logical lines
2. For each logical line, split into wrapped lines of `max_chars = floor(text_area_w / char_w)` characters
3. Empty logical lines (from consecutive `\n` or trailing `\n`) produce a single zero-length `LineSpan`
4. Empty string produces one zero-length `LineSpan` (there's always at least one line)

Character wrap — break at any character boundary when the line exceeds widget width. No word-wrap logic in this phase.

### Cursor helpers

```cpp
namespace detail {

struct LineCol {
    size_t line;   // index into wrapped lines
    size_t col;    // character offset within that wrapped line
};

auto cursor_to_line_col(size_t cursor, std::span<const LineSpan> lines) -> LineCol;
auto line_col_to_cursor(size_t line, size_t col, std::span<const LineSpan> lines) -> size_t;

}
```

- `cursor_to_line_col`: find which wrapped line contains the cursor byte offset, compute column as `cursor - line.start`
- `line_col_to_cursor`: `lines[line].start + min(col, lines[line].length)` — clamps column to line length

These are pure functions — no widget state, trivially testable.

## Rendering

`detail::text_area_record(dl, field, node)`:

1. **Background rect** — color varies by visual state:
   - Focused: `rgba(65, 65, 78)`
   - Hovered: `rgba(55, 55, 68)`
   - Normal: `rgba(45, 45, 55)`
2. **Focus ring** — blue `RectOutline` when focused: `rgba(80, 160, 240)`, 2px, offset by -1
3. **Clip region** — `clip_push({padding, padding, text_area_w, text_area_h})` — clips both axes
4. **Content rendering:**
   - Compute `wrapped = wrap_lines(value, text_area_w, char_w)`
   - If empty and unfocused: render placeholder text on first line at reduced opacity
   - Otherwise: for each wrapped line, compute `y = line_index * line_height - scroll_y`; skip if entirely outside clip region; render as `TextCmd`
5. **Cursor** — when focused:
   - `{line, col} = cursor_to_line_col(cursor, wrapped)`
   - Draw `FilledRect` at `{col * char_w, line * line_height - scroll_y}`, size `{cursor_w, line_height}`
   - Color: `rgba(220, 220, 240)`
6. **clip_pop()**

Text positions are relative to the clip region origin (padding, padding). All coordinates include the `-scroll_y` offset.

## Input Handling

`detail::text_area_handle_input(field, ev, node)`:

### TextInput event
- Insert text at cursor position
- Respect `max_length` (truncate insertion if needed)
- Advance cursor by inserted length
- Mark field dirty

### KeyPress events

| Key | Action |
|---|---|
| Enter | Insert `\n` at cursor, advance cursor by 1 |
| Backspace | If cursor > 0: delete byte before cursor, cursor-- (joins lines if at `\n`) |
| Delete | If cursor < len: delete byte at cursor (joins lines if at `\n`) |
| Left | cursor-- (stop at 0) |
| Right | cursor++ (stop at len) |
| Home | Move cursor to start of current wrapped line (`lines[line].start`) |
| End | Move cursor to end of current wrapped line (`lines[line].start + lines[line].length`) |
| Up | Same column on previous wrapped line; clamp col to line length; no-op on first line |
| Down | Same column on next wrapped line; clamp col to line length; no-op on last line |

Up/Down navigation:
1. `{line, col} = cursor_to_line_col(cursor, wrapped)`
2. `target_line = line ± 1` (bounds-checked)
3. `cursor = line_col_to_cursor(target_line, col, wrapped)` — clamp handles shorter lines

### MouseButton (pressed)
1. Compute `click_line = floor((y - padding + scroll_y) / line_height)`, clamp to `[0, wrapped.size()-1]`
2. Compute `click_col = floor((x - padding) / char_w + 0.5)`, clamp to `[0, wrapped[click_line].length]`
3. `cursor = line_col_to_cursor(click_line, click_col, wrapped)`
4. Mark dirty if cursor changed

### Scroll adjustment (after every input)
```
cursor_y = cursor_line * line_height
text_area_h = rows * line_height

if cursor_y - scroll_y > text_area_h - line_height:
    scroll_y = cursor_y - text_area_h + line_height   // scroll down
if cursor_y < scroll_y:
    scroll_y = cursor_y                                 // scroll up
```

## File Changes

| File | Change |
|---|---|
| `include/prism/core/delegate.hpp` | Add `TextArea<T>` sentinel, `TextAreaEditState`, `Delegate<TextArea<T>>` declaration |
| `include/prism/core/widget_tree.hpp` | Add `detail::wrap_lines`, `detail::cursor_to_line_col`, `detail::line_col_to_cursor`, `detail::text_area_record`, `detail::text_area_handle_input`, `Delegate<TextArea<T>>` definitions |
| `tests/test_text_area.cpp` | New test file |
| `tests/meson.build` | Add `test_text_area.cpp` |

## Testing

### Unit tests for pure helpers
- `wrap_lines`: empty string, single line fits, exact boundary wrap, mid-character wrap, `\n` only, mixed `\n` + character wrap, trailing `\n`
- `cursor_to_line_col` / `line_col_to_cursor`: round-trip, start/end of wrapped lines, last line, cursor at `\n`

### Delegate rendering tests
- Background color per visual state (normal, hovered, focused)
- Focus ring presence/absence
- Clip region with correct vertical extent
- Placeholder rendering when empty + unfocused
- Multiline text rendering (correct y offsets)
- Cursor position on correct line

### Input tests
- TextInput insertion at various positions
- Enter inserts newline, creates new line
- Backspace at start-of-line joins with previous
- Delete at end-of-line joins with next
- Left/Right across line boundaries
- Home/End on wrapped lines (not logical lines)
- Up/Down with column preservation
- Up/Down with column clamping on shorter lines
- Up on first line = no-op, Down on last line = no-op
- Mouse click-to-position on various lines
- Mouse click below last line clamps to end
- max_length enforcement on TextInput and Enter

### Scroll tests
- Cursor below viewport triggers scroll down
- Cursor above viewport triggers scroll up
- Cursor within viewport = no scroll change

### WidgetTree integration
- Focus dispatch reaches TextArea delegate
- Snapshot contains correct draw commands

## Not In Scope

- Word wrap (future: `wrap_mode` config on sentinel)
- Text selection (Shift+arrows, Shift+click)
- Copy/paste (Ctrl+C/V)
- Line numbers
- Scroll bar indicator
- Undo/redo
- Tab key (currently used for focus cycling)
