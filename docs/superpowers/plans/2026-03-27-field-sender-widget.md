# Field<T> + Sender/Observer + Persistent Widget Tree — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace MVU's per-frame view rebuild with persistent `Field<T>` observables, sender-based signal connections, and a reflection-driven widget tree.

**Architecture:** `Field<T>` is a value wrapper with a sender hub that notifies receivers on `set()`. The framework reflects over model structs at compile time, maps `Field<T>` members to widgets, and wires `on_change()` connections. Only dirty widgets re-record their DrawList. The existing rendering pipeline (DrawList, SceneSnapshot, Backend, layout, hit_test) stays unchanged.

**Tech Stack:** C++26 (`-freflection`, `-std=c++26`), GCC 16, `<meta>` header, Meson, doctest

**Build note:** All new headers require the `-freflection` flag. Add `cpp_args : ['-freflection']` to the Meson build.

---

## File Structure

### New files

| File | Responsibility |
|---|---|
| `include/prism/core/field.hpp` | `Field<T>` — value + sender hub + `set()`/`get()`/`on_change()` |
| `include/prism/core/connection.hpp` | `Connection` RAII guard, `SenderHub<T>` multi-receiver dispatch |
| `include/prism/core/list.hpp` | `List<T>` — observable vector with insert/remove/update senders |
| `include/prism/core/reflect.hpp` | `for_each_field()`, `is_field_v`, `is_component_v` reflection utilities |
| `include/prism/core/widget_tree.hpp` | `WidgetNode`, `WidgetTree` — persistent tree built from model via reflection |
| `include/prism/core/model_app.hpp` | `prism::app(title, model)` — new model-driven entry point |
| `tests/test_field.cpp` | Field<T> unit tests |
| `tests/test_connection.cpp` | Connection + SenderHub unit tests |
| `tests/test_list.cpp` | List<T> unit tests |
| `tests/test_reflect.cpp` | Reflection utility tests |
| `tests/test_widget_tree.cpp` | WidgetTree construction + dirty tracking tests |
| `tests/test_model_app.cpp` | Integration tests for model-driven app entry point |

### Modified files

| File | Change |
|---|---|
| `meson.build` | Add `-freflection` to `default_options` or `cpp_args` |
| `tests/meson.build` | Register new test executables |
| `include/prism/prism.hpp` | Include new headers |

### Unchanged files

All existing files (`draw_list.hpp`, `scene_snapshot.hpp`, `backend.hpp`, `layout.hpp`, `hit_test.hpp`, `ui.hpp`, `app.hpp`, `input_event.hpp`, `mpsc_queue.hpp`, `pixel_buffer.hpp`, `software_renderer.hpp`, backends, all existing tests) remain untouched. The old `app<State>()` API continues to work — we add a new `app(title, model)` alongside it.

---

### Task 1: Enable -freflection in Meson Build

**Files:**
- Modify: `meson.build`

- [ ] **Step 1: Add -freflection flag**

In `meson.build`, add the reflection flag to the project default options:

```meson
project('prism', 'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++26',
    'warning_level=3',
    'werror=true',
    'b_ndebug=if-release',
  ],
  license : 'MIT',
  meson_version : '>= 1.5.0',
)

add_project_arguments('-freflection', language : 'cpp')
```

Use `add_project_arguments` rather than `default_options` because `-freflection` is not a meson built-in option.

- [ ] **Step 2: Verify build still passes**

Run: `meson setup builddir --wipe && meson test -C builddir`
Expected: All existing tests pass. The flag has no effect on code that doesn't use `^^`.

- [ ] **Step 3: Commit**

```bash
git add meson.build
git commit -m "build: enable -freflection for C++26 reflection support"
```

---

### Task 2: Connection + SenderHub

**Files:**
- Create: `include/prism/core/connection.hpp`
- Create: `tests/test_connection.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_connection.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/connection.hpp>

TEST_CASE("SenderHub notifies connected receiver") {
    prism::SenderHub<int> hub;
    int received = -1;
    auto conn = hub.connect([&](int v) { received = v; });
    hub.emit(42);
    CHECK(received == 42);
}

TEST_CASE("SenderHub notifies multiple receivers") {
    prism::SenderHub<int> hub;
    int a = 0, b = 0;
    auto c1 = hub.connect([&](int v) { a = v; });
    auto c2 = hub.connect([&](int v) { b = v; });
    hub.emit(7);
    CHECK(a == 7);
    CHECK(b == 7);
}

TEST_CASE("Connection disconnects on destruction") {
    prism::SenderHub<int> hub;
    int received = 0;
    {
        auto conn = hub.connect([&](int v) { received = v; });
        hub.emit(1);
        CHECK(received == 1);
    }
    hub.emit(2);
    CHECK(received == 1);  // not updated — disconnected
}

TEST_CASE("Connection is move-only") {
    prism::SenderHub<int> hub;
    int received = 0;
    auto c1 = hub.connect([&](int v) { received = v; });
    auto c2 = std::move(c1);
    hub.emit(5);
    CHECK(received == 5);  // c2 holds the connection now
}

TEST_CASE("Connection disconnect is idempotent") {
    prism::SenderHub<int> hub;
    auto conn = hub.connect([](int) {});
    conn.disconnect();
    conn.disconnect();  // no crash
}

TEST_CASE("SenderHub with no args") {
    prism::SenderHub<> hub;
    int calls = 0;
    auto conn = hub.connect([&] { ++calls; });
    hub.emit();
    hub.emit();
    CHECK(calls == 2);
}

TEST_CASE("Receiver can disconnect itself during emit") {
    prism::SenderHub<> hub;
    int calls = 0;
    prism::Connection conn;
    conn = hub.connect([&] {
        ++calls;
        conn.disconnect();
    });
    hub.emit();
    hub.emit();
    CHECK(calls == 1);
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `headless_tests` dictionary:

```meson
'connection' : files('test_connection.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir connection`
Expected: FAIL — `prism/core/connection.hpp` not found

- [ ] **Step 4: Implement Connection + SenderHub**

Create `include/prism/core/connection.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace prism {

class Connection {
public:
    Connection() = default;

    Connection(std::shared_ptr<std::function<void()>> detach)
        : detach_(std::move(detach)) {}

    ~Connection() { disconnect(); }

    Connection(Connection&& o) noexcept : detach_(std::move(o.detach_)) {}
    Connection& operator=(Connection&& o) noexcept {
        if (this != &o) {
            disconnect();
            detach_ = std::move(o.detach_);
        }
        return *this;
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void disconnect() {
        if (auto d = std::move(detach_)) {
            if (*d) (*d)();
        }
    }

private:
    std::shared_ptr<std::function<void()>> detach_;
};

template <typename... Args>
class SenderHub {
public:
    using Callback = std::function<void(Args...)>;

    Connection connect(Callback cb) {
        auto id = next_id_++;
        receivers_.push_back({id, std::move(cb)});
        auto detach = std::make_shared<std::function<void()>>(
            [this, id] { remove(id); }
        );
        return Connection{std::move(detach)};
    }

    void emit(Args... args) const {
        // Copy to allow disconnect during emit
        auto snapshot = receivers_;
        for (auto& [id, cb] : snapshot) {
            if (cb) cb(args...);
        }
    }

private:
    struct Entry {
        uint64_t id;
        Callback cb;
    };

    uint64_t next_id_ = 0;
    std::vector<Entry> receivers_;

    void remove(uint64_t id) {
        std::erase_if(receivers_, [id](auto& e) { return e.id == id; });
    }
};

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir connection`
Expected: All 7 tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/connection.hpp tests/test_connection.cpp tests/meson.build
git commit -m "feat: add Connection RAII guard and SenderHub<Args...> for observer pattern"
```

---

### Task 3: Field<T>

**Files:**
- Create: `include/prism/core/field.hpp`
- Create: `tests/test_field.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_field.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>

#include <string>

TEST_CASE("Field stores value and label") {
    prism::Field<int> f{"Count", 42};
    CHECK(f.get() == 42);
    CHECK(f.label == std::string_view("Count"));
}

TEST_CASE("Field implicit conversion to const T&") {
    prism::Field<std::string> f{"Name", "hello"};
    const std::string& ref = f;
    CHECK(ref == "hello");
}

TEST_CASE("Field::set updates value") {
    prism::Field<int> f{"X", 0};
    f.set(10);
    CHECK(f.get() == 10);
}

TEST_CASE("Field::set notifies on_change receivers") {
    prism::Field<int> f{"X", 0};
    int received = -1;
    auto conn = f.on_change().connect([&](const int& v) { received = v; });
    f.set(7);
    CHECK(received == 7);
}

TEST_CASE("Field::set does not notify if value unchanged") {
    prism::Field<int> f{"X", 5};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });
    f.set(5);
    CHECK(calls == 0);
    f.set(6);
    CHECK(calls == 1);
}

TEST_CASE("Field multiple receivers") {
    prism::Field<std::string> f{"S", "a"};
    std::string r1, r2;
    auto c1 = f.on_change().connect([&](const std::string& v) { r1 = v; });
    auto c2 = f.on_change().connect([&](const std::string& v) { r2 = v; });
    f.set("b");
    CHECK(r1 == "b");
    CHECK(r2 == "b");
}

TEST_CASE("Field connection RAII disconnect") {
    prism::Field<int> f{"X", 0};
    int received = 0;
    {
        auto conn = f.on_change().connect([&](const int& v) { received = v; });
        f.set(1);
        CHECK(received == 1);
    }
    f.set(2);
    CHECK(received == 1);  // disconnected
}

TEST_CASE("Field default-constructed value") {
    prism::Field<int> f{"X"};
    CHECK(f.get() == 0);
}

TEST_CASE("Field with bool") {
    prism::Field<bool> f{"Done", false};
    bool toggled = false;
    auto conn = f.on_change().connect([&](const bool& v) { toggled = v; });
    f.set(true);
    CHECK(toggled == true);
    CHECK(f.get() == true);
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `headless_tests` dictionary:

```meson
'field' : files('test_field.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir field`
Expected: FAIL — `prism/core/field.hpp` not found

- [ ] **Step 4: Implement Field<T>**

Create `include/prism/core/field.hpp`:

```cpp
#pragma once

#include <prism/core/connection.hpp>

namespace prism {

template <typename T>
struct Field {
    const char* label = "";
    T value{};

    Field() = default;
    Field(const char* lbl) : label(lbl) {}
    Field(const char* lbl, T init) : label(lbl), value(std::move(init)) {}

    const T& get() const { return value; }
    operator const T&() const { return value; }

    void set(T new_value) {
        if (value == new_value) return;
        value = std::move(new_value);
        changed_.emit(value);
    }

    SenderHub<const T&>& on_change() { return changed_; }

private:
    SenderHub<const T&> changed_;
};

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir field`
Expected: All 9 tests PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/field.hpp tests/test_field.cpp tests/meson.build
git commit -m "feat: add Field<T> observable value type with sender-based on_change()"
```

---

### Task 4: Reflection Utilities

**Files:**
- Create: `include/prism/core/reflect.hpp`
- Create: `tests/test_reflect.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_reflect.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/reflect.hpp>
#include <prism/core/field.hpp>

#include <string>
#include <vector>

struct Simple {
    prism::Field<int> x{"X", 1};
    prism::Field<std::string> name{"Name", "hi"};
};

struct Nested {
    Simple inner;
    prism::Field<bool> flag{"Flag", false};
};

struct Empty {};

struct NotAModel {
    int raw_value = 42;
};

TEST_CASE("is_field_v detects Field<T>") {
    CHECK(prism::is_field_v<prism::Field<int>>);
    CHECK(prism::is_field_v<prism::Field<std::string>>);
    CHECK_FALSE(prism::is_field_v<int>);
    CHECK_FALSE(prism::is_field_v<std::string>);
}

TEST_CASE("for_each_field visits all Field<T> members") {
    Simple s;
    int count = 0;
    prism::for_each_field(s, [&](auto& field) { ++count; });
    CHECK(count == 2);
}

TEST_CASE("for_each_field gives access to field label and value") {
    Simple s;
    std::vector<std::string> labels;
    prism::for_each_field(s, [&](auto& field) {
        labels.push_back(field.label);
    });
    CHECK(labels.size() == 2);
    CHECK(labels[0] == "X");
    CHECK(labels[1] == "Name");
}

TEST_CASE("is_component_v detects structs containing Field<T>") {
    CHECK(prism::is_component_v<Simple>);
    CHECK(prism::is_component_v<Nested>);
    CHECK_FALSE(prism::is_component_v<int>);
    CHECK_FALSE(prism::is_component_v<Empty>);
    CHECK_FALSE(prism::is_component_v<NotAModel>);
}

TEST_CASE("for_each_member visits fields and sub-components") {
    Nested n;
    int fields = 0;
    int components = 0;
    prism::for_each_member(n, [&](auto& member) {
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (prism::is_field_v<M>)
            ++fields;
        else if constexpr (prism::is_component_v<M>)
            ++components;
    });
    CHECK(fields == 1);      // flag
    CHECK(components == 1);  // inner
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `headless_tests` dictionary:

```meson
'reflect' : files('test_reflect.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir reflect`
Expected: FAIL — `prism/core/reflect.hpp` not found

- [ ] **Step 4: Implement reflection utilities**

Create `include/prism/core/reflect.hpp`:

```cpp
#pragma once

#include <meta>
#include <type_traits>

namespace prism {

// Detect Field<T>
template <typename T>
struct is_field : std::false_type {};

template <typename T>
    requires requires { T::label; typename std::remove_cvref_t<decltype(std::declval<T>().value)>; }
    && requires(T t) { t.on_change(); }
struct is_field<T> : std::true_type {};

template <typename T>
inline constexpr bool is_field_v = is_field<T>::value;

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

// Detect a component: a struct that has at least one Field<T> member
// or contains a sub-struct that does (checked shallowly via Field<T> or nested component)
template <typename T>
consteval bool check_is_component() {
    if constexpr (!std::is_class_v<T>) return false;
    else {
        constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (is_field_v<M>) found = true;
        }
        return found;
    }
}

// Two-pass: first check direct fields, then check if any member is itself a component
template <typename T>
consteval bool check_is_component_recursive() {
    if (check_is_component<T>()) return true;
    if constexpr (!std::is_class_v<T>) return false;
    else {
        constexpr auto members = std::define_static_array(
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
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir reflect`
Expected: All 5 tests PASS

Note: if `check_is_component` hits compiler issues with `template for` inside `consteval`, an alternative is to use a consteval loop over the `members` span with index-based access. Adjust if needed — the test defines the contract.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/reflect.hpp tests/test_reflect.cpp tests/meson.build
git commit -m "feat: add reflection utilities — is_field_v, is_component_v, for_each_field/member"
```

---

### Task 5: WidgetTree — Persistent Widget Nodes from Model

**Files:**
- Create: `include/prism/core/widget_tree.hpp`
- Create: `tests/test_widget_tree.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_widget_tree.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <string>

struct SimpleModel {
    prism::Field<int> count{"Count", 0};
    prism::Field<std::string> name{"Name", "hi"};
};

struct NestedModel {
    SimpleModel inner;
    prism::Field<bool> flag{"Flag", false};
};

TEST_CASE("WidgetTree creates one leaf per Field") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("WidgetTree recurses into nested components") {
    NestedModel model;
    prism::WidgetTree tree(model);
    // inner.count + inner.name + flag = 3 leaf fields
    CHECK(tree.leaf_count() == 3);
}

TEST_CASE("WidgetTree nodes track dirty state from Field::set") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK_FALSE(tree.any_dirty());
    model.count.set(5);
    CHECK(tree.any_dirty());
}

TEST_CASE("WidgetTree clear_dirty resets all flags") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    model.count.set(5);
    CHECK(tree.any_dirty());
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("WidgetTree nodes have stable IDs") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    CHECK(ids.size() == 2);
    // IDs are stable across queries
    CHECK(ids == tree.leaf_ids());
}

TEST_CASE("WidgetTree builds SceneSnapshot from model") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    // One entry per leaf field
    CHECK(snap->geometry.size() == 2);
    CHECK(snap->version == 1);
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `headless_tests` dictionary:

```meson
'widget_tree' : files('test_widget_tree.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir widget_tree`
Expected: FAIL — `prism/core/widget_tree.hpp` not found

- [ ] **Step 4: Implement WidgetTree**

Create `include/prism/core/widget_tree.hpp`:

```cpp
#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/reflect.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prism {

struct WidgetNode {
    WidgetId id = 0;
    bool dirty = true;  // dirty on creation
    DrawList draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;  // for container nodes
    bool is_container = false;
};

class WidgetTree {
public:
    template <typename Model>
    explicit WidgetTree(Model& model) {
        root_ = build_container(model);
    }

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }

    [[nodiscard]] bool any_dirty() const { return check_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> build_snapshot(
            int w, int h, uint64_t version) {
        // Build layout tree from widget tree
        LayoutNode layout_root;
        layout_root.kind = LayoutNode::Kind::Column;
        layout_root.id = root_.id;
        build_layout(root_, layout_root);

        layout_measure(layout_root, LayoutAxis::Vertical);
        layout_arrange(layout_root,
            {0, 0, static_cast<float>(w), static_cast<float>(h)});

        auto snap = std::make_shared<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout_root, *snap);
        return snap;
    }

private:
    WidgetNode root_;
    WidgetId next_id_ = 0;

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

    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.dirty = true;

        // Record initial draw commands for this field type
        record_field_widget(node, field);

        // Connect: field change -> mark dirty
        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }

    template <typename T>
    void record_field_widget(WidgetNode& node, const Field<T>& field) {
        node.draws.clear();
        // Minimal placeholder rendering — a labeled rectangle
        // Real widget rendering comes in a later phase
        auto label_text = std::string(field.label);
        node.draws.filled_rect({0, 0, 200, 30}, Color::rgba(50, 50, 60));
        node.draws.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
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
            leaf.draws = node.draws;  // copy current draws
            layout.children.push_back(std::move(leaf));
        } else {
            for (auto& c : node.children) {
                build_layout(c, layout);
            }
        }
    }
};

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir widget_tree`
Expected: All 6 tests PASS

Note: The `mark_dirty` callback captures `this` — this is safe because the `WidgetTree` owns both the nodes and their connections. If the tree is destroyed, connections disconnect via RAII before any callback can fire. If GCC 16 has issues with `template for` inside a member function template that captures `this`, restructure `build_container` as a free function taking `next_id_` by reference.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp tests/meson.build
git commit -m "feat: add WidgetTree — persistent widget nodes from model structs via reflection"
```

---

### Task 6: Model-Driven App Entry Point

**Files:**
- Create: `include/prism/core/model_app.hpp`
- Create: `tests/test_model_app.cpp`
- Modify: `tests/meson.build`
- Modify: `include/prism/prism.hpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_model_app.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/model_app.hpp>
#include <prism/core/null_backend.hpp>
#include <prism/core/field.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <string>

struct TestModel {
    prism::Field<int> count{"Count", 42};
    prism::Field<std::string> name{"Name", "hello"};
};

struct NestedTestModel {
    TestModel inner;
    prism::Field<bool> flag{"Flag", false};
};

TEST_CASE("model_app runs and produces a snapshot") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    TestModel model;
    prism::model_app(
        prism::Backend{std::make_unique<CapturingBackend>(captured)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 2);  // count + name
}

TEST_CASE("model_app with nested model") {
    std::shared_ptr<const prism::SceneSnapshot> captured;

    struct CapturingBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& snap_ref;
        explicit CapturingBackend(std::shared_ptr<const prism::SceneSnapshot>& s)
            : snap_ref(s) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snap_ref = std::move(s);
        }
        void wake() override {}
        void quit() override {}
    };

    NestedTestModel model;
    prism::model_app(
        prism::Backend{std::make_unique<CapturingBackend>(captured)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    REQUIRE(captured != nullptr);
    CHECK(captured->geometry.size() == 3);  // inner.count + inner.name + flag
}

TEST_CASE("model_app rebuilds snapshot when field changes via TestBackend") {
    // Field change between events triggers re-render
    // This verifies the dirty->rebuild pipeline

    struct Counter { prism::Field<int> n{"N", 0}; };

    std::vector<std::shared_ptr<const prism::SceneSnapshot>> snapshots;

    struct CollectingBackend final : public prism::BackendBase {
        std::vector<std::shared_ptr<const prism::SceneSnapshot>>& snaps;
        Counter& model;
        explicit CollectingBackend(
            std::vector<std::shared_ptr<const prism::SceneSnapshot>>& s,
            Counter& m)
            : snaps(s), model(m) {}

        void run(std::function<void(const prism::InputEvent&)> cb) override {
            // Simulate: an event arrives, then model changes, then close
            cb(prism::MouseButton{{0, 0}, 1, true});
            model.n.set(7);
            cb(prism::MouseButton{{0, 0}, 1, false});
            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            snaps.push_back(std::move(s));
        }
        void wake() override {}
        void quit() override {}
    };

    Counter model;
    prism::model_app(
        prism::Backend{std::make_unique<CollectingBackend>(snapshots, model)},
        prism::BackendConfig{.width = 400, .height = 300},
        model
    );

    // At least 2 snapshots: initial + after event processing
    CHECK(snapshots.size() >= 2);
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `backend_base_tests` dictionary:

```meson
'model_app' : files('test_model_app.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir model_app`
Expected: FAIL — `prism/core/model_app.hpp` not found

- [ ] **Step 4: Implement model_app**

Create `include/prism/core/model_app.hpp`:

```cpp
#pragma once

#include <prism/core/backend.hpp>
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

    // Initial snapshot
    backend.submit(tree.build_snapshot(w, h, ++version));
    backend.wake();

    while (running.load(std::memory_order_relaxed)) {
        input_pending.wait(false, std::memory_order_acquire);
        input_pending.store(false, std::memory_order_relaxed);

        while (auto ev = input_queue.pop()) {
            if (std::holds_alternative<WindowClose>(*ev)) {
                running.store(false, std::memory_order_relaxed);
                break;
            }
            if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                w = resize->width;
                h = resize->height;
            }
            // Future: hit_test + event routing to widget senders
        }

        if (!running.load(std::memory_order_relaxed)) break;

        // Rebuild snapshot (only dirty widgets re-record in the future)
        backend.submit(tree.build_snapshot(w, h, ++version));
        backend.wake();
        tree.clear_dirty();
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

- [ ] **Step 5: Update prism.hpp umbrella header**

Read current `include/prism/prism.hpp` and add the new includes. The header should include:

```cpp
#pragma once

// Existing includes (keep all of them)
#include <prism/core/app.hpp>
#include <prism/core/ui.hpp>
// ... all existing includes ...

// New: model-driven API
#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/core/model_app.hpp>
#include <prism/core/reflect.hpp>
#include <prism/core/widget_tree.hpp>
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `meson test -C builddir model_app`
Expected: All 3 tests PASS

Also run all tests to verify nothing is broken:
Run: `meson test -C builddir`
Expected: All existing + new tests PASS

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/model_app.hpp include/prism/prism.hpp tests/test_model_app.cpp tests/meson.build
git commit -m "feat: add model_app() — model-driven entry point with persistent WidgetTree"
```

---

### Task 7: List<T> Observable Collection

**Files:**
- Create: `include/prism/core/list.hpp`
- Create: `tests/test_list.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_list.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/list.hpp>

#include <string>

TEST_CASE("List starts empty") {
    prism::List<int> list;
    CHECK(list.size() == 0);
    CHECK(list.empty());
}

TEST_CASE("List::push_back adds and notifies") {
    prism::List<int> list;
    size_t inserted_idx = 999;
    int inserted_val = -1;
    auto conn = list.on_insert().connect([&](size_t i, const int& v) {
        inserted_idx = i;
        inserted_val = v;
    });
    list.push_back(42);
    CHECK(list.size() == 1);
    CHECK(list[0] == 42);
    CHECK(inserted_idx == 0);
    CHECK(inserted_val == 42);
}

TEST_CASE("List::erase removes and notifies") {
    prism::List<std::string> list;
    list.push_back("a");
    list.push_back("b");
    list.push_back("c");

    size_t removed_idx = 999;
    auto conn = list.on_remove().connect([&](size_t i) { removed_idx = i; });

    list.erase(1);
    CHECK(list.size() == 2);
    CHECK(list[0] == "a");
    CHECK(list[1] == "c");
    CHECK(removed_idx == 1);
}

TEST_CASE("List::set updates in place and notifies") {
    prism::List<int> list;
    list.push_back(1);
    list.push_back(2);

    size_t updated_idx = 999;
    int updated_val = -1;
    auto conn = list.on_update().connect([&](size_t i, const int& v) {
        updated_idx = i;
        updated_val = v;
    });

    list.set(1, 20);
    CHECK(list[1] == 20);
    CHECK(updated_idx == 1);
    CHECK(updated_val == 20);
}

TEST_CASE("List iteration") {
    prism::List<int> list;
    list.push_back(10);
    list.push_back(20);
    list.push_back(30);

    int sum = 0;
    for (const auto& v : list) sum += v;
    CHECK(sum == 60);
}

TEST_CASE("List connection RAII") {
    prism::List<int> list;
    int calls = 0;
    {
        auto conn = list.on_insert().connect([&](size_t, const int&) { ++calls; });
        list.push_back(1);
        CHECK(calls == 1);
    }
    list.push_back(2);
    CHECK(calls == 1);  // disconnected
}
```

- [ ] **Step 2: Register test in Meson**

Add to `tests/meson.build` in the `headless_tests` dictionary:

```meson
'list' : files('test_list.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir list`
Expected: FAIL — `prism/core/list.hpp` not found

- [ ] **Step 4: Implement List<T>**

Create `include/prism/core/list.hpp`:

```cpp
#pragma once

#include <prism/core/connection.hpp>

#include <cstddef>
#include <vector>

namespace prism {

template <typename T>
class List {
public:
    void push_back(T item) {
        items_.push_back(std::move(item));
        inserted_.emit(items_.size() - 1, items_.back());
    }

    void erase(size_t index) {
        items_.erase(items_.begin() + static_cast<ptrdiff_t>(index));
        removed_.emit(index);
    }

    void set(size_t index, T value) {
        items_[index] = std::move(value);
        updated_.emit(index, items_[index]);
    }

    [[nodiscard]] const T& operator[](size_t i) const { return items_[i]; }
    [[nodiscard]] size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

    SenderHub<size_t, const T&>& on_insert() { return inserted_; }
    SenderHub<size_t>& on_remove() { return removed_; }
    SenderHub<size_t, const T&>& on_update() { return updated_; }

private:
    std::vector<T> items_;
    SenderHub<size_t, const T&> inserted_;
    SenderHub<size_t> removed_;
    SenderHub<size_t, const T&> updated_;
};

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test -C builddir list`
Expected: All 6 tests PASS

- [ ] **Step 6: Add List to umbrella header**

Add `#include <prism/core/list.hpp>` to `include/prism/prism.hpp`.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/list.hpp include/prism/prism.hpp tests/test_list.cpp tests/meson.build
git commit -m "feat: add List<T> observable collection with insert/remove/update senders"
```

---

### Task 8: Example — Model-Driven Dashboard

**Files:**
- Create: `examples/model_dashboard.cpp`
- Modify: `examples/meson.build`

- [ ] **Step 1: Write the example**

Create `examples/model_dashboard.cpp`:

```cpp
#include <prism/prism.hpp>

#include <string>

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

    // Cross-component signal: log when dark_mode changes
    auto conn = dashboard.settings.dark_mode.on_change().connect(
        [](const bool& dark) {
            // In a real app this would retheme — for now just a signal test
            (void)dark;
        }
    );

    prism::model_app("PRISM Model Dashboard", dashboard);
}
```

- [ ] **Step 2: Register example in Meson**

Add to `examples/meson.build`:

```meson
executable('model_dashboard', 'model_dashboard.cpp',
  dependencies : [prism_dep],
)
```

- [ ] **Step 3: Build and verify it compiles**

Run: `meson compile -C builddir`
Expected: Compiles without errors. The example window opens showing placeholder widgets for each Field.

- [ ] **Step 4: Run all tests one final time**

Run: `meson test -C builddir`
Expected: All tests PASS (existing + new)

- [ ] **Step 5: Commit**

```bash
git add examples/model_dashboard.cpp examples/meson.build
git commit -m "feat: add model-driven dashboard example using Field<T> + reflection"
```
