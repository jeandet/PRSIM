# Node Tree as Universal Construction Layer — Design Spec

## Summary

Introduce a `Node` class as the universal intermediate between user-defined UI descriptions and the internal `WidgetNode` tree. Both the C++26 reflection path and manual `view()` path converge at the Node level. This makes PRISM work on C++23 compilers (GCC 14+, Clang 17+) with no API difference — the only pre-C++26 requirement is that model structs must provide `view()`.

## Motivation

PRISM currently requires GCC 16 with `-freflection` (P2996) to auto-introspect model structs. This limits adoption to a single experimental compiler. The reflection surface is small (4 call sites), and the `WidgetNode` it produces is already type-erased. By inserting a `Node` layer between "user describes UI" and "WidgetTree runs it," both paths share a single construction pipeline. Reflection becomes sugar, not a requirement.

Secondary benefit: the Node tree is the foundation for Phase 3.6's dynamic UI features (runtime tree mutation, JSON-driven editors, Python bindings).

## Design

### Node Class

`Node` is a type-erased intermediate node. Leaf nodes capture their concrete `Field<T>` type in a closure; container nodes carry children and a `LayoutKind`.

```cpp
// include/prism/core/node.hpp

class Node {
public:
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    // Type-erased WidgetNode builder (leaf nodes only)
    std::function<void(WidgetNode&, WidgetTree&)> build_widget;

    // Type-erased change subscription (leaf nodes only, for dirty tracking)
    std::function<Connection(std::function<void()>)> on_change;
};
```

### Leaf Node Factory

`node_leaf<T>()` is the only function that sees the concrete type. It captures the `Field<T>&` in closures:

```cpp
template <typename T>
Node node_leaf(Field<T>& field) {
    Node n;
    n.is_leaf = true;
    n.build_widget = [&field](WidgetNode& wn, WidgetTree& tree) {
        wn.focus_policy = Delegate<T>::focus_policy;
        wn.record = [&field](WidgetNode& node) {
            node.draws.clear();
            node.overlay_draws.clear();
            Delegate<T>::record(node.draws, field, node);
        };
        wn.record(wn);
        wn.wire = [&field](WidgetNode& node) {
            node.connections.push_back(
                node.on_input.connect([&field, &node](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev, node);
                })
            );
        };
    };
    n.on_change = [&field](std::function<void()> cb) {
        return field.on_change().connect([cb](const T&) { cb(); });
    };
    return n;
}
```

Same logic as today's `build_leaf<T>()`, just split into a two-phase construction: first create the Node (type-erased), then convert to WidgetNode (generic).

### Canvas Node Factory

Canvas nodes follow the same pattern — type-erased via closures:

```cpp
template <HasCanvas T>
Node node_canvas(T& model) {
    Node n;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;
    n.build_widget = [&model](WidgetNode& wn, WidgetTree&) {
        wn.record = [&model](WidgetNode& node) {
            node.draws.clear();
            model.canvas(node.draws, node.canvas_bounds, node);
        };
        wn.record(wn);

        if constexpr (HasCanvasInput<T>) {
            wn.focus_policy = FocusPolicy::tab_and_click;
            wn.wire = [&model](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&model, &node](const InputEvent& ev) {
                        model.handle_canvas_input(ev, node, node.canvas_bounds);
                    })
                );
            };
        }
    };
    return n;
}
```

`ViewBuilder::canvas()` calls `node_canvas()` and returns a `CanvasHandle` that supports `.depends_on(field)` — same API as today. The `CanvasHandle` stores a reference to the Node's `on_change` list for deferred dirty wiring.

### Node → WidgetNode Conversion

A single recursive function replaces today's `build_container`/`build_leaf` split:

```cpp
WidgetNode build_widget_node(Node& node, WidgetTree& tree) {
    WidgetNode wn;
    wn.id = node.id;
    wn.layout_kind = node.layout_kind;

    if (node.is_leaf) {
        wn.is_container = false;
        node.build_widget(wn, tree);
        // Connect dirty tracking
        if (node.on_change) {
            auto id = wn.id;
            wn.connections.push_back(
                node.on_change([&tree, id]() { tree.mark_dirty(id); })
            );
        }
    } else {
        wn.is_container = true;
        for (auto& child : node.children)
            wn.children.push_back(build_widget_node(child, tree));
    }

    return wn;
}
```

### ViewBuilder Targets Node

ViewBuilder methods create `Node`s instead of `WidgetNode`s:

```cpp
class ViewBuilder {
    Node& target_;
    std::vector<Node*> stack_;

    Node& current_parent() {
        return stack_.empty() ? target_ : *stack_.back();
    }

public:
    ViewBuilder(Node& target) : target_(target) {}

    template <typename T>
    void widget(Field<T>& field) {
        current_parent().children.push_back(node_leaf(field));
    }

    template <typename C>
    void component(C& comp);  // see Dispatch section

    void row(std::invocable auto&& fn) {
        Node container;
        container.layout_kind = LayoutKind::Row;
        auto& ref = current_parent().children.emplace_back(std::move(container));
        stack_.push_back(&ref);
        fn();
        stack_.pop_back();
    }

    void column(std::invocable auto&& fn) {
        Node container;
        container.layout_kind = LayoutKind::Column;
        auto& ref = current_parent().children.emplace_back(std::move(container));
        stack_.push_back(&ref);
        fn();
        stack_.pop_back();
    }

    void spacer() {
        Node s;
        s.is_leaf = true;
        s.layout_kind = LayoutKind::Spacer;
        current_parent().children.push_back(std::move(s));
    }

    template <HasCanvas T>
    auto canvas(T& model);  // returns CanvasHandle, same API as today

    void finalize();  // implicit Column wrap if multiple top-level items
};
```

### Dispatch: build_node_tree

The single `#if` guard point for reflection:

```cpp
template <typename Model>
Node build_node_tree(Model& model, WidgetTree& tree) {
    Node root;
    root.is_leaf = false;

    if constexpr (has_view<Model>) {
        ViewBuilder vb{root};
        model.view(vb);
        vb.finalize();
    }
#if __cpp_reflection
    else {
        // Reflection walk: iterate struct members, produce Node children
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_state_v<M>) {
                // skip
            } else if constexpr (is_field_v<M>) {
                root.children.push_back(node_leaf(member));
            } else if constexpr (is_component_v<M>) {
                root.children.push_back(build_node_tree(member, tree));
            }
        }
    }
#else
    else {
        static_assert(has_view<Model>,
            "Pre-C++26: model structs must provide a view() method");
    }
#endif

    return root;
}
```

### ViewBuilder::component Dispatch

Same pattern — `has_view` takes priority, reflection fallback on C++26:

```cpp
template <typename C>
void ViewBuilder::component(C& comp) {
    if constexpr (has_view<C>) {
        Node container;
        container.layout_kind = LayoutKind::Default;
        ViewBuilder sub{container};
        comp.view(sub);
        sub.finalize();
        current_parent().children.push_back(std::move(container));
    }
#if __cpp_reflection
    else if constexpr (is_component_v<C>) {
        // Reflection: build_node_tree for sub-component
        current_parent().children.push_back(build_node_tree(comp, /* tree ref */));
    }
#else
    else {
        static_assert(has_view<C>,
            "Pre-C++26: components passed to vb.component() must provide a view() method");
    }
#endif
}
```

### WidgetTree Constructor

```cpp
template <typename Model>
WidgetTree(Model& model) {
    Node node_tree = build_node_tree(model, *this);
    root_ = build_widget_node(node_tree, *this);
    build_index(root_);
}
```

### Enum Support: magic_enum Fallback

The second `#if` guard location. Same API surface, different backend:

```cpp
#if __cpp_reflection

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
    // P2996 template for loop
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    // P2996 template for loop
}

#else

template <ScopedEnum T>
constexpr size_t enum_count() {
    return magic_enum::enum_count<T>();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    auto values = magic_enum::enum_values<T>();
    return std::string(magic_enum::enum_name(values[index]));
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    return magic_enum::enum_index(value).value();
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    return magic_enum::enum_values<T>()[index];
}

#endif
```

### reflect.hpp Guard

The entire file is guarded:

```cpp
#if __cpp_reflection
// is_component_v, for_each_field, for_each_member, check_unplaced_fields
// ...
#endif
```

These utilities are only used by the reflection path in `build_node_tree` and `ViewBuilder::component`.

## What Changes

| File | Change |
|---|---|
| `include/prism/core/node.hpp` | **New.** `Node` class, `node_leaf<T>()`, `build_widget_node()` |
| `include/prism/core/widget_tree.hpp` | `ViewBuilder` targets `Node`, `build_node_tree()` replaces `build_container()`/`build_leaf()`, constructor updated |
| `include/prism/core/delegate.hpp` | Enum helpers get `#if` branch with magic_enum fallback |
| `include/prism/core/reflect.hpp` | Entire file guarded behind `#if __cpp_reflection` |
| `meson.build` | magic_enum wrap (conditional on `__cpp_reflection` absence) |
| `tests/test_node.cpp` | **New.** Node tree construction and conversion tests |

## What Stays Unchanged

- `WidgetNode` struct — identical
- `Field<T>`, `State<T>`, `SenderHub`, `Connection` — untouched
- All `Delegate<T>` specializations — untouched
- `build_index()`, `refresh_dirty()`, `dispatch()`, `build_snapshot()` — untouched
- Layout, hit-test, focus management — untouched
- `DrawList`, `SceneSnapshot`, all backends — untouched
- `model_app()` — untouched (takes `Model&`, calls `WidgetTree(model)`)
- Canvas escape hatch — untouched (ViewBuilder::canvas targets Node)
- stdexec integration — untouched

## User-Facing API

**Identical on both paths.** Same `model_app()`, `Field<T>`, `view()`, `vb.widget()`, `vb.component()`, `vb.canvas()` signatures.

**Pre-C++26 only requirement:** every model struct and sub-component must provide `view()`. Without it, `static_assert` fires at compile time with a clear message.

## Testing

### New: test_node.cpp

1. **node_leaf** — creates Node from `Field<T>`, verify `build_widget` and `on_change` are set
2. **Node → WidgetNode** — leaf conversion preserves focus_policy, record produces draws
3. **Container conversion** — Node with children → WidgetNode with children, layout kinds preserved
4. **Row/Column/Spacer** — correct LayoutKind propagation through conversion
5. **Nested components** — component Node subtree converts correctly
6. **Dirty tracking** — `on_change` callback triggers through Node → WidgetNode connection
7. **Canvas node** — canvas Node converts to WidgetNode with expand layout

### Existing tests

All existing tests updated to work through the Node layer. Same assertions — the WidgetNode output is identical. Tests that construct models with `view()` should pass with zero logic changes (only internal construction path differs).

### Pre-C++26 CI

Verify that the codebase compiles and all tests pass with GCC 14 or Clang 17 (no `-freflection`). Models in tests that lack `view()` need one added. This is a CI configuration concern, not a code change.

## Out of Scope

- Runtime tree mutation (`add_leaf`/`remove_subtree`) — Phase 3.6 future work, Node provides the foundation
- `Node` exposed as public API for dynamic UIs — future work
- Python bindings via Node — future work (Phase 5)
- `check_unplaced_fields` debug diagnostic on pre-C++26 — no reflection to check against
- Word-wrap, selection, undo in TextArea — unrelated
