# Custom Widget from Type — Design Spec

**Date:** 2026-04-11
**Status:** Approved
**Strategic priority:** #4 — specialize a type once, propagates everywhere

## Problem

PRISM's delegate dispatch already works at compile time via `Delegate<T>` template specialization. But:

- The name "Delegate" is an internal pattern name; users think in "widgets"
- There's no concept to detect whether a type has a custom widget (needed for propagation to tables, inspector, etc.)
- Sentinel authoring requires knowing internal details (`std::any_cast` for edit state, include-order sensitivity)
- Built-in sentinels use the same mechanism as user code, but this isn't obvious or documented

## Solution

### 1. Rename `Delegate<T>` → `Widget<T>`

Single public extension point. The primary template is empty (not a widget):

```cpp
namespace prism {

template <typename T>
struct Widget {};

}
```

### 2. `is_widget_v<T>` concept

Detects whether `Widget<T>` has been specialized with the required static members:

```cpp
template <typename T>
concept is_widget_v = requires(DrawList& dl, const Field<T>& cf, Field<T>& f,
                                const InputEvent& ev, WidgetNode& node) {
    Widget<T>::record(dl, cf, node);
    Widget<T>::handle_input(f, ev, node);
};
```

### 3. Widget specialization API

Built-in and user-defined widgets use the same interface:

```cpp
template <>
struct prism::Widget<MyType> {
    // Required
    static void record(DrawList& dl, const Field<MyType>& f, WidgetNode& node);
    static void handle_input(Field<MyType>& f, const InputEvent& ev, WidgetNode& node);

    // Optional (detected via requires-clauses)
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool expand = false;
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
};
```

Optional members default to `FocusPolicy::none`, `expand = false`, no expand_axis if absent.

### 4. Templated sentinel support

User-defined templated sentinels with concept-constrained partial specialization:

```cpp
template <Numeric T>
struct prism::Widget<Knob<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static void record(DrawList& dl, const Field<Knob<T>>& f, WidgetNode& node);
    static void handle_input(Field<Knob<T>>& f, const InputEvent& ev, WidgetNode& node);
};
```

### 5. `WidgetNode::get_or_create<S>()`

Convenience for edit state access, replaces `std::any_cast` boilerplate:

```cpp
template <typename S>
S& get_or_create() {
    if (!edit_state.has_value() || edit_state.type() != typeid(S))
        edit_state = S{};
    return std::any_cast<S&>(edit_state);
}
```

Used by built-in sentinels (TextField, Dropdown, etc.) and user sentinels equally.

### 6. Concept-constrained partial specializations

Existing concept-based delegates (`Numeric`, `StringLike`, `ScopedEnum`) become concept-constrained `Widget<T>` partial specializations:

```cpp
template <Numeric T>
struct Widget<T> { /* renders value as text */ };

template <StringLike T>
struct Widget<T> { /* renders string */ };

template <ScopedEnum T>
struct Widget<T> { /* auto-dropdown via reflection */ };
```

### 7. Fallback for non-widget types

`node_leaf<T>()` checks `is_widget_v<T>`:
- If true: uses `Widget<T>::record`, `Widget<T>::handle_input`, reads optional members
- If false: default renderer (filled rect with hover, no-op input) — same as today's primary `Delegate<T>`

## Propagation sites

Every place PRISM renders a `Field<T>` resolves through `Widget<T>`:

| Site | Mechanism | Change needed |
|------|-----------|---------------|
| `node_leaf<T>()` | Captures `Widget<T>` in lambdas | Rename from Delegate |
| Reflection walk (`build_node_tree`) | Calls `node_leaf<T>()` | None (follows from above) |
| `vb.widget(field)` | Calls `node_leaf<T>()` | None |
| Tables (column cells) | Type-erased render function | Verify dispatch through Widget<T> |
| Virtual lists (rows) | `node_leaf<T>()` | None |
| Future inspector | Reflection walk + `node_leaf<T>()` | Automatic |

## Scope

**What changes:**
- Rename `Delegate<T>` → `Widget<T>` in all specializations and dispatch sites
- Add `is_widget_v<T>` concept
- Add `WidgetNode::get_or_create<S>()` helper
- Migrate all built-in `std::any_cast` usages to `get_or_create`

**What stays the same:**
- `Field<T>`, `State<T>`, `Derived<T>`, `Shared<T>` — untouched
- Observer/sender system — untouched
- ViewBuilder API — untouched
- Layout system — untouched
- Input routing — untouched
- Directory structure (`delegates/`) — kept, file/types renamed inside

## Non-goals

- Runtime registration (plugin DLLs) — compile-time only for now
- New built-in widget types — this is about the extension mechanism
- Breaking changes to sentinel data structs (Slider, Button, etc.) — only the Delegate wrapper renames
