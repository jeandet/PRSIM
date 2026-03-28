# Dropdown Widget Design

## Overview

Add a dropdown (combo-box) widget to PRISM. Scoped enums automatically render as dropdowns via reflection; `Dropdown<T>` sentinel provides optional custom labels. Introduces an overlay layer in `SceneSnapshot` for the popup list.

## Decisions

- **Enum-first:** `Field<MyEnum>` renders as a dropdown with labels from `enumerators_of(^^MyEnum)`. No sentinel needed for the default case.
- **Custom labels:** `Dropdown<T>` sentinel wraps an enum value + optional `labels` vector for display name overrides.
- **Overlay layer:** `SceneSnapshot` gains an `overlay` draw list rendered last, on top of all widgets. Reusable for tooltips/menus later.
- **Popup position:** Always below the trigger. No flip-above logic in v1.
- **Keyboard navigation:** Up/Down arrows move highlight, Enter selects, Escape cancels.
- **Close triggers:** Select an option, click outside, press Escape, or lose focus (Tab away).

## New Types

### `ScopedEnum` concept (delegate.hpp)

```cpp
template <typename T>
concept ScopedEnum = std::is_scoped_enum_v<T>;
```

### `Dropdown<T>` sentinel (delegate.hpp)

```cpp
template <ScopedEnum T>
struct Dropdown {
    T value{};
    std::vector<std::string> labels{};  // empty = use reflection
    bool operator==(const Dropdown&) const = default;
};
```

When `labels` is empty, display names come from reflection. When non-empty, `labels[i]` maps to the i-th enumerator (must match `enumerators_of` order and count).

### `DropdownEditState` (delegate.hpp)

```cpp
struct DropdownEditState {
    bool open = false;
    size_t highlighted = 0;  // index into enumerator list
    Rect popup_rect{};       // absolute position, for overlay hit-testing
};
```

### Reflection helper (delegate.hpp)

```cpp
template <ScopedEnum T>
consteval size_t enum_count() {
    return std::meta::enumerators_of(^^T).size();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    return std::string(std::meta::identifier_of(enums[index]));
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    size_t result = 0, i = 0;
    template for (constexpr auto e : enums) {
        if ([:e:] == value) result = i;
        ++i;
    }
    return result;
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    T result{};
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = [:e:];
        ++i;
    }
    return result;
}
```

### Key constants (input_event.hpp)

```cpp
namespace keys {
    inline constexpr int32_t up   = 0x4000'0052;  // SDLK_UP
    inline constexpr int32_t down = 0x4000'0051;  // SDLK_DOWN
}
```

## Overlay System

### SceneSnapshot change

```cpp
struct SceneSnapshot {
    uint64_t version = 0;
    std::vector<std::pair<WidgetId, Rect>> geometry;
    std::vector<DrawList> draw_lists;
    std::vector<uint16_t> z_order;
    DrawList overlay;  // rendered last, on top of everything, no clip
};
```

### WidgetNode change

```cpp
struct WidgetNode {
    // ... existing fields ...
    DrawList overlay_draws;  // populated by delegates that need overlay rendering
};
```

`build_snapshot` collects all non-empty `overlay_draws` into `SceneSnapshot::overlay` after the main layout pass, translating draw commands to absolute coordinates (same offset as the widget's geometry).

### Backend change

In `render_snapshot`, after rendering all z-ordered draw lists, render `snap.overlay` with clip rect reset to the full window.

## Delegate<T> for ScopedEnum

### record()

1. Background rect (darker on hover, blue border on focus)
2. Current value text (from reflection or custom label)
3. Down-arrow indicator on the right side (triangle or "v" glyph)
4. Focus ring when focused
5. If `open`: write popup to `node.overlay_draws`:
   - Shadow/border rect below the trigger
   - One text row per enumerator, highlighted row has distinct background
   - Clip is NOT applied (overlay renders without parent clip)

### handle_input()

**When closed:**
- **Click / Space / Enter:** Open popup, set `highlighted` to current selection index
- **Up/Down:** Cycle through values directly without opening (quick-select)

**When open:**
- **Up arrow:** Move highlight up (wrap to bottom)
- **Down arrow:** Move highlight down (wrap to top)
- **Enter:** Select highlighted option → `field.set(enum_from_index(highlighted))`, close
- **Escape:** Close without changing
- **Click on option:** Select it, close
- **Click outside popup:** Close without changing

After any change: update `DropdownEditState` in `node.edit_state`, mark dirty.

## Delegate<Dropdown<T>>

Same as `Delegate<T>` for enums, but resolves labels from `Dropdown::labels` when non-empty, falling back to reflection when empty. The `value` field holds the enum, same `handle_input` logic.

Both delegates share implementation via a common static helper that takes a label-resolver function, avoiding duplication.

## model_app Changes

Click-outside detection for popup close: when a `MouseButton` press hits no widget (or a different widget), any open dropdown must close. This requires:

- `WidgetTree::close_overlays()` — iterates nodes with non-empty `overlay_draws`, resets their `DropdownEditState::open` to false, clears `overlay_draws`, marks dirty.
- Called in `model_app` before dispatching a click to a different widget.

## Testing

### Unit tests (test_dropdown.cpp)

1. **Reflection helpers:** `enum_count`, `enum_label`, `enum_index`, `enum_from_index` round-trip
2. **Default render:** `Delegate<MyEnum>::record()` produces background + current label text + arrow indicator
3. **Open popup:** Click trigger → `overlay_draws` contains option rows
4. **Keyboard select:** Open → Down → Down → Enter → field value updated
5. **Escape closes:** Open → Escape → popup closed, value unchanged
6. **Click option:** Open → click on option rect → field value updated, popup closed
7. **Up/Down quick-select:** When closed, Up/Down cycle enum value
8. **Focus ring:** Verify outline when focused
9. **Custom labels:** `Delegate<Dropdown<MyEnum>>` uses provided labels instead of reflection names
10. **Overlay in snapshot:** Build snapshot with open dropdown → `snapshot.overlay` is non-empty

### Integration test

11. **Full round-trip:** Model with `Field<MyEnum>`, build WidgetTree, focus, open, select, verify field value

### Existing tests

12. **No regression:** All existing delegate/widget_tree/model_app tests still pass

## Files Changed

| File | Change |
|------|--------|
| `input_event.hpp` | `keys::up`, `keys::down` |
| `delegate.hpp` | `ScopedEnum` concept, `Dropdown<T>`, `DropdownEditState`, reflection helpers, `Delegate<T>` for enums, `Delegate<Dropdown<T>>` |
| `widget_tree.hpp` | `overlay_draws` on `WidgetNode`, `close_overlays()`, collect overlays in `build_snapshot` |
| `scene_snapshot.hpp` | `DrawList overlay` field |
| `model_app.hpp` | Call `close_overlays()` on click-outside |
| `software_backend.cpp` | Render `snap.overlay` after main scene |
| `tests/test_dropdown.cpp` | New test file |
| `tests/meson.build` | Register `test_dropdown.cpp` |
| `examples/model_dashboard.cpp` | Add `Field<SomeEnum>` demo |

## Not Changed

Layout system, focus management logic (dropdown uses existing `tab_and_click`), DrawList format (overlay is just another DrawList), BackendBase vtable.
