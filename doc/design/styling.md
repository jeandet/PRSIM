# Styling & Theming

## Overview

Styling in PRISM is plain data — no CSS cascade, no global state, no callbacks. A theme is a struct of style structs, carried through a `Context` value. Per-instance overrides use `std::optional` fields. State-dependent styling (hover, pressed, disabled) is resolved via struct fields, not functions.

## Design Goals

- **Easy to use**: theme comes via Context, no manual plumbing.
- **Hard to misuse**: no cascade or specificity rules, no global mutable state, explicit overrides.
- **Zero overhead**: all plain data, state selection is a branch on an enum, no heap allocation.
- **Full escape hatch**: custom `record()` bypasses the style system entirely.

## Context

Every `record()` call receives a `Context` — a lightweight value carrying the current theme and widget state:

```cpp
struct Context {
    const Theme& theme;
    WidgetState  state;   // hovered, focused, pressed, disabled
};

template <typename T>
concept Widget = requires(const T w, DrawList& dl, const Context& ctx) {
    { w.record(dl, ctx) } -> std::same_as<void>;
};
```

`Context` is not a heap-allocated object — it's a reference to a theme plus a small state struct. Composite widgets forward it to children, optionally modifying the state:

```cpp
void record(DrawList& dl, const Context& ctx) const {
    header.record(dl, ctx);
    body.record(dl, ctx);
}
```

## Theme

A theme is a plain data struct containing style definitions for each widget type:

```cpp
struct Theme {
    ButtonStyle     button;
    ButtonStyle     button_primary;
    LabelStyle      label;
    TextFieldStyle  text_field;
    SliderStyle     slider;
    // ...
};
```

Themes are values — constructible, copyable, serialisable. Creating a custom theme is constructing a struct. Deriving a variant is copying and changing fields:

```cpp
auto dark = default_theme;
dark.button.normal.background = Color::rgba(50, 50, 55);
dark.label.normal.text = Color::rgba(220, 220, 220);
```

No theme registry, no string keys, no inheritance chain.

## Style Structs

Each widget type has a style struct containing visual properties for each state:

```cpp
struct ButtonVisuals {
    Color background;
    Color border;
    Color text;
    float radius;
    float border_width;
    float font_size;
};

struct ButtonStyle {
    ButtonVisuals normal;
    ButtonVisuals hovered;
    ButtonVisuals pressed;
    ButtonVisuals disabled;
};
```

State selection is a branch, not a callback:

```cpp
const ButtonVisuals& resolve(const ButtonStyle& s, WidgetState state) {
    if (state.disabled) return s.disabled;
    if (state.pressed)  return s.pressed;
    if (state.hovered)  return s.hovered;
    return s.normal;
}
```

## Per-Instance Overrides

Widgets may override individual style properties via `std::optional` fields. If set, they win over the theme. If unset, the theme decides:

```cpp
struct Button {
    std::string label;
    Rect bounds;
    std::optional<Color> background;   // override theme if set
    std::optional<float> radius;       // override theme if set

    void record(DrawList& dl, const Context& ctx) const {
        auto& visuals = resolve(ctx.theme.button, ctx.state);
        auto bg = background.value_or(visuals.background);
        auto r  = radius.value_or(visuals.radius);
        dl.filled_rect(bounds, bg);
        // ...
    }
};
```

There is no ambiguity about which value wins — optional is set or it isn't.

## Custom Drawing

For full control, the user writes their own `record()` and ignores the style system. This is always available because `DrawList` is the public API:

```cpp
struct CustomGauge {
    float value;
    Rect bounds;

    void record(DrawList& dl, const Context&) const {
        // No theme, no style struct — draw whatever you want
        dl.filled_rect(bounds, Color::rgba(30, 30, 30));
        dl.filled_rect({bounds.x, bounds.y, bounds.w * value, bounds.h},
                       Color::rgba(0, 200, 100));
    }
};
```

## Python

Themes and styles are plain data — trivial to expose via nanobind:

```python
theme = Theme()
theme.button.normal.background = Color.rgba(0, 120, 215)

# Per-instance override
btn = Button(label="OK", background=Color.rgba(255, 0, 0))
```

Hot-reloading a theme from Python is just reassigning the theme reference in the Context.

## What This Avoids

| Anti-pattern | PRISM's alternative |
|---|---|
| CSS cascade / specificity | Flat lookup: theme → optional override |
| Global stylesheet | Theme is a value, passed via Context |
| String-keyed properties | Typed struct fields |
| Style callbacks / lambdas | State variants as struct fields |
| Theme inheritance chains | Copy + modify fields |

## Open Questions

- Should `WidgetState` be a bitmask or a struct of bools?
- How does Context propagation work for deeply nested widgets — implicit via the call stack, or explicit parent → child?
- Should themes support partial overrides (a "dark mode overlay" that only changes some fields)?
- Animation interpolation between style states — is this a style concern or an animation concern?
