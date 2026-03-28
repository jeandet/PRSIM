# Checkbox Widget Design

## Overview

Two changes:
1. Improve `Delegate<bool>` to render as a proper checkbox box instead of a colored rectangle
2. Add a `Checkbox` sentinel type that bundles a bool + label into a single widget

## Sentinel Type

```cpp
struct Checkbox {
    bool checked = false;
    std::string label;
    bool operator==(const Checkbox&) const = default;
};
```

Non-templated, follows the `Button` pattern. Usage:

```cpp
Field<Checkbox> dark_mode{{"Dark mode"}};
Field<Checkbox> agree{{.label = "I agree", .checked = true}};
```

## Delegate<bool> Visual Update

Replace the current green/grey filled rectangle with a proper checkbox box:

- 16×16 box with border, centered vertically within the 200×30 widget rect
- Unchecked: dark grey border, transparent fill
- Checked: blue fill + white checkmark (text glyph "✓" via `dl.text()`)
- Hover: lighten border/fill
- Press: darken
- Focus: blue focus ring around widget rect (existing pattern)

No label — `Delegate<bool>` renders only the box. Hit target remains the full widget rect.

## Delegate<Checkbox>

### Rendering

Horizontal layout within a single 200×30 widget rect:

- Box (16×16): left-aligned at ~8px padding, vertically centered. Same style as `Delegate<bool>`.
- Label text: rendered at ~32px from left, vertically centered, light grey text (220, 220, 220)
- Hover/press/focus colors follow existing delegate patterns

### Input

- Click anywhere in the widget rect → toggle `checked`
- Space/Enter when focused → toggle `checked`
- `focus_policy = FocusPolicy::tab_and_click`

Toggle logic:

```cpp
auto cb = field.get();
cb.checked = !cb.checked;
field.set(cb);
```

## Shared Box Rendering

Free function to avoid duplication between `Delegate<bool>` and `Delegate<Checkbox>`:

```cpp
void draw_check_box(DrawList& dl, float x, float y, bool checked,
                    const WidgetVisualState& vs);
```

Draws the 16×16 box + checkmark at the given position with hover/press color adjustments. Lives in `delegates.hpp`.

## Testing

In existing test files:

- `Checkbox` default construction: `checked == false`, `label == ""`
- Toggle via `MouseButton` press
- Toggle via `KeyPress` Space and Enter
- `Field<Checkbox>` observer fires on toggle
- `Delegate<bool>` still toggles correctly with new visuals

Demo: add `Field<Checkbox>` to `model_dashboard` example.

## Decisions

- **Binary only** — no tri-state/indeterminate. Can be added later as a separate type if needed.
- **No composition** — horizontal layout + cross-widget click wiring don't exist yet. Self-contained sentinel is pragmatic.
- **Non-templated** — label is always `std::string`. No benefit to `Checkbox<T>`.
- **Whole widget clickable** — clicking box or label toggles. Standard desktop UX.
