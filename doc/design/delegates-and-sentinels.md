# Delegates & Sentinel Types

## Overview

In PRISM's **Model-View-Behavior (MVB)** architecture, delegates are the **View** layer — compile-time resolved rendering strategies selected by the field's value type. **Sentinel types** are templated wrapper types that carry both a value and its presentation semantics. **Concepts** drive delegate resolution, keeping the system generic and decoupled from specific types.

Two field wrapper types exist:
- `Field<T>` — observable + rendered (delegate generates a widget). Part of the **Model** layer.
- `State<T>` — observable + invisible (no widget, used for backend state and synchronisation). Part of the **Model** layer.

## Widget Visual State

Every widget node carries a `WidgetVisualState` that tracks interactive feedback, plus an `std::any edit_state` for ephemeral delegate-managed UI state:

```cpp
struct WidgetVisualState {
    bool hovered = false;   // mouse cursor is over widget
    bool pressed = false;   // mouse button held down on widget
    bool focused = false;   // keyboard focus
};
```

Visual state is managed by the `WidgetTree`:
- `update_hover(optional<WidgetId>)` — MouseMove → hit_test → set hovered, clear previous
- `set_pressed(WidgetId, bool)` — MouseButton down/up → set pressed state

Delegates receive `WidgetNode&` (not `WidgetVisualState&`), giving access to `visual_state`, `edit_state`, and `dirty`:
- `record(DrawList&, const Field<T>&, const WidgetNode&)` — for rendering with hover/press/focus feedback
- `handle_input(Field<T>&, const InputEvent&, WidgetNode&)` — delegates can mutate node state

Since `WidgetNode` is forward-declared in `delegate.hpp` (to avoid circular includes with `widget_tree.hpp`), visual state is accessed via `node_vs(node)` helper. Delegates that need the complete `WidgetNode` type (e.g. for `edit_state` access) declare methods in `delegate.hpp` and define them in `widget_tree.hpp`.

## Field vs State

Both share the same observable core (`.get()`, `.set()`, `.on_change()`, `.observe()`). The only difference: reflection emits a widget for `Field<T>` and skips `State<T>`.

Two subscription APIs:
- `.observe(cb)` — fire-and-forget, connection stored internally, lives as long as the Field/State. Use this by default.
- `.on_change().connect(cb)` — returns `[[nodiscard]] Connection` for scoped lifetime management.

```cpp
struct Dashboard {
    Field<std::string> username{"jeandet"};   // → text input widget
    Field<bool>        dark_mode{true};        // → checkbox widget
    State<int>         request_count{0};       // → observable, no widget
    State<std::string> session_token{""};      // → observable, no widget
};
```

`Field<T>` holds only the value — no display label. The member name via P2996 reflection provides identity. Display labels (e.g. "Username") are a form-layout concern, not a model concern.

`State<T>` is for values that participate in the signal graph (triggering reactions, cross-component sync) but have no visual representation.

## Sentinel Types

A sentinel type wraps a value and encodes presentation semantics in the type system. Sentinels are templated with a default underlying type:

```cpp
template <StringLike T = std::string>
struct Label { T value; };

template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;  // 0 = unlimited
};

template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
};

template <StringLike T = std::string>
struct TextArea { T value; };  // not yet implemented

template <Numeric T = double>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
};

struct Button {
    std::string text;
    uint64_t click_count = 0;  // increments on click, observable via on_change()
};

struct Checkbox {
    bool checked = false;
    std::string label{};
};

template <ScopedEnum T>
struct Dropdown {
    T value{};
    std::vector<std::string> labels{};  // empty = reflection-derived names
};
```

Usage in model structs:

```cpp
enum class Theme { Light, Dark, System };

struct Settings {
    Field<std::string>           username{"jeandet"};                        // → read-only string display
    Field<TextField<>>           search{{.placeholder = "Search..."}};        // → editable text field
    Field<Label<>>               status{{"OK"}};                             // → read-only label
    Field<Password<>>            secret{{.placeholder = "API key"}};          // → masked text input
    Field<Slider<>>              volume{{.value = 0.8}};                     // → continuous slider
    Field<Button>                save{{"Save"}};                             // → clickable button
    Field<bool>                  dark_mode{true};                            // → checkbox (default delegate)
    Field<Checkbox>              notifications{{.checked = true, .label = "Enable"}};  // → checkbox with label
    Field<Theme>                 theme{Theme::Dark};                         // → auto-dropdown via reflection
    Field<Slider<int>>           quality{{.value = 3, .min = 1, .max = 5, .step = 1}};  // → discrete slider
    Field<Label<MyCustomString>> custom{{my_string}};                        // works with any StringLike type
};
```

The template parameter defaults mean `Label<>` is `Label<std::string>`, `Slider<>` is `Slider<double>`. Users can substitute their own types as long as they satisfy the concept.

## Concepts

Concepts define the traits a type must have for a delegate to handle it. Delegates match on concepts, not concrete types:

```cpp
template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

Sentinel-level concepts define what a delegate needs from the sentinel wrapper:

```cpp
template <typename T>
concept TextEditable = requires(const T& s) {
    { s.value } -> StringLike;
};

template <typename T>
concept TextDisplayable = requires(const T& s) {
    { s.value } -> StringLike;
};

template <typename T>
concept SliderRenderable = requires(const T& s) {
    { s.value } -> Numeric;
    { s.min }   -> Numeric;
    { s.max }   -> Numeric;
};

template <typename T>
concept Toggleable = requires(const T& s) {
    { s } -> std::convertible_to<bool>;
};
```

## Delegate Resolution

Delegates are resolved at compile time via concept matching. The delegate is a struct with a static `render` method:

```cpp
// Concept-based delegates — match on traits, not types
// Each delegate has record() for rendering and handle_input() for interaction.
// Both receive WidgetNode& for visual state, edit state, and dirty tracking.

template <TextEditable T>
struct Delegate<T> {
    static void record(DrawList& dl, const Field<T>& field, const WidgetNode& node);
    static void handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node);
};

template <TextDisplayable T>
struct Delegate<Label<T>> {
    static void record(DrawList& dl, const Field<Label<T>>& field, const WidgetNode& node);
    static void handle_input(Field<Label<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <SliderRenderable T>
struct Delegate<T> {
    static void record(DrawList& dl, const Field<T>& field, const WidgetNode& node);
    static void handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node);
};
```

A user-defined sentinel that satisfies `SliderRenderable` automatically gets the slider delegate:

```cpp
struct TemperatureKnob {
    float value;
    float min = -40.0f;
    float max = 85.0f;
    float step = 0.5f;
};
// Satisfies SliderRenderable → picks up slider delegate automatically
// Unless explicitly specialized for custom rendering
```

Explicit specialization overrides concept-based resolution:

```cpp
template <>
struct Delegate<TemperatureKnob> {
    static void record(DrawList& dl, const Field<TemperatureKnob>& field, const WidgetNode& node) {
        // custom circular knob rendering with hover/press feedback
    }
    static void handle_input(Field<TemperatureKnob>& field, const InputEvent& ev, WidgetNode& node) {
        // custom input handling
    }
};
```

## Default Type-to-Delegate Mapping

When no sentinel is used, `Field<T>` uses the default delegate for `T`:

| `Field<T>` type | Concept matched | Default delegate | Status |
|---|---|---|---|
| `Field<bool>` | `Toggleable` | Checkbox | **Implemented** |
| `Field<std::string>` | `StringLike` | Read-only text display | **Implemented** |
| `Field<int>`, `Field<double>` | `Numeric` | Filled rect (catch-all fallback) | **Implemented** |
| `Field<ScopedEnum>` | `ScopedEnum` concept | Auto-dropdown via P2996 `enumerators_of` | **Implemented** |

Sentinels override these defaults:

| `Field<Sentinel>` type | Delegate | Status |
|---|---|---|
| `Field<Label<T>>` | Read-only text label | **Implemented** |
| `Field<TextField<T>>` | Single-line editable text field (cursor, scroll, placeholder, max_length) | **Implemented** |
| `Field<Password<T>>` | Masked text input (shares `detail::` helpers with TextField) | **Implemented** |
| `Field<Slider<T>>` | Slider (continuous or discrete based on `step`) | **Implemented** |
| `Field<Button>` | Clickable button (text label, observable click_count) | **Implemented** |
| `Field<Checkbox>` | Checkbox box + label text, whole-widget toggle | **Implemented** |
| `Field<Dropdown<T>>` | Dropdown with custom labels | **Implemented** |
| `Field<TextArea<T>>` | Multiline text editor (character wrap, vertical scroll, Up/Down navigation, configurable rows) | **Implemented** |

## Reflection Walk

During widget tree construction, P2996 reflection walks the model struct:

```
for each member M of Model:
    if M is Field<T>:
        resolve Delegate<T>
        create widget node with delegate
        connect Field<T>::on_change() → mark widget dirty
    if M is State<T>:
        skip (no widget)
    if M is a nested struct (component):
        recurse
```

## Shared Implementation Helpers

TextField and Password share their implementation via parameterized `detail::` helpers in `widget_tree.hpp`:

- `detail::text_field_record<Sentinel, DisplayFn>()` — rendering parameterized by a display transform (identity for TextField, masking for Password)
- `detail::text_field_handle_input<Sentinel>()` — input handling shared identically
- `detail::mask_string(size_t)` — returns UTF-8 bullet "●" characters
- `detail::get_text_edit_state()` / `detail::ensure_text_edit_state()` — `TextEditState` access from `WidgetNode::edit_state`

Enum dropdown and `Dropdown<T>` similarly share `detail::dropdown_record()` and `detail::dropdown_handle_input()`.

## Focus Policy

Each delegate declares a compile-time `FocusPolicy`:

```cpp
enum class FocusPolicy : uint8_t { none, tab_and_click };
```

- `none` — non-interactive (Label, StringLike, primary catch-all)
- `tab_and_click` — interactive (bool, Slider, Button, Checkbox, TextField, Password, ScopedEnum, Dropdown)

Tab/Shift+Tab cycles through focusable widgets. Click on a focusable widget sets focus. Focused widgets render a blue focus ring (`RectOutline`, 2px).

## Overlay System

Dropdown popups render via the overlay system:

- `WidgetNode::overlay_draws` — per-widget DrawList for content above all widgets
- `SceneSnapshot::overlay` — collected during `layout_flatten`, rendered last
- `SceneSnapshot::overlay_geometry` — `{WidgetId, Rect}` pairs for overlay hit-testing
- `hit_test()` checks overlay geometry first (on top), then normal widget geometry
- `WidgetTree::close_overlays()` — clears overlay draws + resets edit_state on click-outside

## Design Principles

- **Delegates are the View layer** — in MVB terms, delegates handle widget-level input mechanics (e.g. translating a click into a toggle). They are PRISM-internal, not user-written.
- **Concepts over concrete types** — delegates match traits, not specific types. Custom string/numeric types work if they satisfy the concept.
- **Type = specification** — the sentinel type carries both the value and its rendering semantics. No runtime metadata, no string annotations.
- **Templated with defaults** — sentinels are generic (`Label<T = std::string>`) so users can plug in their own types.
- **Explicit override** — template specialization of `Delegate<T>` overrides concept-based resolution for custom rendering.
- **Observable core shared** — `Field<T>` and `State<T>` share the same observer machinery. The only difference is UI visibility.

## Open Questions

- Should `Delegate` carry layout hints (preferred size, stretch policy) in addition to rendering?
- Animated transitions between delegate states (e.g., text field gaining focus) — delegate concern or separate animation layer?
