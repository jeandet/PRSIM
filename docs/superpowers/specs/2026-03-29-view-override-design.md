# Custom view() Override on Model Structs

> **Goal:** Allow model structs to define a `view(ViewBuilder&)` method that controls widget layout — replacing the default reflection-driven vertical stack with user-specified Row/Column/Spacer nesting — while preserving all MVB wiring (dirty marking, focus order, event dispatch).

## Motivation

Today `WidgetTree` reflects over a model struct and produces a flat vertical stack of widgets — one per `Field<T>`, in declaration order. This is fine for prototyping but useless for real applications where fields need side-by-side layout, grouped sections, spacers, or deliberate omission from the UI.

The `view()` override lets a component declare its own internal layout at construction time. It runs once (not per-frame), defining the persistent widget tree structure. All MVB wiring (delegates, on_change → dirty, focus, input dispatch) works identically — only the tree shape changes.

## Design Decisions

- **Construction-time only** — `view()` runs once when the WidgetTree is built. No per-frame re-evaluation. Conditional visibility (show/hide) is a separate concern (future `visible` property on WidgetNode).
- **Lambda-based nesting** — `vb.row([&]{...})` / `vb.column([&]{...})` for layout containers. Nesting is visually clear, impossible to forget closing calls.
- **Omitting fields is permitted** — a `Field<T>` not placed by `view()` has no widget but remains reactive and connectable. A debug diagnostic warns about unplaced fields in debug builds.
- **Sub-component embedding** — `vb.component(sub_struct)` embeds a sub-component's widget subtree. The sub-component uses its own `view()` if it has one, or default reflection otherwise.
- **Component owns its layout** — a component's `view()` defines its own top-level container kind. The parent sees it as an opaque box. If `view()` produces multiple top-level items without a wrapper, they are implicitly wrapped in a Column.
- **ViewBuilder is a nested class of WidgetTree** — it needs direct access to `build_leaf()`, `build_container()`, `next_id_`, etc. No reason for it to exist outside the tree.

## Detection

### `has_view` concept

```cpp
template <typename T>
concept has_view = requires(T t, WidgetTree::ViewBuilder vb) {
    t.view(vb);
};
```

Checked at compile time in `build_container()`. Zero runtime cost. Existing code without `view()` keeps working identically.

## ViewBuilder

### Class definition (nested inside WidgetTree)

```cpp
class WidgetTree {
public:
    class ViewBuilder {
        WidgetTree& tree_;
        WidgetNode& target_;
        std::vector<WidgetNode*> stack_;  // parent stack for nesting
        std::set<const void*> placed_;    // addresses of placed fields (debug)

        WidgetNode& current_parent() {
            return stack_.empty() ? target_ : *stack_.back();
        }

    public:
        ViewBuilder(WidgetTree& tree, WidgetNode& target);

        template <typename T>
        void widget(Field<T>& field);

        template <typename C>
        void component(C& comp);

        void row(std::invocable auto&& fn);
        void column(std::invocable auto&& fn);
        void spacer();

        void finalize();
    };
    // ...
};
```

### Method behavior

**`widget(Field<T>& field)`** — calls `tree_.build_leaf(field)`, appends the resulting WidgetNode to `current_parent().children`. Records `&field` in `placed_` for debug diagnostic.

**`component(C& comp)`** — calls `tree_.build_container(comp)` (recursively uses `comp.view()` if it has one, or default reflection if not). Appends the resulting container node to `current_parent().children`.

**`row(fn)` / `column(fn)`** — creates a new container WidgetNode tagged with `LayoutKind::Row` / `LayoutKind::Column`, pushes it onto `stack_`, calls `fn()`, pops the stack, appends the container to the previous parent.

**`spacer()`** — creates a non-container WidgetNode tagged with `LayoutKind::Spacer`. No field, no delegate, no wiring.

**`finalize()`** — if `target_` has exactly one child, done (the component's layout kind comes from that child). If multiple direct children, wraps them in a new Column container node. This enforces "component is an opaque box."

## WidgetNode Changes

### New field: `layout_kind`

```cpp
struct WidgetNode {
    // ... existing fields ...
    enum class LayoutKind { Default, Row, Column, Spacer } layout_kind = LayoutKind::Default;
};
```

- `Default` — current behavior (container flattens into parent's layout)
- `Row` / `Column` — created by `vb.row()` / `vb.column()`
- `Spacer` — created by `vb.spacer()`

## Modified `build_container()`

```cpp
template <typename Model>
WidgetNode build_container(Model& model) {
    WidgetNode container;
    container.id = next_id_++;
    container.is_container = true;

    if constexpr (has_view<Model>) {
        ViewBuilder vb{*this, container};
        model.view(vb);
        check_unplaced_fields(model, vb.placed_);  // debug only
        vb.finalize();
    } else {
        // existing reflection walk — unchanged
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_state_v<M>) {
            } else if constexpr (is_field_v<M>) {
                container.children.push_back(build_leaf(member));
            } else if constexpr (is_component_v<M>) {
                container.children.push_back(build_container(member));
            }
        }
    }

    return container;
}
```

## Modified `build_layout()`

Currently `build_layout()` treats all containers as flat pass-throughs. With `LayoutKind`, it emits proper layout nesting:

```cpp
static void build_layout(WidgetNode& node, LayoutNode& parent) {
    using LK = WidgetNode::LayoutKind;

    if (!node.is_container) {
        if (node.layout_kind == LK::Spacer) {
            LayoutNode spacer;
            spacer.kind = LayoutNode::Kind::Spacer;
            spacer.id = node.id;
            parent.children.push_back(std::move(spacer));
        } else {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = node.id;
            leaf.draws = node.draws;
            leaf.overlay_draws = node.overlay_draws;
            parent.children.push_back(std::move(leaf));
        }
    } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
        LayoutNode container;
        container.kind = (node.layout_kind == LK::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        container.id = node.id;
        for (auto& c : node.children)
            build_layout(c, container);
        parent.children.push_back(std::move(container));
    } else {
        // Default: flatten children into parent (current behavior)
        for (auto& c : node.children)
            build_layout(c, parent);
    }
}
```

## Modified `build_snapshot()`

The root container's layout kind is respected:

```cpp
LayoutNode layout;
layout.kind = (root_.layout_kind == LayoutKind::Row)
    ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
layout.id = root_.id;
build_layout(root_, layout);
```

Root with `view()` that wraps in `vb.row()` → Row at top level. Default or multiple unwrapped items → Column. Everything else (measure, arrange, flatten) unchanged.

## Debug Diagnostic for Unplaced Fields

In `build_container()`, after `view()` returns, a debug-only check iterates the model's `Field<T>` members via reflection and compares addresses against `ViewBuilder::placed_`:

```cpp
template <typename Model>
void check_unplaced_fields([[maybe_unused]] Model& model,
                           [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M>) {
            if (!placed.contains(&member)) {
                std::fprintf(stderr, "[prism] warning: Field '%s' in %s not placed by view()\n",
                    std::meta::identifier_of(m).data(),
                    std::meta::identifier_of(^^Model).data());
            }
        }
    }
#endif
}
```

Active only in debug builds (`NDEBUG` not defined). Silent in release.

## User-Facing API

### Model with view()

```cpp
struct Dashboard {
    Field<std::string> name;
    Field<bool> dark_mode;
    Field<std::string> description;

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(name);
            vb.spacer();
            vb.widget(dark_mode);
        });
        vb.widget(description);
    }
};
```

### Model with sub-components

```cpp
struct Sidebar {
    Field<std::string> search;
    Field<bool> filter_active;
};

struct App {
    Sidebar sidebar;
    Field<int> selected;

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.component(sidebar);
            vb.column([&] {
                vb.widget(selected);
            });
        });
    }
};
```

### Model without view() — unchanged

```cpp
struct Simple {
    Field<std::string> name;
    Field<bool> active;
};
// Default vertical stack, same as today
```

## File Changes

| File | Change |
|---|---|
| `include/prism/core/widget_tree.hpp` | Add `LayoutKind` to `WidgetNode`, add `ViewBuilder` nested class, modify `build_container()`, `build_layout()`, `build_snapshot()`, add `check_unplaced_fields()` |
| `tests/test_widget_tree.cpp` | Add view() tests (or new `test_view.cpp` if cleaner) |
| `tests/meson.build` | Add test file if new |

No changes to: `delegate.hpp`, `layout.hpp`, `reflect.hpp`, `model_app.hpp`, `draw_list.hpp`, `scene_snapshot.hpp`, or any backend code.

## Testing

1. **Default behavior unchanged** — model without `view()` produces same tree as today
2. **`view()` with `widget()` only** — fields placed in custom order, verify leaf order matches
3. **`view()` with `row()`** — verify LayoutNode tree has Row kind with correct children
4. **Nested `row()` inside `column()`** — verify layout nesting
5. **`view()` with `spacer()`** — spacer node in layout
6. **`view()` with `component()`** — sub-component builds its own subtree
7. **`component()` delegates to sub-component's `view()`** — recursive view override
8. **`component()` with default reflection** — sub-component without `view()` uses reflection walk
9. **Omitted field** — no widget, field still reactive (set() works, no crash)
10. **Implicit Column wrap** — multiple top-level items → wrapped in Column
11. **Single top-level container** — one `row()` call, no extra Column wrap
12. **Focus order** — focusable widgets placed by `view()` appear in focus order
13. **Dirty propagation** — `field.set()` on a `view()`-placed widget marks it dirty
14. **Debug diagnostic** — unplaced field produces stderr warning (debug build, test via capture)

## Out of Scope

- Conditional visibility (`visible` property on WidgetNode) — future feature
- Per-frame re-evaluation of `view()` — construction-time only
- `canvas()` escape hatch for raw DrawList — separate spec
- Custom `SizeHint` / min/max constraints on containers — future layout enhancement
