# Keyboard Focus Management

## Summary

Add keyboard focus tracking to the widget tree: Tab/Shift+Tab cycling, click-to-focus, and Space/Enter activation. Follows Qt conventions — interactive widgets are focusable by default, passive ones are not.

## Motivation

Focus is a prerequisite for any keyboard-driven widget (text fields, dropdowns). Currently all keyboard events are ignored by `model_app`. This adds the focus infrastructure that text input will build on.

## Scope

### Changes

| File | Change |
|---|---|
| `include/prism/core/delegate.hpp` | Add `FocusPolicy` enum, `focus_policy` static member to each Delegate |
| `include/prism/core/widget_tree.hpp` | Add `focused_id_`, `focus_order_`, focus methods, `WidgetNode::focus_policy` |
| `include/prism/core/model_app.hpp` | Route Tab/Shift+Tab, click-to-focus, Space/Enter to focused widget |
| `tests/test_widget_tree.cpp` | Focus tracking and Tab cycling tests |

### Unchanged

- `BackendBase`, `SoftwareBackend`, rendering — no changes
- `DrawList`, `SceneSnapshot`, `InputEvent` — no changes
- `Field<T>`, `Connection`, `SenderHub` — no changes

## Design

### FocusPolicy

```cpp
enum class FocusPolicy : uint8_t { none, tab_and_click };
```

Each `Delegate<T>` declares a static constexpr member:

```cpp
template <typename T>
struct Delegate {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    // ... record(), handle_input()
};
```

Interactive delegates override to `tab_and_click`:

| Delegate | FocusPolicy |
|---|---|
| Primary `Delegate<T>` | `none` |
| `Delegate<StringLike T>` | `none` |
| `Delegate<Label<T>>` | `none` |
| `Delegate<bool>` | `tab_and_click` |
| `Delegate<Slider<T>>` | `tab_and_click` |
| `Delegate<Button>` | `tab_and_click` |

### WidgetNode changes

Add one field:

```cpp
struct WidgetNode {
    // ... existing fields ...
    FocusPolicy focus_policy = FocusPolicy::none;
};
```

Set during `build_leaf()` from `Delegate<T>::focus_policy`.

### WidgetTree changes

New private members:

```cpp
WidgetId focused_id_ = 0;
std::vector<WidgetId> focus_order_;  // focusable widget IDs in tree order
```

`focus_order_` is built during `build_index()` by collecting leaf IDs where `node.focus_policy != FocusPolicy::none`, in the order they are visited (which is tree/struct member order).

New public methods:

```cpp
WidgetId focused_id() const;
void set_focused(WidgetId id);      // clear old, set new, mark both dirty
void clear_focus();                  // unfocus current, mark dirty
void focus_next();                   // Tab — advance in focus_order_, wrap
void focus_prev();                   // Shift+Tab — reverse, wrap
```

`set_focused(id)` ignores the call if `id` is not in `focus_order_` (non-focusable widget). `focus_next()`/`focus_prev()` on empty `focus_order_` are no-ops. If no widget is focused, `focus_next()` focuses the first, `focus_prev()` focuses the last.

### model_app event routing

Inside the event dispatch lambda, add handling for `KeyPress`:

```
KeyPress with key == Tab:
    if mods & Shift:  tree.focus_prev()
    else:             tree.focus_next()

KeyPress with key == Space or key == Return:
    if tree.focused_id() != 0:
        tree.dispatch(tree.focused_id(), ev)

MouseButton down:
    existing: set_pressed, dispatch
    new: if hit widget has tab_and_click policy → tree.set_focused(id)
         if hit empty space or non-focusable widget → tree.clear_focus()
```

SDL3 keycodes for Tab, Space, and Return are plain ASCII values (`SDLK_TAB = 0x09`, `SDLK_SPACE = 0x20`, `SDLK_RETURN = 0x0D`). Define key/mod constants in `input_event.hpp` so both `model_app.hpp` and `delegate.hpp` can use them without an SDL dependency:

```cpp
namespace keys {
    inline constexpr int32_t tab   = 0x09;   // '\t', matches SDLK_TAB
    inline constexpr int32_t space = 0x20;   // ' ',  matches SDLK_SPACE
    inline constexpr int32_t enter = 0x0D;   // '\r', matches SDLK_RETURN
}
namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT (LSHIFT|RSHIFT)
}
```

### Delegate handle_input updates

`Delegate<bool>` — existing MouseButton press toggle, add: if `KeyPress` with key Space or Return → toggle.

`Delegate<Button>` — existing MouseButton press → increment click_count, add: if `KeyPress` with key Space or Return → increment click_count.

`Delegate<Slider<T>>` — no keyboard activation for now. Arrow keys are a future feature.

Delegates match on `KeyPress` in addition to `MouseButton`, using `keys::space` and `keys::enter` from `input_event.hpp` (defined above). No SDL dependency in delegate code.

### Visual feedback — focus ring

When `vs.focused` is true, interactive delegates draw a `RectOutline` around the widget. Consistent style across all delegates:

```cpp
if (vs.focused)
    dl.rect_outline({-1, -1, w+2, h+2}, Color::rgba(80, 160, 240), 2.0f);
```

A blue outline, 2px wide, slightly outside the widget bounds. Drawn after the widget content so it's on top.

## Testing

Tests use `TestBackend` or construct `WidgetTree` directly — no SDL needed.

### Test cases

1. **Focus order matches struct member order** — build tree from a model, verify `focus_order_` contains only focusable widget IDs in struct order.
2. **Tab cycles forward** — call `focus_next()` repeatedly, verify it cycles through focus_order_ and wraps.
3. **Shift+Tab cycles backward** — call `focus_prev()` repeatedly, verify reverse order and wrap.
4. **set_focused on non-focusable is no-op** — call `set_focused(label_id)`, verify focused_id stays 0.
5. **set_focused marks dirty** — verify old and new focused widgets are marked dirty after `set_focused()`.
6. **clear_focus from focused state** — verify focused widget is marked dirty and focused_id becomes 0.
7. **Space/Enter dispatches to focused widget** — wire a handler on a Button's on_input, send KeyPress(space), verify handler fires.
8. **Delegate<bool> toggles on Space** — focus a bool field, send KeyPress(space), verify value toggles.
9. **Delegate<Button> increments on Enter** — focus a Button field, send KeyPress(enter), verify click_count increments.

## What This Enables

- Tab navigation through interactive widgets
- Keyboard activation of buttons and toggles
- Focus ring visual indicator
- Foundation for TextField (which needs focus to receive key events)
