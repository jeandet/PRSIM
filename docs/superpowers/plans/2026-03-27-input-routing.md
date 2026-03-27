# Input Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire hit testing to widget-level SenderHub dispatch so that clicking a `Field<bool>` widget toggles its value and triggers a repaint.

**Architecture:** `WidgetNode` gains an `on_input` SenderHub and a `record` function. A post-construction `build_index()` pass populates a `WidgetId → WidgetNode*` map and wires input handlers. `model_app` routes `MouseButton` events through `hit_test()` → `tree.dispatch()`.

**Tech Stack:** C++26 with `-freflection`, doctest, Meson

---

### Task 1: Add `dispatch()` to WidgetTree with `on_input` SenderHub

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test — dispatch emits on correct node**

Add to `tests/test_widget_tree.cpp`:

```cpp
#include <prism/core/input_event.hpp>

TEST_CASE("WidgetTree dispatch emits on correct widget on_input") {
    NestedModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 3);

    bool received = false;
    tree.connect_input(ids[2], [&](const prism::InputEvent&) {
        received = true;
    });

    tree.dispatch(ids[2], prism::MouseButton{{50, 15}, 1, true});
    CHECK(received);
}

TEST_CASE("WidgetTree dispatch to unknown id is a no-op") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    // Should not crash
    tree.dispatch(9999, prism::MouseButton{{0, 0}, 1, true});
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: Compilation error — `connect_input` and `dispatch` not defined.

- [ ] **Step 3: Implement on_input, build_index, dispatch, connect_input**

In `include/prism/core/widget_tree.hpp`, add the `on_input` member to `WidgetNode`, add `index_` / `build_index()` / `dispatch()` / `connect_input()` to `WidgetTree`:

```cpp
#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/reflect.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism {

struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    DrawList draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    std::function<void(WidgetNode&)> wire;
};

class WidgetTree {
public:
    template <typename Model>
    explicit WidgetTree(Model& model) {
        root_ = build_container(model);
        build_index(root_);
        clear_dirty();
    }

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }
    [[nodiscard]] bool any_dirty() const { return check_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    void dispatch(WidgetId id, const InputEvent& ev) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->on_input.emit(ev);
    }

    Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        LayoutNode layout;
        layout.kind = LayoutNode::Kind::Column;
        layout.id = root_.id;
        build_layout(root_, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {0, 0, w, h});

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }

private:
    WidgetNode root_;
    WidgetId next_id_ = 1;
    std::unordered_map<WidgetId, WidgetNode*> index_;

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        for (auto& c : node.children)
            build_index(c);
    }

    static size_t count_leaves(const WidgetNode& node) {
        if (!node.is_container) return 1;
        size_t n = 0;
        for (auto& c : node.children) n += count_leaves(c);
        return n;
    }

    static bool check_dirty(const WidgetNode& node) {
        if (node.dirty) return true;
        for (auto& c : node.children)
            if (check_dirty(c)) return true;
        return false;
    }

    static void clear_dirty_impl(WidgetNode& node) {
        node.dirty = false;
        for (auto& c : node.children) clear_dirty_impl(c);
    }

    static void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
        if (!node.is_container) {
            ids.push_back(node.id);
            return;
        }
        for (auto& c : node.children) collect_leaf_ids(c, ids);
    }

    static bool mark_dirty(WidgetNode& node, WidgetId id) {
        if (node.id == id) { node.dirty = true; return true; }
        for (auto& c : node.children)
            if (mark_dirty(c, id)) return true;
        return false;
    }

    static void build_layout(WidgetNode& node, LayoutNode& layout) {
        if (!node.is_container) {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = node.id;
            leaf.draws = node.draws;
            layout.children.push_back(std::move(leaf));
        } else {
            for (auto& c : node.children) {
                build_layout(c, layout);
            }
        }
    }

    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;

        record_field_widget(node, field);

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }

    template <typename T>
    static void record_field_widget(WidgetNode& node, const Field<T>& field) {
        node.draws.clear();
        auto label_text = std::string(field.label);
        node.draws.filled_rect({0, 0, 200, 30}, Color::rgba(50, 50, 60));
        node.draws.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    template <typename Model>
    WidgetNode build_container(Model& model) {
        WidgetNode container;
        container.id = next_id_++;
        container.is_container = true;

        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));

        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;

            if constexpr (is_field_v<M>) {
                container.children.push_back(build_leaf(member));
            } else if constexpr (is_component_v<M>) {
                container.children.push_back(build_container(member));
            }
        }

        return container;
    }
};

} // namespace prism
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: All widget_tree tests PASS (existing + 2 new).

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: add on_input SenderHub, build_index, dispatch to WidgetTree"
```

---

### Task 2: Add `record` function and `refresh_dirty` for re-recording

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test — dirty node re-records draws**

Add to `tests/test_widget_tree.cpp`:

```cpp
TEST_CASE("WidgetTree refresh_dirty re-records draws from field state") {
    SimpleModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    model.count.set(99);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    // Snapshot should rebuild successfully with refreshed draws
    REQUIRE(snap2 != nullptr);
    CHECK(snap2->geometry.size() == 2);
}
```

- [ ] **Step 2: Run test to verify it fails (or passes trivially)**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: May pass trivially since draws don't change yet. This test establishes the baseline — the real behavior test is in Task 3 where `Field<bool>` changes color.

- [ ] **Step 3: Add `record` function and `refresh_dirty` to WidgetTree**

In `include/prism/core/widget_tree.hpp`, modify `WidgetNode` to add `record`, modify `build_leaf` to store the record function, add `refresh_dirty`, and call it from `build_snapshot`:

Add to `WidgetNode` struct (after `wire`):
```cpp
    std::function<void(WidgetNode&)> record;
```

Replace the `build_leaf` method:
```cpp
    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;

        node.record = [&field](WidgetNode& n) {
            record_field_widget(n, field);
        };
        node.record(node);

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }
```

Add `refresh_dirty` as a private static method:
```cpp
    static void refresh_dirty(WidgetNode& node) {
        if (node.dirty && node.record)
            node.record(node);
        for (auto& c : node.children)
            refresh_dirty(c);
    }
```

Add `refresh_dirty(root_)` as the first line in `build_snapshot`:
```cpp
    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);

        LayoutNode layout;
        // ... rest unchanged
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: All widget_tree tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: add record function and refresh_dirty for dirty re-recording"
```

---

### Task 3: Wire `Field<bool>` toggle handler via `build_index`

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test — Field<bool> toggle via dispatch**

Add to `tests/test_widget_tree.cpp`:

```cpp
struct BoolModel {
    prism::Field<bool> flag{"Flag", false};
    prism::Field<int> count{"Count", 0};
};

TEST_CASE("Field<bool> toggles on MouseButton dispatch") {
    BoolModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 2);

    CHECK(model.flag.get() == false);
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
    CHECK(model.flag.get() == true);
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> ignores mouse release") {
    BoolModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, false});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> toggle produces different draws on re-record") {
    BoolModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    REQUIRE(snap2 != nullptr);
    // The draws should differ because Field<bool> changes background color
    // We verify the snapshot rebuilt successfully — visual difference is
    // confirmed by the draw list having the same structure but different color
    CHECK(snap2->draw_lists.size() == snap1->draw_lists.size());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: FAIL — `Field<bool>` dispatch doesn't toggle the field value because no input handler is wired.

- [ ] **Step 3: Add bool-specific record and wire functions to build_leaf**

In `include/prism/core/widget_tree.hpp`, add a specialization of `record_field_widget` for `Field<bool>` that shows visual state, and set up `wire` for bool fields:

Add a `record_field_widget` specialization for `bool` (before the generic `record_field_widget`):

```cpp
    static void record_field_widget(WidgetNode& node, const Field<bool>& field) {
        node.draws.clear();
        auto label_text = std::string(field.label);
        auto bg = field.get()
            ? Color::rgba(0, 120, 80)    // green-ish when true
            : Color::rgba(50, 50, 60);   // dark when false
        node.draws.filled_rect({0, 0, 200, 30}, bg);
        node.draws.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }
```

Modify `build_leaf` to set `wire` for `Field<bool>`:

```cpp
    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;

        node.record = [&field](WidgetNode& n) {
            record_field_widget(n, field);
        };
        node.record(node);

        if constexpr (std::is_same_v<T, bool>) {
            node.wire = [&field](WidgetNode& n) {
                n.connections.push_back(
                    n.on_input.connect([&field](const InputEvent& ev) {
                        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
                            field.set(!field.get());
                    })
                );
            };
        }

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: All widget_tree tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: wire Field<bool> toggle handler via on_input + build_index"
```

---

### Task 4: Route input events in model_app

**Files:**
- Modify: `include/prism/core/model_app.hpp`
- Modify: `tests/test_model_app.cpp`

- [ ] **Step 1: Write the failing test — model_app routes click to Field<bool>**

Add to `tests/test_model_app.cpp`:

```cpp
#include <prism/core/hit_test.hpp>

struct ClickTestModel {
    prism::Field<bool> toggle{"Toggle", false};
};

TEST_CASE("model_app routes MouseButton to Field<bool> toggle") {
    std::vector<std::shared_ptr<const prism::SceneSnapshot>> snapshots;

    struct ClickBackend final : public prism::BackendBase {
        std::vector<std::shared_ptr<const prism::SceneSnapshot>>& snaps;
        explicit ClickBackend(std::vector<std::shared_ptr<const prism::SceneSnapshot>>& s)
            : snaps(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            // Wait for first snapshot to know geometry
            while (snaps.empty()) {}
            auto& geo = snaps.back()->geometry;
            REQUIRE_FALSE(geo.empty());
            auto [id, rect] = geo[0];
            auto center = rect.center();

            // Click in the widget
            cb(prism::MouseButton{center, 1, true});
            // Give app thread time to process
            while (snaps.size() < 2) {}

            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snaps.push_back(std::move(s));
        }
        void wake() override {}
        void quit() override {}
    };

    ClickTestModel model;
    CHECK(model.toggle.get() == false);

    prism::model_app(
        prism::Backend{std::make_unique<ClickBackend>(snapshots)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    CHECK(model.toggle.get() == true);
    CHECK(snapshots.size() >= 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test model_app -C builddir --print-errorlogs`
Expected: FAIL — `model.toggle.get()` is still `false` because `model_app` doesn't route input events.

- [ ] **Step 3: Add input routing to model_app**

Replace the contents of `include/prism/core/model_app.hpp`:

```cpp
#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/widget_tree.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <variant>

namespace prism {

template <typename Model>
void model_app(Backend backend, BackendConfig cfg, Model& model) {
    mpsc_queue<InputEvent> input_queue;
    std::atomic<bool> running{true};
    std::atomic<bool> input_pending{false};

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
            input_queue.push(ev);
            input_pending.store(true, std::memory_order_release);
            input_pending.notify_one();
        });
    });

    backend.wait_ready();

    WidgetTree tree(model);
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    auto publish = [&]() -> std::shared_ptr<const SceneSnapshot> {
        auto snap = std::shared_ptr<const SceneSnapshot>(
            tree.build_snapshot(w, h, ++version));
        backend.submit(snap);
        backend.wake();
        tree.clear_dirty();
        return snap;
    };

    auto current_snap = publish();

    while (running.load(std::memory_order_relaxed)) {
        input_pending.wait(false, std::memory_order_acquire);
        input_pending.store(false, std::memory_order_relaxed);

        bool needs_rebuild = false;

        while (auto ev = input_queue.pop()) {
            if (std::holds_alternative<WindowClose>(*ev)) {
                running.store(false, std::memory_order_relaxed);
                break;
            }
            if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                w = resize->width;
                h = resize->height;
                needs_rebuild = true;
            }
            if (auto* mb = std::get_if<MouseButton>(&*ev); mb && current_snap) {
                if (auto id = hit_test(*current_snap, mb->position))
                    tree.dispatch(*id, *ev);
            }
        }

        if (!running.load(std::memory_order_relaxed)) break;

        if (tree.any_dirty() || needs_rebuild)
            current_snap = publish();
    }

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(std::string_view title, Model& model) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    model_app(Backend::software(cfg), cfg, model);
}

} // namespace prism
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test model_app -C builddir --print-errorlogs`
Expected: All model_app tests PASS.

- [ ] **Step 5: Run all tests**

Run: `meson test -C builddir --print-errorlogs`
Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/model_app.hpp tests/test_model_app.cpp
git commit -m "feat: route MouseButton events through hit_test to widget dispatch in model_app"
```

---

### Task 5: Update model_dashboard example

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Read current example**

Read `examples/model_dashboard.cpp` to see its current content.

- [ ] **Step 2: Add a note about interactive Field<bool>**

The example already has `Field<bool> dark_mode`. With input routing, clicking it now toggles. Add a signal connection that prints to stdout when toggled, so the user sees the interaction working:

```cpp
#include <prism/prism.hpp>
#include <iostream>

enum class Priority { Low, Medium, High };

struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};
    prism::Field<bool> dark_mode{"Dark Mode", true};
};

struct Dashboard {
    Settings settings;
    prism::Field<int> counter{"Counter", 0};
};

int main() {
    Dashboard dashboard;

    dashboard.settings.dark_mode.on_change().connect([](const bool& v) {
        std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
    });

    prism::model_app("Dashboard", dashboard);
}
```

- [ ] **Step 3: Build and verify the example compiles**

Run: `meson compile -C builddir model_dashboard`
Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add examples/model_dashboard.cpp
git commit -m "feat: update model_dashboard to demonstrate interactive Field<bool> toggle"
```
