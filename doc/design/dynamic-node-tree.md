# Dynamic Node Tree: Runtime-Defined UI

## Overview

PRISM's widget tree is currently built at compile time via P2996 reflection over model structs. This design adds a **runtime equivalent**: a `Node` tree that produces identical `WidgetNode` output using the same delegates, layout, focus, and dirty tracking infrastructure.

The two paths converge to the same internal representation. The framework cannot distinguish a widget created by reflection from one created at runtime.

```
Compile-time path:   struct with Field<T> members  →  reflection walk  →  WidgetNode tree
Runtime path:        Node tree                      →  recursive walk   →  WidgetNode tree
                                                                        ↑ same output
```

## Motivation

Some UIs cannot be known at compile time:

- **JSON/YAML editors** — structure comes from data
- **Plugin systems** — third-party code defines widgets
- **Configuration-driven dashboards** — layout from a spec file
- **Database schema browsers** — fields from runtime metadata
- **Python bindings** — all UI construction is runtime

The static `Field<T>` + reflection path remains the primary API for known-at-compile-time applications. The dynamic path extends PRISM to cases where the structure is data-driven.

## Design

### Node: a type-erased Field<T> + children

A `Node` is the runtime equivalent of what reflection sees when walking a model struct: either a leaf (analogous to `Field<T>`) or a container (analogous to a nested struct).

```cpp
namespace prism {

class Node {
public:
    // Leaf factories — T determines the Delegate used
    template <typename T>
    static Node field(std::string name, T initial = {});

    // Container factory
    static Node group(std::string name, std::vector<Node> children = {});

    // Tree mutation
    void append(Node child);
    void remove(std::size_t index);
    void clear();

    // Value access (leaf nodes only)
    template <typename T> const T& get() const;
    template <typename T> void set(T value);

    // Observer API — same semantics as Field<T>::on_change()
    template <typename T>
    auto on_change() -> /* sender_of<const T&> */;

    // Type-erased observer for cross-type wiring
    auto on_change_erased() -> /* sender_of<const std::any&> */;

    // Introspection
    const std::string& name() const;
    std::span<const Node> children() const;
    std::span<Node> children();
    bool is_leaf() const;

private:
    std::string name_;
    std::vector<Node> children_;

    // Type-erased field internals (leaf only)
    std::any value_;
    SenderHub<const std::any&> on_change_;
    std::function<void(WidgetNode&, Node&)> build_leaf_;  // captures Delegate<T> calls
};

} // namespace prism
```

### How field<T>() works

The factory captures the concrete type in closures, then forgets it:

```cpp
template <typename T>
Node Node::field(std::string name, T initial) {
    Node n;
    n.name_ = std::move(name);
    n.value_ = std::move(initial);

    // Captures Delegate<T> — same lambda shape as WidgetTree::build_leaf
    n.build_leaf_ = [](WidgetNode& node, Node& self) {
        node.record = [&self](WidgetNode& n) {
            n.draws.clear();
            // Delegate<T>::record(n.draws, ..., n);
        };
        node.wire = [&self](WidgetNode& n) {
            // Delegate<T>::handle_input(...)
        };
    };

    return n;
}
```

After construction, the `Node` is fully type-erased. The `build_leaf_` lambda is the only place that knows `T`.

### Building the WidgetTree from a Node tree

A single recursive function, mirroring `build_container` / `build_leaf`:

```cpp
void build_from_node(Node& node, WidgetNode& parent, WidgetTree& tree) {
    if (node.is_leaf()) {
        auto& widget = tree.add_leaf(parent);
        node.build_leaf_(widget, node);
    } else {
        auto& container = tree.add_container(parent);
        for (auto& child : node.children())
            build_from_node(child, container, tree);
    }
}
```

This is the same traversal as the compile-time reflection walk, with `template for` replaced by a regular `for` loop.

### Required WidgetTree additions

Two new methods — the runtime equivalent of the existing `build_leaf` and `build_container`:

```cpp
class WidgetTree {
public:
    // Existing: compile-time construction from model struct
    template <typename Model>
    explicit WidgetTree(Model& model);

    // New: runtime construction from Node tree
    explicit WidgetTree(Node& root);

    // New: dynamic subtree mutation
    WidgetNode& add_leaf(WidgetNode& parent);
    WidgetNode& add_container(WidgetNode& parent);
    void remove_subtree(WidgetId id);
    void rebuild_index();  // after structural mutations
};
```

### Hybrid: static model with dynamic subtrees

A static model can embed a `Node` as a member. Reflection detects it and delegates to the runtime builder:

```cpp
struct App {
    Field<std::string> title;
    Node dynamic_panel;          // runtime-built subtree
    Field<bool> dark_mode;
};
```

In `build_container`, the reflection walk sees `Node` and calls `build_from_node` instead of recursing into struct members. Static and dynamic widgets coexist as siblings in the same tree.

## Runtime Behavior Wiring

`Node` exposes the same sender/observer API as `Field<T>`. Runtime wiring uses the same patterns:

```cpp
auto name = Node::field<std::string>("Name", "");
auto greeting = Node::field<std::string>("Greeting", "");

name.on_change<std::string>([&](const std::string& v) {
    greeting.set("Hello, " + v);
});
```

For cross-node wiring where types aren't statically known:

```cpp
source.on_change_erased([&](const std::any& v) {
    target.set(transform(v));
});
```

## JSON Editor Example

The dynamic path makes a JSON editor trivial — map JSON types to existing delegates:

```cpp
Node json_to_node(const std::string& key, nlohmann::json& j) {
    switch (j.type()) {
        case json::value_t::boolean:
            return Node::field<bool>(key, j.get<bool>());
        case json::value_t::number_integer:
            return Node::field<int>(key, j.get<int>());
        case json::value_t::string:
            return Node::field<TextField<>>(key, {j.get<std::string>()});
        case json::value_t::object: {
            auto group = Node::group(key);
            for (auto& [k, v] : j.items())
                group.append(json_to_node(k, v));
            return group;
        }
        case json::value_t::array: {
            auto group = Node::group(key);
            for (std::size_t i = 0; i < j.size(); ++i)
                group.append(json_to_node(std::to_string(i), j[i]));
            return group;
        }
        default:
            return Node::field<Label<>>(key, {key + ": null"});
    }
}
```

Every leaf reuses an existing `Delegate<T>`. No new rendering or input code.

## Structural Changes (Rebuild)

When the node tree's structure changes (not just values), the corresponding widget subtree must be rebuilt:

```cpp
// Add a new key to the JSON
root.append(Node::field<std::string>("new_key", ""));
tree.rebuild_subtree(root_widget_id);  // tear down + rebuild from Node tree
```

Value changes go through the normal `set()` -> `on_change()` -> dirty -> repaint path. Structural changes (append/remove/clear) trigger a subtree rebuild and re-index.

## Future Sugar

The core `Node` API is deliberately minimal. Convenience layers can be added incrementally:

- **Builder pattern** for fluent tree construction
- **Path-based lookup** (`root.find("settings.font_size")`)
- **Declarative rules** attached to the node tree
- **Schema-driven construction** (JSON Schema -> Node tree with validation)
- **Serialization** (Node tree <-> JSON/YAML round-trip)

These are sugar over `Node::field<T>()` + `on_change()` + tree mutation.

## Relationship to Components

[Components](components.md) bundle Model + Behavior via inheritance. A Component with a `Node` member can build its subtree dynamically:

```cpp
struct JsonEditor : prism::Component {
    Node tree;

    JsonEditor(nlohmann::json& data) : tree(json_to_node("root", data)) {}

    void setup(AppContext& ctx) override {
        // wire cross-field behaviors on the dynamic tree
    }
};
```

The Component provides the lifecycle (setup, teardown). The `Node` tree provides the dynamic structure. Each covers a distinct concern.

## What Changes

| File | Change |
|------|--------|
| `node.hpp` | **New.** `Node` class |
| `widget_tree.hpp` | `add_leaf()`, `add_container()`, `remove_subtree()`, `rebuild_index()`, `WidgetTree(Node&)` constructor |
| `model_app.hpp` | Overload accepting `Node&` as model |

## What Stays Unchanged

- `Field<T>`, `SenderHub`, `Connection` — unchanged, `Node` wraps them internally
- `Delegate<T>` — unchanged, `Node::build_leaf_` calls them
- `WidgetNode` — unchanged, same struct for both paths
- `DrawList`, `SceneSnapshot`, `BackendBase` — unchanged
- Layout, focus, hit testing, dirty tracking — unchanged
- Existing static model structs — unchanged, no migration
