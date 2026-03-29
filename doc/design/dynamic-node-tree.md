# Dynamic Node Tree: Universal Construction Layer

## Status: Implemented (Phase 3.6)

## Overview

`Node` is a type-erased intermediate between user UI descriptions and `WidgetNode`. It enables PRISM to work on both C++26 (with P2996 reflection) and C++23 (without reflection) compilers through a single codebase.

Both construction paths converge to the same `Node` tree, which is then converted to `WidgetNode` in a single pass:

```
C++26 path:    struct with Field<T> members  →  reflection walk    →  Node tree  →  WidgetNode tree
C++23 path:    struct with view() method     →  ViewBuilder calls  →  Node tree  →  WidgetNode tree
                                                                                  ↑ same output
```

## Key Design Decisions

**Node is internal.** Users interact with `ViewBuilder` (via `view()`) or reflection. The `Node` layer is not exposed as public API. Future phases may promote it for dynamic UI construction.

**Only 2 `#if __cpp_impl_reflection` locations:**
1. `build_node_tree()` in `widget_tree.hpp` — reflection walk vs `static_assert(has_view)`
2. Enum helpers in `delegate.hpp` — P2996 `enumerators_of` vs magic_enum

**Pre-C++26 requirement:** All model structs must provide a `view()` method. The `has_view` concept detects this via duck typing.

## Node Structure

```cpp
// include/prism/core/node.hpp
struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    // Type-erased — captures Delegate<T> calls
    std::function<void(WidgetNode&)> build_widget;

    // Type-erased — captures Field<T>::on_change()
    std::function<Connection(std::function<void()>)> on_change;

    // Canvas dependencies — multiple fields can trigger redraw
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;
};
```

## Factory Functions

Defined in `widget_tree.hpp` (after `WidgetNode` definition, since lambdas need the complete type):

- **`node_leaf<T>(Field<T>&)`** — creates a leaf Node that type-erases a `Field<T>` and its `Delegate<T>`. The `build_widget` lambda captures delegate `record()` and `handle_input()` calls.

- **`node_canvas<T>(T&)`** — creates a canvas Node for models with a `canvas()` method. The `build_widget` lambda wires the model's `canvas()` to the WidgetNode's draw callback.

## Pipeline

The `WidgetTree` constructor runs four passes:

```cpp
template <typename Model>
explicit WidgetTree(Model& model) {
    auto node_tree = build_node_tree(model);     // 1. Model → Node tree
    root_ = build_widget_node(node_tree);         // 2. Node → WidgetNode tree
    connect_dirty(node_tree, root_);              // 3. Wire on_change → mark_dirty
    build_index(root_);                           // 4. WidgetId → WidgetNode* map
    clear_dirty();
}
```

### 1. `build_node_tree(Model&)`

With reflection (`__cpp_impl_reflection`): walks struct members via P2996, creates `node_leaf` for `Field<T>`, skips `State<T>`, recurses into nested structs. Falls through to `view()` if the model has one.

Without reflection: `static_assert(has_view<Model>)`, then calls `model.view(vb)` where `vb` is a `ViewBuilder` targeting the Node tree.

### 2. `build_widget_node(Node&)`

Recursive conversion. Each Node becomes a WidgetNode. Leaf nodes get their `build_widget` lambda called. Container nodes recurse into children. Layout kind is propagated.

### 3. `connect_dirty(Node&, WidgetNode&)`

Walks both trees in parallel. For each leaf Node with an `on_change`, connects it to `widget_node.mark_dirty()`. For canvas nodes, connects all `dependencies` entries.

### 4. `build_index(WidgetNode&)`

Populates the `WidgetId → WidgetNode*` lookup map for hit testing and input routing.

## ViewBuilder

`ViewBuilder` targets a `Node&` and maintains a `std::vector<Node*>` stack:

- `widget(field)` → calls `node_leaf<T>(field)`, appends to current container
- `component(model)` → calls `build_node_tree(model)`, appends children
- `row(fn)` / `column(fn)` → push container Node with LayoutKind, call fn, pop
- `canvas(model)` → calls `node_canvas(model)`, returns `CanvasHandle`
- `spacer()` → appends spacer Node

`CanvasHandle` holds a `Node&` and provides `depends_on(field)` which pushes to `node.dependencies`.

## Traits (No Reflection Required)

`include/prism/core/traits.hpp` provides `is_field_v<T>` and `is_state_v<T>` via pure SFINAE — no reflection dependency. These are used by `build_node_tree` on the reflection path to classify struct members.

## Enum Support

`delegate.hpp` provides `enum_count<T>()`, `enum_label<T>(i)`, `enum_index<T>(v)`, `enum_from_index<T>(i)`:

- With reflection: uses P2996 `enumerators_of` and `identifier_of`
- Without reflection: delegates to `magic_enum` (fetched as Meson wrap)

## Files

| File | Role |
|------|------|
| `include/prism/core/node.hpp` | `Node` struct definition |
| `include/prism/core/traits.hpp` | `is_field_v`, `is_state_v` (SFINAE, no reflection) |
| `include/prism/core/widget_tree.hpp` | `node_leaf`, `node_canvas`, `ViewBuilder`, `build_node_tree`, `build_widget_node`, `connect_dirty` |
| `include/prism/core/delegate.hpp` | `LayoutKind` enum, conditional enum helpers |
| `include/prism/core/reflect.hpp` | Reflection utilities, guarded behind `#if __cpp_impl_reflection` |
| `meson.build` | Conditional `-freflection` flag |
| `src/meson.build` | Conditional magic_enum dependency |

## Future

- **Runtime tree mutation** (`add_leaf`/`remove_subtree`) for dynamic UIs
- **Node as public API** for JSON editors, plugin systems, Python bindings
- **Sugar:** builder pattern, path lookup, declarative rules, schema-driven construction
