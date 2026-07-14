# Node Tree as Universal Construction Layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Insert a `Node` intermediate layer between user-defined UI and `WidgetNode`, making PRISM compile on C++23 compilers without P2996 reflection.

**Architecture:** `ViewBuilder` and reflection both build a `Node` tree (type-erased). A single `build_widget_node()` pass converts `Node` → `WidgetNode`. Reflection is guarded behind `#if __cpp_reflection`. Enum introspection falls back to magic_enum on pre-C++26.

**Tech Stack:** C++23/26, meson, doctest, magic_enum (new wrap, conditional)

**Spec:** `docs/superpowers/specs/2026-03-29-node-tree-pre-cpp26-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `include/prism/core/node.hpp` | **New.** `Node` struct, `node_leaf<T>()`, `node_canvas<T>()`, `build_widget_node()` |
| `include/prism/core/widget_tree.hpp` | **Modify.** `ViewBuilder` targets `Node` instead of `WidgetNode`. `build_container`/`build_leaf` replaced by `build_node_tree`. Constructor updated. `check_unplaced_fields` guarded. |
| `include/prism/core/delegate.hpp` | **Modify.** Enum helpers (`enum_count`, `enum_label`, `enum_index`, `enum_from_index`) get `#if __cpp_reflection` / magic_enum branch. Remove `#include <meta>` on pre-C++26. |
| `include/prism/core/reflect.hpp` | **Modify.** Entire file guarded behind `#if __cpp_reflection`. `is_field_v` and `is_state_v` extracted to a new location (they don't use reflection). |
| `include/prism/core/traits.hpp` | **New.** `is_field_v`, `is_state_v` — pure SFINAE, no reflection dependency. |
| `tests/test_node.cpp` | **New.** Node construction and conversion tests. |
| `tests/meson.build` | **Modify.** Add `test_node.cpp`. |
| `subprojects/magic_enum.wrap` | **New.** magic_enum meson wrap. |
| `src/meson.build` | **Modify.** Conditional magic_enum dependency. |

---

### Task 1: Extract `is_field_v` and `is_state_v` into `traits.hpp`

These traits are pure SFINAE — no reflection needed. They must be usable by `node.hpp` which shouldn't include `reflect.hpp`.

**Files:**
- Create: `include/prism/core/traits.hpp`
- Modify: `include/prism/core/reflect.hpp`

- [ ] **Step 1: Write the failing test**

No new test needed — existing `test_reflect.cpp` and `test_widget_tree.cpp` already exercise `is_field_v`/`is_state_v`. We verify they still pass after the move.

- [ ] **Step 2: Create `traits.hpp`**

```cpp
// include/prism/core/traits.hpp
#pragma once

#include <type_traits>

namespace prism {

// Detect Field<T>: has is_prism_field = true and on_change()
template <typename T>
struct is_field : std::false_type {};

template <typename T>
    requires requires { { T::is_prism_field } -> std::convertible_to<bool>; }
    && T::is_prism_field
    && requires(T t) { t.on_change(); }
struct is_field<T> : std::true_type {};

template <typename T>
inline constexpr bool is_field_v = is_field<T>::value;

// Detect State<T>: has .value and on_change() but NOT is_prism_field
template <typename T>
struct is_state : std::false_type {};

template <typename T>
    requires requires { typename std::remove_cvref_t<decltype(std::declval<T>().value)>; }
    && requires(T t) { t.on_change(); }
    && (!requires { { T::is_prism_field } -> std::convertible_to<bool>; })
struct is_state<T> : std::true_type {};

template <typename T>
inline constexpr bool is_state_v = is_state<T>::value;

} // namespace prism
```

- [ ] **Step 3: Update `reflect.hpp` to include `traits.hpp` and remove duplicates**

Replace the `is_field` / `is_state` definitions in `reflect.hpp` with `#include <prism/core/traits.hpp>`. Keep everything else (the reflection-dependent code) in `reflect.hpp`.

```cpp
// include/prism/core/reflect.hpp
#pragma once

#include <prism/core/traits.hpp>

#if __cpp_reflection
#include <meta>

namespace prism {

// Visit all Field<T> members of a struct (non-recursive)
template <typename Model, typename Fn>
void for_each_field(Model& model, Fn&& fn) {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M>) {
            fn(member);
        }
    }
}

// Detect a component: struct with Field<T> members (direct or one level nested)
template <typename T>
consteval bool check_is_component() {
    if constexpr (!std::is_class_v<T>) return false;
    else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (is_field_v<M> || is_state_v<M>) found = true;
        }
        return found;
    }
}

template <typename T>
consteval bool check_is_component_recursive() {
    if (check_is_component<T>()) return true;
    if constexpr (!std::is_class_v<T>) return false;
    else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (std::is_class_v<M> && check_is_component<M>()) found = true;
        }
        return found;
    }
}

template <typename T>
inline constexpr bool is_component_v = check_is_component_recursive<T>();

// Visit all members (Field<T> and sub-components) of a struct
template <typename Model, typename Fn>
void for_each_member(Model& model, Fn&& fn) {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M> || is_component_v<M>) {
            fn(member);
        }
    }
}

} // namespace prism

#endif // __cpp_reflection
```

- [ ] **Step 4: Run all tests to verify nothing broke**

Run: `meson test -C builddir`
Expected: All existing tests pass. `is_field_v` / `is_state_v` now come from `traits.hpp` via `reflect.hpp`.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/traits.hpp include/prism/core/reflect.hpp
git commit -m "refactor: extract is_field_v/is_state_v into traits.hpp (no reflection needed)"
```

---

### Task 2: Create `node.hpp` with `Node`, `node_leaf<T>()`, and `build_widget_node()`

**Files:**
- Create: `include/prism/core/node.hpp`
- Create: `tests/test_node.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

This test file is created in Task 2 but will only fully compile after Tasks 3 and 4 (LayoutKind extraction and build_widget_node). The node_leaf-only tests will compile after Task 3.

```cpp
// tests/test_node.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>

TEST_CASE("node_leaf creates a leaf Node from Field<int>") {
    prism::Field<int> count{0};
    prism::WidgetId next_id = 1;
    auto node = prism::node_leaf(count, next_id);
    CHECK(node.is_leaf);
    CHECK(node.id == 1);
    CHECK(next_id == 2);
    CHECK(node.build_widget != nullptr);
    CHECK(node.on_change != nullptr);
}

TEST_CASE("node_leaf creates a leaf Node from Field<bool>") {
    prism::Field<bool> flag{false};
    prism::WidgetId next_id = 1;
    auto node = prism::node_leaf(flag, next_id);
    CHECK(node.is_leaf);
    CHECK(node.build_widget != nullptr);
}

TEST_CASE("node_leaf increments next_id") {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    prism::WidgetId next_id = 10;
    prism::node_leaf(a, next_id);
    prism::node_leaf(b, next_id);
    CHECK(next_id == 12);
}

TEST_CASE("node_leaf on_change fires when field changes") {
    prism::Field<int> count{0};
    prism::WidgetId next_id = 1;
    auto node = prism::node_leaf(count, next_id);

    bool fired = false;
    auto conn = node.on_change([&fired]() { fired = true; });
    count.set(42);
    CHECK(fired);
}

TEST_CASE("Node tree through WidgetTree produces same leaf count") {
    struct Model {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(a);
            vb.widget(b);
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("Node tree dirty tracking works through WidgetTree") {
    struct Model {
        prism::Field<int> val{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(val);
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());
    model.val.set(99);
    CHECK(tree.any_dirty());
}

TEST_CASE("Node tree with Row layout through WidgetTree") {
    struct Model {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.row([&] {
                vb.widget(a);
                vb.widget(b);
            });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);
    // Row: same y, different x
    auto& [id0, r0] = snap->geometry[0];
    auto& [id1, r1] = snap->geometry[1];
    CHECK(r0.origin.y.raw() == r1.origin.y.raw());
    CHECK(r1.origin.x.raw() > r0.origin.x.raw());
}

TEST_CASE("Node tree with spacer through WidgetTree") {
    struct Model {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.row([&] {
                vb.widget(a);
                vb.spacer();
                vb.widget(b);
            });
        }
    };
    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);
}

TEST_CASE("Node tree with nested component through WidgetTree") {
    struct Inner {
        prism::Field<int> x{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(x);
        }
    };
    struct Outer {
        Inner inner;
        prism::Field<int> y{0};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.component(inner);
            vb.widget(y);
        }
    };
    Outer model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("Canvas node depends_on tracks multiple fields") {
    struct CanvasModel {
        prism::Field<int> a{0};
        prism::Field<int> b{0};
        void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
            dl.filled_rect(bounds, prism::Color::rgba(255, 0, 0));
        }
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.canvas(*this).depends_on(a).depends_on(b);
        }
    };
    CanvasModel model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();

    model.a.set(1);
    CHECK(tree.any_dirty());
    tree.clear_dirty();

    model.b.set(1);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```
  'node' : files('test_node.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir node`
Expected: FAIL — `node.hpp` doesn't exist yet.

- [ ] **Step 4: Write `node.hpp`**

`node.hpp` defines `Node`, `node_leaf<T>()`, and `node_canvas<T>()`. It does NOT contain `build_widget_node()` — that goes in `widget_tree.hpp` (Task 4) because it needs the full `WidgetNode` type. This mirrors how delegate definitions are split between `delegate.hpp` and `widget_tree.hpp`.

`node.hpp` depends on `LayoutKind` being at namespace scope (done in Task 3) so it can use it without including `widget_tree.hpp`.

```cpp
// include/prism/core/node.hpp
#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace prism {

struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    // Type-erased WidgetNode builder (leaf nodes only).
    std::function<void(WidgetNode&)> build_widget;

    // Type-erased change subscription (leaf nodes only).
    std::function<Connection(std::function<void()>)> on_change;

    // Multiple dependencies for canvas nodes with depends_on().
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;
};

template <typename T>
Node node_leaf(Field<T>& field, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;

    n.build_widget = [&field](WidgetNode& wn) {
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

    n.on_change = [&field](std::function<void()> cb) -> Connection {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}

template <typename T>
    requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
        t.canvas(dl, r, n);
    }
Node node_canvas(T& model, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;
    n.layout_kind = LayoutKind::Canvas;

    n.build_widget = [&model](WidgetNode& wn) {
        wn.record = [&model](WidgetNode& node) {
            node.draws.clear();
            model.canvas(node.draws, node.canvas_bounds, node);
        };
        wn.record(wn);

        if constexpr (requires(T& t, const InputEvent& ev, WidgetNode& nd, Rect r) {
                           t.handle_canvas_input(ev, nd, r);
                       }) {
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

} // namespace prism
```

**Dependency note:** `node.hpp` requires Task 3 (LayoutKind at namespace scope) to be completed first.

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir node`
Expected: All 7 test cases PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/node.hpp tests/test_node.cpp tests/meson.build
git commit -m "feat: add Node intermediate layer with node_leaf and build_widget_node"
```

---

### Task 3: Extract `LayoutKind` to namespace scope

`WidgetNode::LayoutKind` is used by `Node`, `ViewBuilder`, `build_layout`, and `build_snapshot`. Moving it to namespace scope lets `node.hpp` use it without including `widget_tree.hpp`.

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `include/prism/core/node.hpp` (update `using` alias)

- [ ] **Step 1: Move `LayoutKind` out of `WidgetNode`**

In `widget_tree.hpp`, before the `WidgetNode` struct, define:

```cpp
enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas };
```

In `WidgetNode`, replace the nested enum with:

```cpp
struct WidgetNode {
    // ...
    LayoutKind layout_kind = LayoutKind::Default;
    // ...
};
```

- [ ] **Step 2: Update all references from `WidgetNode::LayoutKind` to `LayoutKind`**

Search for `WidgetNode::LayoutKind` and `LK::` usage in `widget_tree.hpp`. Replace:
- `WidgetNode::LayoutKind::Row` → `LayoutKind::Row` (etc.)
- `using LK = WidgetNode::LayoutKind;` → `using LK = LayoutKind;` (or remove the alias since it's already at namespace scope)

Also update any test files that reference `prism::WidgetNode::LayoutKind`.

- [ ] **Step 3: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass. Pure rename, no behavior change.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: extract LayoutKind to namespace scope for use by Node"
```

---

### Task 4: Rewrite `ViewBuilder` to target `Node`

This is the core change. `ViewBuilder` methods create `Node`s instead of `WidgetNode`s. `build_widget_node()` is added to `WidgetTree` (private). `build_container`/`build_leaf` are replaced.

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Add `#include <prism/core/node.hpp>` to `widget_tree.hpp`**

Add after the existing includes:

```cpp
#include <prism/core/node.hpp>
```

- [ ] **Step 2: Add `build_widget_node` as a private method of `WidgetTree`**

Inside `WidgetTree`, in the `private:` section, add:

```cpp
    static WidgetNode build_widget_node(Node& node) {
        WidgetNode wn;
        wn.id = node.id;
        wn.layout_kind = node.layout_kind;

        if (node.is_leaf) {
            wn.is_container = false;
            if (node.build_widget)
                node.build_widget(wn);
        } else {
            wn.is_container = true;
            for (auto& child : node.children)
                wn.children.push_back(build_widget_node(child));
        }

        return wn;
    }
```

- [ ] **Step 3: Add `connect_dirty` private method**

After `build_widget_node`, add a method that walks the Node tree and connects `on_change` → `mark_dirty`:

```cpp
    void connect_dirty(Node& node, WidgetNode& wn) {
        if (node.is_leaf && node.on_change) {
            auto id = wn.id;
            wn.connections.push_back(
                node.on_change([this, id]() { mark_dirty(root_, id); })
            );
        }
        if (!node.is_leaf) {
            assert(node.children.size() == wn.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
                connect_dirty(node.children[i], wn.children[i]);
        }
    }
```

- [ ] **Step 4: Add `build_node_tree` private method**

This is where the `#if __cpp_reflection` guard lives:

```cpp
    template <typename Model>
    Node build_node_tree(Model& model) {
        Node root;
        root.id = next_id_++;
        root.is_leaf = false;

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
            ViewBuilder vb{*this, root};
            model.view(vb);
#if __cpp_reflection
            check_unplaced_fields(model, vb.placed());
#endif
            vb.finalize();
        }
#if __cpp_reflection
        else {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));

            template for (constexpr auto m : members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;

                if constexpr (is_state_v<M>) {
                    // invisible observable — no widget
                } else if constexpr (is_field_v<M>) {
                    root.children.push_back(node_leaf(member, next_id_));
                } else if constexpr (is_component_v<M>) {
                    root.children.push_back(build_node_tree(member));
                }
            }
        }
#else
        else {
            static_assert(requires(Model& m, ViewBuilder& vb) { m.view(vb); },
                "Pre-C++26: model structs must provide a view() method");
        }
#endif

        return root;
    }
```

- [ ] **Step 5: Rewrite `ViewBuilder` to target `Node`**

Replace the existing `ViewBuilder` class with:

```cpp
    class ViewBuilder {
        WidgetTree& tree_;
        Node& target_;
        std::vector<Node*> stack_;
        std::set<const void*> placed_;

        Node& current_parent() {
            return stack_.empty() ? target_ : *stack_.back();
        }

    public:
        struct CanvasHandle {
            Node& node_ref;
            WidgetTree& tree_ref;

            template <typename U>
            CanvasHandle& depends_on(Field<U>& field) {
                auto id = node_ref.id;
                // Store a pending on_change that will be connected during connect_dirty
                auto existing = std::move(node_ref.on_change);
                node_ref.on_change = [&field, existing = std::move(existing)]
                                     (std::function<void()> cb) -> Connection {
                    return field.on_change().connect(
                        [cb = std::move(cb)](const U&) { cb(); });
                };
                // We need multiple dependencies — use a vector approach instead.
                // Actually, canvas nodes store dependencies differently.
                // Let's use a simpler approach: store pending connections in the Node.
                return *this;
            }
        };

        ViewBuilder(WidgetTree& tree, Node& target)
            : tree_(tree), target_(target) {}

        template <typename T>
        void widget(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(node_leaf(field, tree_.next_id_));
        }

        [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }

        template <typename C>
        void component(C& comp) {
            if constexpr (requires(C& c, ViewBuilder& vb) { c.view(vb); }) {
                current_parent().children.push_back(tree_.build_node_tree(comp));
            }
#if __cpp_reflection
            else if constexpr (is_component_v<C>) {
                current_parent().children.push_back(tree_.build_node_tree(comp));
            }
#else
            else {
                static_assert(requires(C& c, ViewBuilder& vb) { c.view(vb); },
                    "Pre-C++26: components passed to vb.component() must provide a view() method");
            }
#endif
        }

        void row(std::invocable auto&& fn)    { push_container(LayoutKind::Row, fn); }
        void column(std::invocable auto&& fn) { push_container(LayoutKind::Column, fn); }

    private:
        void push_container(LayoutKind kind, std::invocable auto&& fn) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = kind;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            stack_.push_back(&parent.children.back());
            fn();
            stack_.pop_back();
        }

    public:
        void spacer() {
            Node s;
            s.id = tree_.next_id_++;
            s.is_leaf = true;
            s.layout_kind = LayoutKind::Spacer;
            current_parent().children.push_back(std::move(s));
        }

        template <typename T>
            requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
                t.canvas(dl, r, n);
            }
        auto canvas(T& model) {
            current_parent().children.push_back(node_canvas(model, tree_.next_id_));
            return CanvasHandle{current_parent().children.back(), tree_};
        }

        void finalize() {
            if (target_.children.size() > 1) {
                Node wrapper;
                wrapper.id = tree_.next_id_++;
                wrapper.is_leaf = false;
                wrapper.layout_kind = LayoutKind::Column;
                wrapper.children = std::move(target_.children);
                target_.children.clear();
                target_.children.push_back(std::move(wrapper));
            }
            if (target_.children.size() == 1) {
                auto lk = target_.children[0].layout_kind;
                if (lk == LayoutKind::Row || lk == LayoutKind::Column) {
                    target_.layout_kind = lk;
                    target_.children = std::move(target_.children[0].children);
                }
            }
        }
    };
```

- [ ] **Step 6: Handle CanvasHandle.depends_on properly**

The `CanvasHandle::depends_on` needs to support multiple fields. Since `Node::on_change` is a single function, canvas nodes need a different approach. Add a `std::vector` of on_change functions to `Node`:

In `node.hpp`, update `Node`:

```cpp
struct Node {
    WidgetId id = 0;
    bool is_leaf = false;
    LayoutKind layout_kind = LayoutKind::Default;
    std::vector<Node> children;

    std::function<void(WidgetNode&)> build_widget;

    // Single on_change for regular leaf nodes
    std::function<Connection(std::function<void()>)> on_change;

    // Multiple on_change for canvas nodes with depends_on
    std::vector<std::function<Connection(std::function<void()>)>> dependencies;
};
```

Then `CanvasHandle::depends_on` becomes:

```cpp
template <typename U>
CanvasHandle& depends_on(Field<U>& field) {
    node_ref.dependencies.push_back(
        [&field](std::function<void()> cb) -> Connection {
            return field.on_change().connect(
                [cb = std::move(cb)](const U&) { cb(); });
        }
    );
    return *this;
}
```

And `connect_dirty` handles both:

```cpp
    void connect_dirty(Node& node, WidgetNode& wn) {
        if (node.is_leaf) {
            auto id = wn.id;
            if (node.on_change) {
                wn.connections.push_back(
                    node.on_change([this, id]() { mark_dirty(root_, id); })
                );
            }
            for (auto& dep : node.dependencies) {
                wn.connections.push_back(
                    dep([this, id]() { mark_dirty(root_, id); })
                );
            }
        } else {
            assert(node.children.size() == wn.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
                connect_dirty(node.children[i], wn.children[i]);
        }
    }
```

- [ ] **Step 7: Update the `WidgetTree` constructor**

Replace:

```cpp
    template <typename Model>
    explicit WidgetTree(Model& model) {
        root_ = build_container(model);
        build_index(root_);
        clear_dirty();
    }
```

With:

```cpp
    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        root_ = build_widget_node(node_tree);
        connect_dirty(node_tree, root_);
        build_index(root_);
        clear_dirty();
    }
```

- [ ] **Step 8: Remove old `build_leaf` and `build_container`**

Delete the `build_leaf<T>()` and `build_container<Model>()` methods from `WidgetTree`'s private section. They are fully replaced by `build_node_tree` + `node_leaf` + `build_widget_node`.

- [ ] **Step 9: Run all tests**

Run: `meson test -C builddir`
Expected: ALL existing tests pass. The Node layer is transparent — same WidgetNode output, same behavior.

- [ ] **Step 10: Commit**

```bash
git add include/prism/core/node.hpp include/prism/core/widget_tree.hpp
git commit -m "feat: rewrite ViewBuilder and WidgetTree to use Node intermediate layer"
```

---

### Task 5: Guard `reflect.hpp` and `check_unplaced_fields` behind `#if __cpp_reflection`

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `include/prism/core/reflect.hpp` (already done in Task 1, verify)

- [ ] **Step 1: Conditionally include `reflect.hpp` in `widget_tree.hpp`**

Replace:

```cpp
#include <prism/core/reflect.hpp>
```

With:

```cpp
#include <prism/core/traits.hpp>
#if __cpp_reflection
#include <prism/core/reflect.hpp>
#endif
```

- [ ] **Step 2: Guard `check_unplaced_fields` entirely**

Wrap the `check_unplaced_fields` method in `WidgetTree` with:

```cpp
#if __cpp_reflection
    template <typename Model>
    void check_unplaced_fields(...) {
        // ... existing code ...
    }
#endif
```

The call site in `build_node_tree` is already guarded (done in Task 4).

- [ ] **Step 3: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass (still compiling with `-freflection`, so both paths are active).

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: guard reflect.hpp and check_unplaced_fields behind __cpp_reflection"
```

---

### Task 6: Add magic_enum fallback for enum helpers

**Files:**
- Create: `subprojects/magic_enum.wrap`
- Modify: `src/meson.build`
- Modify: `include/prism/core/delegate.hpp`

- [ ] **Step 1: Create magic_enum meson wrap**

```ini
# subprojects/magic_enum.wrap
[wrap-file]
directory = magic_enum-0.9.7
source_url = https://github.com/Neargye/magic_enum/archive/refs/tags/v0.9.7.tar.gz
source_filename = magic_enum-0.9.7.tar.gz
source_hash = 3caab3aace7d14e074d1906be22fee245e5e7f74aaec38959e0e9b75a3dda1a4
patch_directory = magic_enum

[provide]
magic_enum = magic_enum_dep
```

Check the WrapDB for the exact hash. Alternatively use a simpler approach:

```ini
[wrap-git]
url = https://github.com/Neargye/magic_enum.git
revision = v0.9.7
depth = 1

[provide]
magic_enum = magic_enum_dep
```

- [ ] **Step 2: Add conditional magic_enum dependency in `src/meson.build`**

Add after `stdexec_dep`:

```meson
cpp = meson.get_compiler('cpp')
has_reflection = cpp.compiles('int main() { return __cpp_reflection; }', name : '__cpp_reflection check')

if not has_reflection
  magic_enum_dep = dependency('magic_enum', fallback : ['magic_enum', 'magic_enum_dep'])
else
  magic_enum_dep = dependency('', required : false)
endif

prism_core_dep = declare_dependency(
  include_directories : prism_inc,
  dependencies : [stdexec_dep, magic_enum_dep],
)
```

- [ ] **Step 3: Add `#if __cpp_reflection` / `#else` for enum helpers in `delegate.hpp`**

Replace the enum helper section (lines 94-136 of `delegate.hpp`) with:

```cpp
#if __cpp_reflection
#include <meta>

// Reflection helpers for scoped enums
template <ScopedEnum T>
consteval size_t enum_count() {
    return std::meta::enumerators_of(^^T).size();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    std::string result;
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = std::string(std::meta::identifier_of(e));
        ++i;
    }
    return result;
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

#else
#include <magic_enum/magic_enum.hpp>

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

Also remove the top-level `#include <meta>` from `delegate.hpp` (it's now inside the `#if` block).

- [ ] **Step 4: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass (compiling with `-freflection`, so the P2996 branch is used).

- [ ] **Step 5: Commit**

```bash
git add subprojects/magic_enum.wrap src/meson.build include/prism/core/delegate.hpp
git commit -m "feat: add magic_enum fallback for enum introspection on pre-C++26"
```

---

### Task 7: Make `-freflection` conditional in `meson.build`

**Files:**
- Modify: `meson.build`

- [ ] **Step 1: Replace unconditional `-freflection` with a check**

Replace:

```meson
add_project_arguments('-freflection', language : 'cpp')
```

With:

```meson
cpp = meson.get_compiler('cpp')
if cpp.has_argument('-freflection')
  add_project_arguments('-freflection', language : 'cpp')
endif
```

- [ ] **Step 2: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass (GCC 16 supports `-freflection`).

- [ ] **Step 3: Commit**

```bash
git add meson.build
git commit -m "build: make -freflection conditional on compiler support"
```

---

### Task 8: Add `view()` to test models that rely on reflection

When compiling without reflection, models without `view()` hit the `static_assert`. Add `view()` methods to test models that currently rely on auto-introspection.

**Files:**
- Modify: `tests/test_widget_tree.cpp`
- Modify: `tests/test_view.cpp` (RowModel)
- Modify: any other test files with models lacking `view()`

- [ ] **Step 1: Identify affected models**

Models in tests that don't have `view()` and rely on reflection:
- `test_widget_tree.cpp`: `SimpleModel`, `NestedModel`, `BoolModel`, `ModelWithState`, `SentinelModel`, `ButtonModel`, `FocusModel`
- `test_view.cpp`: `RowModel`

These need `view()` methods for the pre-C++26 path. Wrap the `view()` requirement in `#if`:

Actually — these tests still compile with reflection on GCC 16, so the existing auto-introspection path works. The `view()` additions are only needed for pre-C++26 CI. We can either:
1. Add `view()` to all test models (makes them work on both paths)
2. Guard test files with `#if __cpp_reflection` for reflection-only tests

Option 1 is simpler and more useful — it tests the `view()` path on all compilers.

- [ ] **Step 2: Add `view()` to `SimpleModel`**

```cpp
struct SimpleModel {
    prism::Field<int> count{0};
    prism::Field<std::string> name{"hi"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(count);
        vb.widget(name);
    }
};
```

- [ ] **Step 3: Add `view()` to `NestedModel`**

```cpp
struct NestedModel {
    SimpleModel inner;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.component(inner);
        vb.widget(flag);
    }
};
```

- [ ] **Step 4: Add `view()` to `BoolModel`**

```cpp
struct BoolModel {
    prism::Field<bool> flag{false};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(flag);
        vb.widget(count);
    }
};
```

- [ ] **Step 5: Add `view()` to `ModelWithState`**

```cpp
struct ModelWithState {
    prism::Field<int> visible{0};
    prism::State<int> hidden{0};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(visible);
        // hidden is State — not placed
        vb.widget(flag);
    }
};
```

- [ ] **Step 6: Add `view()` to `SentinelModel`**

```cpp
struct SentinelModel {
    prism::Field<prism::Label<>> status{{"OK"}};
    prism::Field<prism::Slider<>> volume{{.value = 0.5}};
    prism::Field<bool> enabled{true};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(status);
        vb.widget(volume);
        vb.widget(enabled);
    }
};
```

- [ ] **Step 7: Add `view()` to `ButtonModel`**

```cpp
struct ButtonModel {
    prism::Field<prism::Button> action{{"Click me"}};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(action);
        vb.widget(count);
    }
};
```

- [ ] **Step 8: Add `view()` to `FocusModel`**

```cpp
struct FocusModel {
    prism::Field<prism::Label<>> title{{"Hello"}};
    prism::Field<bool> toggle{false};
    prism::Field<prism::Slider<>> slider{{.value = 0.5}};
    prism::Field<prism::Button> btn{{"Click"}};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.widget(toggle);
        vb.widget(slider);
        vb.widget(btn);
        vb.widget(count);
    }
};
```

- [ ] **Step 9: Add `view()` to `RowModel` in `test_view.cpp`**

```cpp
struct RowModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(a);
        vb.widget(b);
    }
};
```

- [ ] **Step 10: Check all other test files for models without `view()`**

Scan `test_delegate.cpp`, `test_dropdown.cpp`, `test_text_field.cpp`, `test_password.cpp`, `test_text_area.cpp`, `test_model_app.cpp`, `test_ui.cpp` for structs that use `prism::WidgetTree` without `view()`. Add `view()` to each.

- [ ] **Step 11: Guard `test_reflect.cpp` behind `#if __cpp_reflection`**

`test_reflect.cpp` tests reflection-specific functionality. Guard the entire file:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#if __cpp_reflection
#include <prism/core/reflect.hpp>
// ... all existing test cases ...
#endif
```

- [ ] **Step 12: Run all tests**

Run: `meson test -C builddir`
Expected: All tests pass on GCC 16 with `-freflection`.

- [ ] **Step 13: Commit**

```bash
git add tests/test_widget_tree.cpp tests/test_view.cpp tests/test_reflect.cpp tests/test_delegate.cpp tests/test_dropdown.cpp tests/test_text_field.cpp tests/test_password.cpp tests/test_text_area.cpp tests/test_model_app.cpp tests/test_ui.cpp
git commit -m "feat: add view() to all test models for pre-C++26 compatibility"
```

---

### Task 9: Update examples to have `view()`

**Files:**
- Modify: `examples/model_dashboard.cpp` (and any other examples)

- [ ] **Step 1: Check which examples exist**

Look in `examples/` for model structs without `view()`.

- [ ] **Step 2: Add `view()` to any example model that lacks one**

The `Dashboard` struct in `model_dashboard.cpp` likely doesn't have `view()` (it relies on reflection for auto-layout). Add one:

```cpp
void view(prism::WidgetTree::ViewBuilder& vb) {
    vb.component(settings);
    vb.component(waveform);
    vb.widget(status);
    vb.widget(notes);
    vb.widget(increment);
    vb.widget(counter);
}
```

Similarly for `Settings` if it lacks `view()`.

- [ ] **Step 3: Build and run the example**

Run: `meson compile -C builddir && ./builddir/examples/model_dashboard`
Expected: Dashboard renders identically to before.

- [ ] **Step 4: Commit**

```bash
git add examples/
git commit -m "feat: add view() to example models for pre-C++26 compatibility"
```

---

### Task 10: Update memory and roadmap

**Files:**
- Modify: `/home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/project-roadmap.md`
- Modify: `/home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/MEMORY.md`

- [ ] **Step 1: Update roadmap**

Mark Phase 3.6 items as in-progress/complete:
- [x] `Node` class — type-erased `Field<T>` + children
- [x] `WidgetTree` uses Node as intermediate layer
- [ ] Runtime behavior wiring via `Node::on_change()` / `on_change_erased()` (deferred)
- [ ] `add_leaf`/`add_container`/`remove_subtree` (deferred — dynamic mutation)
- [x] Pre-C++26 support: C++23 minimum, magic_enum fallback, `-freflection` conditional

- [ ] **Step 2: Add memory for pre-C++26 support decision**

Record the key architectural decision: Node is the universal construction layer, reflection is sugar.

- [ ] **Step 3: Commit memory updates**

No git commit needed for memory files (they're outside the repo).
