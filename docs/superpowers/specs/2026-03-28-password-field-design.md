# Password Field Widget Design

## Overview

Add a `Password<T>` sentinel that renders masked text (bullets) while editing the underlying string value. Reuses all TextField input logic — only the display differs.

## Decisions

- **Sentinel-only:** `Password<T>` is a sentinel wrapping a string, like `TextField<T>`. `Field<Password<>>` renders as a masked text field.
- **Shared input logic:** `handle_input()` is identical to TextField's — extracted into a `detail::text_field_handle_input()` helper. Both delegates call it.
- **Shared rendering structure:** `record()` follows the same layout (background, clip, text, cursor, focus ring) but replaces the display string with bullet characters. Extracted into `detail::text_field_record()` with a display-transform parameter.
- **Mask character:** `"●"` (U+25CF, `"\xe2\x97\x8f"` in UTF-8). Hardcoded — no configuration in v1.
- **Placeholder visible:** When empty and unfocused, placeholder text renders in cleartext (standard UX).
- **TextEditable concept:** `Password<T>` satisfies `TextEditable` (has `.value` member), so the `ScopedEnum` constraint `requires (!TextEditable<T>)` excludes it correctly.

## New Type

### `Password<T>` sentinel (delegate.hpp)

```cpp
template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const Password&) const = default;
};
```

## Refactored Helpers (widget_tree.hpp, detail namespace)

### `text_field_record()`

Extracts the common rendering logic from `Delegate<TextField<T>>::record()`, parameterized by a display-transform function:

```cpp
template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node,
                       DisplayFn display_fn);
```

- `DisplayFn` takes `const std::string& value` → returns `std::string` (the display text)
- TextField passes identity: `[](const std::string& v) { return v; }`
- Password passes masking: `[](const std::string& v) { return mask_string(v.size()); }`

### `text_field_handle_input()`

Extracts the common input logic from `Delegate<TextField<T>>::handle_input()`:

```cpp
template <typename Sentinel>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node);
```

Both `Delegate<TextField<T>>` and `Delegate<Password<T>>` forward to this.

### `mask_string()`

```cpp
inline std::string mask_string(size_t len) {
    std::string result;
    result.reserve(len * 3);  // UTF-8 "●" is 3 bytes
    for (size_t i = 0; i < len; ++i)
        result += "\xe2\x97\x8f";
    return result;
}
```

### `get_edit_state()` / `ensure_edit_state()`

Moved to `detail` namespace as free functions (currently static methods on `Delegate<TextField<T>>`). Both delegates use them.

## Delegate<Password<T>>

```cpp
template <StringLike T>
struct Delegate<Password<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Password<T>>& field, const WidgetNode& node);
    static void handle_input(Field<Password<T>>& field, const InputEvent& ev, WidgetNode& node);
};
```

Bodies in widget_tree.hpp — one-line forwards to `detail::text_field_record()` and `detail::text_field_handle_input()`.

## Testing

### Unit tests (test_password.cpp)

1. **Default construction:** empty value, empty placeholder, max_length 0
2. **TextEditable concept:** `Password<>` satisfies `TextEditable`
3. **Focus policy:** `tab_and_click`
4. **Masked rendering:** `record()` output contains "●●●" not the actual value
5. **Placeholder visible:** empty + unfocused shows placeholder in cleartext
6. **Cursor rendering:** focused shows cursor rect
7. **TextInput inserts:** type "abc" → field value is "abc", display is "●●●"
8. **Backspace/Delete:** same behavior as TextField
9. **Arrow keys/Home/End:** same cursor movement
10. **max_length enforced:** same truncation behavior

### Integration test

11. **WidgetTree round-trip:** Model with `Field<Password<>>`, build tree, dispatch TextInput, verify field value

### Existing tests

12. **TextField still works:** Refactoring into shared helpers doesn't break existing TextField tests

## Files Changed

| File | Change |
|------|--------|
| `delegate.hpp` | Add `Password<T>` sentinel, declare `Delegate<Password<T>>` |
| `widget_tree.hpp` | Extract `detail::text_field_record()`, `detail::text_field_handle_input()`, `detail::mask_string()`, move `get/ensure_edit_state` to detail. Add `Delegate<Password<T>>` bodies. |
| `tests/test_password.cpp` | New test file |
| `tests/meson.build` | Register `test_password.cpp` |
| `examples/model_dashboard.cpp` | Add `Field<Password<>>` demo |

## Not Changed

Layout, SceneSnapshot, BackendBase, input_event.hpp, model_app.hpp, focus management.
