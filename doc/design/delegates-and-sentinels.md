# Delegates & Sentinel Types

## Overview

PRISM maps model fields to widgets through **delegates** — compile-time resolved rendering strategies selected by the field's value type. **Sentinel types** are templated wrapper types that carry both a value and its presentation semantics. **Concepts** drive delegate resolution, keeping the system generic and decoupled from specific types.

Two field wrapper types exist:
- `Field<T>` — observable + rendered (delegate generates a widget)
- `State<T>` — observable + invisible (no widget, used for backend state and synchronisation)

## Field vs State

Both share the same observable core (`.get()`, `.set()`, `.on_change()`, `.observe()`). The only difference: reflection emits a widget for `Field<T>` and skips `State<T>`.

Two subscription APIs:
- `.observe(cb)` — fire-and-forget, connection stored internally, lives as long as the Field/State. Use this by default.
- `.on_change().connect(cb)` — returns `[[nodiscard]] Connection` for scoped lifetime management.

```cpp
struct Dashboard {
    Field<std::string> username{"Username", "jeandet"};  // → text input widget
    Field<bool>        dark_mode{"Dark Mode", true};      // → checkbox widget
    State<int>         request_count{0};                  // → observable, no widget
    State<std::string> session_token{""};                 // → observable, no widget
};
```

`State<T>` is for values that participate in the signal graph (triggering reactions, cross-component sync) but have no visual representation.

## Sentinel Types

A sentinel type wraps a value and encodes presentation semantics in the type system. Sentinels are templated with a default underlying type:

```cpp
template <StringLike T = std::string>
struct Label { T value; };

template <StringLike T = std::string>
struct Password { T value; };

template <StringLike T = std::string>
struct TextArea { T value; };

template <Numeric T = double>
struct Slider {
    T value;
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
};
```

Usage in model structs:

```cpp
struct Settings {
    Field<std::string>           username{"Username"};     // → text input (default for string)
    Field<Label<>>               status{"Status", {"OK"}}; // → read-only label
    Field<Password<>>            secret{"Password"};       // → masked text input
    Field<Slider<>>              volume{"Volume", {.value = 0.8}};
    Field<Slider<int>>           quality{"Quality", {.value = 3, .min = 1, .max = 5, .step = 1}};
    Field<Label<MyCustomString>> custom{"Info", {my_string}};  // works with any StringLike type
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
template <TextEditable T>
struct Delegate<T> {
    static void render(DrawList& dl, const Context& ctx, Field<T>& field);
};

template <TextDisplayable T>
struct Delegate<Label<T>> {
    static void render(DrawList& dl, const Context& ctx, Field<Label<T>>& field);
};

template <SliderRenderable T>
struct Delegate<T> {
    static void render(DrawList& dl, const Context& ctx, Field<T>& field);
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
    static void render(DrawList& dl, const Context& ctx, Field<TemperatureKnob>& field) {
        // custom circular knob rendering
    }
};
```

## Default Type-to-Delegate Mapping

When no sentinel is used, `Field<T>` uses the default delegate for `T`:

| `Field<T>` type | Concept matched | Default delegate |
|---|---|---|
| `Field<bool>` | `Toggleable` | Checkbox |
| `Field<std::string>` | `StringLike` | Text input |
| `Field<int>`, `Field<double>` | `Numeric` | Numeric input / spinner |
| `Field<Enum>` | `std::is_enum_v` | Dropdown |

Sentinels override these defaults:

| `Field<Sentinel>` type | Delegate |
|---|---|
| `Field<Label<T>>` | Read-only text label |
| `Field<Password<T>>` | Masked text input |
| `Field<TextArea<T>>` | Multiline text editor |
| `Field<Slider<T>>` | Slider (continuous or discrete based on `step`) |

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

## Design Principles

- **Concepts over concrete types** — delegates match traits, not specific types. Custom string/numeric types work if they satisfy the concept.
- **Type = specification** — the sentinel type carries both the value and its rendering semantics. No runtime metadata, no string annotations.
- **Templated with defaults** — sentinels are generic (`Label<T = std::string>`) so users can plug in their own types.
- **Explicit override** — template specialization of `Delegate<T>` overrides concept-based resolution for custom rendering.
- **Observable core shared** — `Field<T>` and `State<T>` share the same observer machinery. The only difference is UI visibility.

## Open Questions

- Priority/ordering when multiple concepts match — does `SliderRenderable` win over `Numeric` for a type satisfying both?
- Should `Delegate` carry layout hints (preferred size, stretch policy) in addition to rendering?
- Animated transitions between delegate states (e.g., text field gaining focus) — delegate concern or separate animation layer?
