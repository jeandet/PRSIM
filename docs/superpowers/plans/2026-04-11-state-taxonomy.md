# State Taxonomy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `Derived<T>` (computed, read-only) and `Shared<T>` (cross-thread, read-only in UI) to PRISM's type system, fully integrated with reflection, delegates, transactions, and the event loop.

**Architecture:** Two new core types in `prism::core` following the same patterns as `Field<T>` and `State<T>`. `Derived<T>` connects to source `on_change()` hubs and recomputes on change. `Shared<T>` uses `atomic_cell<T>` for storage and an MPSC notification queue with coalescing. Both render as read-only widgets via the existing `Delegate<T>` infrastructure. Transaction support adds two-phase commit: source callbacks first, then derived recomputation.

**Tech Stack:** C++23, doctest, Meson

**Spec:** `docs/superpowers/specs/2026-04-11-state-taxonomy-design.md`

---

### Task 1: Add `is_derived_v` and `is_shared_v` traits

**Files:**
- Modify: `include/prism/core/traits.hpp`
- Test: `tests/test_state.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_state.cpp`:

```cpp
#include <prism/core/traits.hpp>

// Forward-declare Derived and Shared for trait tests
namespace prism::core {
template <typename T> struct Derived;
template <typename T> struct Shared;
}

TEST_CASE("is_derived_v identifies Derived<T>") {
    CHECK(prism::core::is_derived_v<prism::core::Derived<int>>);
    CHECK_FALSE(prism::core::is_derived_v<prism::core::Field<int>>);
    CHECK_FALSE(prism::core::is_derived_v<int>);
}

TEST_CASE("is_shared_v identifies Shared<T>") {
    CHECK(prism::core::is_shared_v<prism::core::Shared<float>>);
    CHECK_FALSE(prism::core::is_shared_v<prism::core::Field<float>>);
    CHECK_FALSE(prism::core::is_shared_v<int>);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test state -C builddir --print-errorlogs`
Expected: compile error — `is_derived_v` and `is_shared_v` not defined

- [ ] **Step 3: Implement the traits**

In `include/prism/core/traits.hpp`, add after the `is_list_v` block (before the closing `}` of namespace `prism::core`):

```cpp
template <typename> struct Derived;

template <typename T>
struct is_derived : std::false_type {};

template <typename T>
struct is_derived<Derived<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_derived_v = is_derived<T>::value;

template <typename> struct Shared;

template <typename T>
struct is_shared : std::false_type {};

template <typename T>
struct is_shared<Shared<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_shared_v = is_shared<T>::value;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test state -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/traits.hpp tests/test_state.cpp
git commit -m "feat: add is_derived_v and is_shared_v traits"
```

---

### Task 2: Implement `Derived<T>` core type

**Files:**
- Create: `include/prism/core/derived.hpp`
- Test: `tests/test_derived.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_derived.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("Derived recomputes when source changes") {
    prism::Field<int> a{2};
    prism::Field<int> b{3};
    prism::core::Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    CHECK(sum.get() == 5);

    a.set(10);
    CHECK(sum.get() == 13);

    b.set(7);
    CHECK(sum.get() == 17);
}

TEST_CASE("Derived fires on_change when value changes") {
    prism::Field<int> x{1};
    prism::core::Derived<int> doubled{[&] { return x.get() * 2; }, x};

    int calls = 0;
    int last = -1;
    auto conn = doubled.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    x.set(5);
    CHECK(calls == 1);
    CHECK(last == 10);
}

TEST_CASE("Derived suppresses on_change when recomputed value unchanged") {
    prism::Field<int> x{5};
    prism::core::Derived<int> clamped{[&] { return std::min(x.get(), 10); }, x};

    int calls = 0;
    auto conn = clamped.on_change().connect([&](const int&) { ++calls; });

    // Setting x to 20 still clamps to 10, but the derived value changes from 5 to 10
    x.set(20);
    CHECK(calls == 1);
    CHECK(clamped.get() == 10);

    // Setting x to 30 still clamps to 10 — no change, no callback
    x.set(30);
    CHECK(calls == 1);
}

TEST_CASE("Derived observe() works fire-and-forget") {
    prism::Field<int> x{0};
    prism::core::Derived<int> d{[&] { return x.get() + 1; }, x};

    int observed = -1;
    d.observe([&](const int& v) { observed = v; });

    x.set(9);
    CHECK(observed == 10);
}

TEST_CASE("Derived implicit conversion") {
    prism::Field<int> x{42};
    prism::core::Derived<int> d{[&] { return x.get(); }, x};

    int val = d;
    CHECK(val == 42);
}

TEST_CASE("Derived from multiple source types (Field + State)") {
    prism::Field<int> a{1};
    prism::core::State<int> b{2};
    prism::core::Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    CHECK(sum.get() == 3);

    a.set(10);
    CHECK(sum.get() == 12);

    b.set(20);
    CHECK(sum.get() == 30);
}

TEST_CASE("Derived chain: derived from derived") {
    prism::Field<int> x{2};
    prism::core::Derived<int> doubled{[&] { return x.get() * 2; }, x};
    prism::core::Derived<int> quadrupled{[&] { return doubled.get() * 2; }, doubled};

    CHECK(quadrupled.get() == 8);

    x.set(5);
    CHECK(doubled.get() == 10);
    CHECK(quadrupled.get() == 20);
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build` (before the closing `}`):

```python
  'derived' : files('test_derived.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test derived -C builddir --print-errorlogs`
Expected: compile error — `prism/core/derived.hpp` not found

- [ ] **Step 4: Implement `Derived<T>`**

Create `include/prism/core/derived.hpp`:

```cpp
#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/transaction.hpp>

#include <functional>
#include <vector>

namespace prism::core {

template <typename T>
struct Derived {
    template <typename Fn, typename... Sources>
    Derived(Fn&& compute, Sources&... sources)
        : compute_(std::forward<Fn>(compute))
        , value_(compute_())
    {
        (subscribe(sources), ...);
    }

    const T& get() const { return value_; }
    operator const T&() const { return value_; }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    std::function<T()> compute_;
    T value_;
    SenderHub<const T&> changed_;
    std::vector<Connection> connections_;
    std::vector<Connection> observers_;

    void recompute() {
        T new_value = compute_();
        if (value_ == new_value) return;
        value_ = std::move(new_value);
        if (transaction_active()) {
            current_transaction().queue.push_back({
                static_cast<void*>(&changed_),
                [this] { changed_.emit(value_); }
            });
        } else {
            changed_.emit(value_);
        }
    }

    template <typename Source>
    void subscribe(Source& source) {
        connections_.push_back(
            source.on_change().connect([this](const auto&) { recompute(); })
        );
    }
};

} // namespace prism::core
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test derived -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/derived.hpp tests/test_derived.cpp tests/meson.build
git commit -m "feat: add Derived<T> computed observable type"
```

---

### Task 3: `Derived<T>` transaction integration

**Files:**
- Modify: `include/prism/core/transaction.hpp`
- Test: `tests/test_derived.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_derived.cpp`:

```cpp
#include <prism/core/transaction.hpp>

TEST_CASE("Derived defers recomputation during transaction") {
    prism::Field<int> a{1};
    prism::Field<int> b{2};
    prism::core::Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    int calls = 0;
    auto conn = sum.on_change().connect([&](const int&) { ++calls; });

    prism::transaction([&] {
        a.set(10);
        b.set(20);
        // During transaction, derived has not recomputed yet
        CHECK(calls == 0);
    });

    // After commit: derived recomputes once, fires once
    CHECK(calls == 1);
    CHECK(sum.get() == 30);
}

TEST_CASE("Derived diamond dependency fires once in transaction") {
    prism::Field<int> x{1};
    prism::core::Derived<int> a{[&] { return x.get() + 1; }, x};
    prism::core::Derived<int> b{[&] { return x.get() * 2; }, x};
    prism::core::Derived<int> c{[&] { return a.get() + b.get(); }, a, b};

    int c_calls = 0;
    auto conn = c.on_change().connect([&](const int&) { ++c_calls; });

    prism::transaction([&] {
        x.set(5);
    });

    CHECK(a.get() == 6);
    CHECK(b.get() == 10);
    CHECK(c.get() == 16);
    // c should fire at most once (coalesced)
    CHECK(c_calls == 1);
}
```

- [ ] **Step 2: Run tests to verify behavior**

Run: `meson test derived -C builddir --print-errorlogs`

The transaction deferral test should already pass because `Derived::recompute()` uses `transaction_active()` and defers via the queue. The diamond test should also pass because the transaction coalesces by sender pointer.

If both pass, the existing transaction infrastructure already handles `Derived<T>` correctly — no changes to `transaction.hpp` needed.

- [ ] **Step 3: If tests fail, add two-phase commit to `TransactionGuard::flush`**

If the diamond test shows `c_calls > 1`, modify `TransactionGuard::flush` in `include/prism/core/transaction.hpp` to process the queue in two passes: first fire all non-derived callbacks, then fire derived callbacks. This would require tagging `DeferredEmit` entries. However, the current coalescing by sender pointer should already handle this — verify before making changes.

- [ ] **Step 4: Commit**

```bash
git add tests/test_derived.cpp
git commit -m "test: Derived<T> transaction deferral and diamond dependency"
```

If `transaction.hpp` was modified:
```bash
git add include/prism/core/transaction.hpp tests/test_derived.cpp
git commit -m "feat: two-phase transaction commit for Derived<T>"
```

---

### Task 4: Implement `Shared<T>` core type

**Files:**
- Create: `include/prism/core/shared.hpp`
- Test: `tests/test_shared.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_shared.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/shared.hpp>
#include <string>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("Shared stores and retrieves value") {
    prism::core::Shared<int> s{42};
    CHECK(s.get() == 42);
}

TEST_CASE("Shared::set updates value atomically") {
    prism::core::Shared<int> s{0};
    s.set(99);
    CHECK(s.get() == 99);
}

TEST_CASE("Shared::drain_notifications fires on_change on UI thread") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    int last = -1;
    auto conn = s.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    s.set(10);
    // Callback has not fired yet — notification is queued
    CHECK(calls == 0);

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(last == 10);
}

TEST_CASE("Shared coalesces multiple sets into one notification") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    auto conn = s.on_change().connect([&](const int&) { ++calls; });

    s.set(1);
    s.set(2);
    s.set(3);

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(s.get() == 3);
}

TEST_CASE("Shared::drain with no pending notifications is a no-op") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    auto conn = s.on_change().connect([&](const int&) { ++calls; });

    s.drain_notifications();
    CHECK(calls == 0);
}

TEST_CASE("Shared::observe works fire-and-forget") {
    prism::core::Shared<int> s{0};
    int observed = -1;
    s.observe([&](const int& v) { observed = v; });

    s.set(7);
    s.drain_notifications();
    CHECK(observed == 7);
}

TEST_CASE("Shared set from another thread, drain on main") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    int last = -1;
    auto conn = s.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    std::thread writer([&] {
        s.set(42);
    });
    writer.join();

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(last == 42);
}

TEST_CASE("Shared has no implicit conversion operator") {
    prism::core::Shared<int> s{5};
    // Must use .get() — no implicit conversion
    // int x = s;  // should not compile
    int x = s.get();
    CHECK(x == 5);
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```python
  'shared' : files('test_shared.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test shared -C builddir --print-errorlogs`
Expected: compile error — `prism/core/shared.hpp` not found

- [ ] **Step 4: Implement `Shared<T>`**

Create `include/prism/core/shared.hpp`:

```cpp
#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/connection.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace prism::core {

template <typename T>
struct Shared {
    Shared() : cell_(T{}) {}
    Shared(T init) : cell_(std::move(init)) {}

    T get() const {
        auto ptr = cell_.load();
        return ptr ? *ptr : T{};
    }

    void set(T new_value) {
        cell_.store(std::move(new_value));
        pending_.store(true, std::memory_order_release);
    }

    void drain_notifications() {
        if (!pending_.exchange(false, std::memory_order_acquire))
            return;
        changed_.emit(get());
    }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    atomic_cell<T> cell_;
    std::atomic<bool> pending_{false};
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

} // namespace prism::core
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test shared -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/shared.hpp tests/test_shared.cpp tests/meson.build
git commit -m "feat: add Shared<T> cross-thread observable type"
```

---

### Task 5: Update reflection to discover `Derived<T>` and `Shared<T>`

**Files:**
- Modify: `include/prism/core/reflect.hpp`
- Modify: `include/prism/core/traits.hpp` (update `component_type` concept)
- Test: `tests/test_reflect.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_reflect.cpp` (this file uses `#if __cpp_impl_reflection` guards — add inside them):

```cpp
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>

TEST_CASE("for_each_member visits Derived and Shared members") {
    struct Model {
        prism::Field<int> x{1};
        prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};
        prism::core::Shared<float> temperature{22.5f};
    };

    Model m;
    int count = 0;
    prism::core::for_each_member(m, [&](auto&) { ++count; });
    CHECK(count == 3);
}

TEST_CASE("is_component_v includes structs with Derived or Shared") {
    struct DerivedOnly {
        prism::Field<int> x{0};
        prism::core::Derived<int> y{[this] { return x.get(); }, x};
    };
    struct SharedOnly {
        prism::core::Shared<int> s{0};
    };

    CHECK(prism::core::is_component_v<DerivedOnly>);
    CHECK(prism::core::is_component_v<SharedOnly>);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test reflect -C builddir --print-errorlogs`
Expected: FAIL — `for_each_member` does not visit `Derived`/`Shared`, `is_component_v` is false for `SharedOnly`

- [ ] **Step 3: Update `for_each_member` in `reflect.hpp`**

In `include/prism/core/reflect.hpp`, add includes at the top:

```cpp
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>
```

Change the `for_each_member` body's `if constexpr` condition from:

```cpp
if constexpr (is_field_v<M> || is_component_v<M>) {
```

to:

```cpp
if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M> || is_component_v<M>) {
```

- [ ] **Step 4: Update `check_is_component` in `reflect.hpp`**

Change the condition from:

```cpp
if constexpr (is_field_v<M> || is_state_v<M>) found = true;
```

to:

```cpp
if constexpr (is_field_v<M> || is_state_v<M> || is_derived_v<M> || is_shared_v<M>) found = true;
```

- [ ] **Step 5: Update `component_type` concept in `traits.hpp`**

Change from:

```cpp
template <typename T>
concept component_type = std::is_class_v<T> && !is_field_v<T> && !is_state_v<T>;
```

to:

```cpp
template <typename T>
concept component_type = std::is_class_v<T> && !is_field_v<T> && !is_state_v<T>
    && !is_derived_v<T> && !is_shared_v<T>;
```

- [ ] **Step 6: Run test to verify it passes**

Run: `meson test reflect -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/reflect.hpp include/prism/core/traits.hpp tests/test_reflect.cpp
git commit -m "feat: reflection discovers Derived<T> and Shared<T> members"
```

---

### Task 6: Read-only widget nodes for `Derived<T>` and `Shared<T>`

**Files:**
- Modify: `include/prism/ui/widget_node.hpp` (add `node_readonly_leaf`)
- Modify: `include/prism/app/widget_tree.hpp` (ViewBuilder + build_node_tree)
- Test: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_widget_tree.cpp`:

```cpp
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>

TEST_CASE("Derived<T> creates a read-only widget node") {
    struct Model {
        prism::Field<int> x{5};
        prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(x);
            vb.widget(doubled);
        }
    };

    Model m;
    prism::WidgetTree tree(m);
    // root has 2 children: x (interactive) and doubled (read-only)
    CHECK(tree.root().children.size() == 2);
    // doubled node has no focus
    CHECK(tree.root().children[1].focus_policy == prism::FocusPolicy::none);
}

TEST_CASE("Shared<T> creates a read-only widget node") {
    struct Model {
        prism::core::Shared<float> temp{22.5f};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(temp);
        }
    };

    Model m;
    prism::WidgetTree tree(m);
    CHECK(tree.root().children.size() == 1);
    CHECK(tree.root().children[0].focus_policy == prism::FocusPolicy::none);
}

TEST_CASE("Derived<T> widget re-renders when source changes") {
    struct Model {
        prism::Field<int> x{5};
        prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(x);
            vb.widget(doubled);
        }
    };

    Model m;
    prism::WidgetTree tree(m);
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    m.x.set(10);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: compile error — `ViewBuilder::widget` does not accept `Derived<T>` or `Shared<T>`

- [ ] **Step 3: Add `node_readonly_leaf` to `widget_node.hpp`**

In `include/prism/ui/widget_node.hpp`, add after the existing `node_leaf` function (around line 131):

```cpp
template <typename T, typename Observable>
Node node_readonly_leaf(Observable& obs, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;

    n.build_widget = [&obs](WidgetNode& wn) {
        wn.focus_policy = FocusPolicy::none;
        wn.record = [&obs](WidgetNode& node) {
            node.draws.clear();
            node.overlay_draws.clear();
            // Create a temporary Field<T> for Delegate<T>::record compatibility
            Field<T> tmp{obs.get()};
            Delegate<T>::record(node.draws, tmp, node);
        };
        wn.record(wn);
        // No input wiring — read-only
    };

    n.on_change = [&obs](std::function<void()> cb) -> Connection {
        return obs.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}
```

- [ ] **Step 4: Add `ViewBuilder::widget` overloads for `Derived<T>` and `Shared<T>`**

In `include/prism/app/widget_tree.hpp`, in the `ViewBuilder` class's public section (after the existing `widget(Field<T>&)` at line 91), add:

```cpp
        template <typename T>
        void widget(Derived<T>& derived) {
            current_parent().children.push_back(
                node_readonly_leaf<T>(derived, tree_.next_id_));
        }

        template <typename T>
        void widget(Shared<T>& shared) {
            current_parent().children.push_back(
                node_readonly_leaf<T>(shared, tree_.next_id_));
        }
```

Also add the includes at the top of `widget_tree.hpp`:

```cpp
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>
```

- [ ] **Step 5: Update `build_node_tree` reflection path**

In `include/prism/app/widget_tree.hpp`, in the `build_node_tree` method's reflection branch (around line 901-912), add handling for `Derived<T>` and `Shared<T>`:

Change from:
```cpp
if constexpr (is_state_v<M>) {
    // invisible observable — no widget
} else if constexpr (is_field_v<M>) {
    root.children.push_back(node_leaf(member, next_id_));
} else if constexpr (is_component_v<M>) {
    root.children.push_back(build_node_tree(member));
}
```

To:
```cpp
if constexpr (is_state_v<M>) {
    // invisible observable — no widget
} else if constexpr (is_field_v<M>) {
    root.children.push_back(node_leaf(member, next_id_));
} else if constexpr (is_derived_v<M>) {
    using Inner = std::remove_cvref_t<decltype(member.get())>;
    root.children.push_back(node_readonly_leaf<Inner>(member, next_id_));
} else if constexpr (is_shared_v<M>) {
    using Inner = std::remove_cvref_t<decltype(member.get())>;
    root.children.push_back(node_readonly_leaf<Inner>(member, next_id_));
} else if constexpr (is_component_v<M>) {
    root.children.push_back(build_node_tree(member));
}
```

- [ ] **Step 6: Update `check_unplaced_fields` to also warn on unplaced `Derived`/`Shared`**

In the `check_unplaced_fields` method (around line 920-941), extend the inner `if constexpr` to also check `is_derived_v<M>` and `is_shared_v<M>`:

```cpp
if constexpr (is_field_v<M> || is_derived_v<M> || is_shared_v<M>) {
```

- [ ] **Step 7: Update `ViewBuilder::item` to handle new types**

In the `ViewBuilder` private section (around line 109-111), add overloads:

```cpp
        template <typename T>
        void item(Derived<T>& d) { widget(d); }
        template <typename T>
        void item(Shared<T>& s) { widget(s); }
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 9: Commit**

```bash
git add include/prism/ui/widget_node.hpp include/prism/app/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: read-only widget nodes for Derived<T> and Shared<T>"
```

---

### Task 7: Integrate `Shared<T>` drain into `model_app` event loop

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (collect drain callbacks)
- Modify: `include/prism/app/model_app.hpp` (drain each tick)
- Test: `tests/test_model_app.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_model_app.cpp`:

```cpp
#include <prism/core/shared.hpp>
#include <thread>

TEST_CASE("Shared<T> updates propagate through model_app event loop") {
    struct Model {
        prism::core::Shared<int> value{0};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.widget(value);
        }
    };

    Model m;
    prism::TestBackend backend;
    auto& window = backend.create_window({.title = "test"});
    int frame_count = 0;

    std::thread app_thread([&] {
        prism::model_app(backend, window, m, [&](prism::AppContext&) {
            m.value.set(42);
        });
    });

    backend.wait_ready();
    // Give event loop time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    backend.inject_close(window.id());
    app_thread.join();

    CHECK(m.value.get() == 42);
}
```

- [ ] **Step 2: Run test to verify it fails or passes**

Run: `meson test model_app -C builddir --print-errorlogs`

The test verifies the plumbing works end-to-end. It may pass already if `Shared<T>` fires observers during `set()` synchronously for same-thread calls, but the drain-based path needs explicit integration.

- [ ] **Step 3: Add drain callback collection to `WidgetTree`**

In `include/prism/app/widget_tree.hpp`, add a member to `WidgetTree`:

```cpp
    std::vector<std::function<void()>> drain_callbacks_;
```

In the `connect_dirty` method, when processing a leaf node, check if the node was created from a `Shared<T>` and if so, the drain callback was already stored. Actually, the drain needs to be collected during `build_node_tree`. Add a `drain_fns` field to `Node`:

In `include/prism/ui/node.hpp`, add to `Node`:

```cpp
    std::function<void()> drain_fn;
```

In `widget_node.hpp`, in `node_readonly_leaf`, for `Shared<T>` specifically, set the drain_fn. However, `node_readonly_leaf` is generic. Instead, add the drain_fn in the `ViewBuilder::widget(Shared<T>&)` overload:

In `widget_tree.hpp`, modify the `widget(Shared<T>&)` overload:

```cpp
        template <typename T>
        void widget(Shared<T>& shared) {
            auto node = node_readonly_leaf<T>(shared, tree_.next_id_);
            node.drain_fn = [&shared] { shared.drain_notifications(); };
            current_parent().children.push_back(std::move(node));
        }
```

In the `build_node_tree` reflection path, for `is_shared_v<M>`:

```cpp
} else if constexpr (is_shared_v<M>) {
    using Inner = std::remove_cvref_t<decltype(member.get())>;
    auto node = node_readonly_leaf<Inner>(member, next_id_);
    node.drain_fn = [&member] { member.drain_notifications(); };
    root.children.push_back(std::move(node));
}
```

Then collect drain_fns during `connect_dirty` or in the constructor. Add a helper in `WidgetTree`:

```cpp
    void collect_drains(const Node& node) {
        if (node.drain_fn)
            drain_callbacks_.push_back(node.drain_fn);
        for (const auto& child : node.children)
            collect_drains(child);
    }
```

Call it in the `WidgetTree` constructor, after `build_node_tree` and before `build_widget_node`:

```cpp
    template <typename Model>
    explicit WidgetTree(Model& model) {
        auto node_tree = build_node_tree(model);
        collect_drains(node_tree);  // <-- add this line
        root_ = build_widget_node(node_tree);
        propagate_theme(root_);
        connect_dirty(node_tree, root_);
        build_index(root_);
        clear_dirty();
    }
```

Add a public accessor:

```cpp
    void drain_shared() {
        for (auto& fn : drain_callbacks_)
            fn();
    }
```

- [ ] **Step 4: Call `drain_shared()` in `model_app` event loop**

In `include/prism/app/model_app.hpp`, in the event handler lambda (around line 173), add `tree.drain_shared()` right before the dirty check. Change:

```cpp
                    if (tree.any_dirty() || needs_publish)
                        publish();
```

to:

```cpp
                    tree.drain_shared();
                    if (tree.any_dirty() || needs_publish)
                        publish();
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson test model_app -C builddir --print-errorlogs`
Expected: all PASS

Also run the full suite to check for regressions:
Run: `meson test -C builddir --print-errorlogs`

- [ ] **Step 6: Commit**

```bash
git add include/prism/ui/node.hpp include/prism/ui/widget_node.hpp include/prism/app/widget_tree.hpp include/prism/app/model_app.hpp tests/test_model_app.cpp
git commit -m "feat: integrate Shared<T> drain into model_app event loop"
```

---

### Task 8: Update master header and run full test suite

**Files:**
- Modify: `include/prism/prism.hpp`
- No new tests — full suite regression check

- [ ] **Step 1: Add new headers to `prism.hpp`**

In `include/prism/prism.hpp`, in the `// core` section, add after the `#include <prism/core/state.hpp>` line:

```cpp
#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>
```

- [ ] **Step 2: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`
Expected: all PASS, no regressions

- [ ] **Step 3: Build and run existing examples to verify no breakage**

Run: `meson compile -C builddir model_dashboard`
Expected: compiles successfully

- [ ] **Step 4: Commit**

```bash
git add include/prism/prism.hpp
git commit -m "feat: include Derived<T> and Shared<T> in master header"
```

---

### Task 9: `Derived<T>` as `depends_on` source for canvas/table

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (CanvasHandle, TableBuilder)
- Test: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_widget_tree.cpp`:

```cpp
TEST_CASE("canvas depends_on accepts Derived<T>") {
    struct Model {
        prism::Field<int> x{5};
        prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};

        void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
            dl.filled_rect(bounds, prism::Color::rgba(0, 0, 0));
        }

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.canvas(*this).depends_on(doubled);
        }
    };

    Model m;
    prism::WidgetTree tree(m);
    tree.clear_dirty();

    m.x.set(10);  // triggers doubled recomputation
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: compile error — `depends_on` does not accept `Derived<T>`

- [ ] **Step 3: Add `depends_on` overloads for `Derived<T>` and `Shared<T>`**

In `include/prism/app/widget_tree.hpp`, add to `CanvasHandle`:

```cpp
            template <typename U>
            CanvasHandle& depends_on(Derived<U>& derived) {
                node_ref.dependencies.push_back(
                    [&derived](std::function<void()> cb) -> Connection {
                        return derived.on_change().connect(
                            [cb = std::move(cb)](const U&) { cb(); });
                    }
                );
                return *this;
            }

            template <typename U>
            CanvasHandle& depends_on(Shared<U>& shared) {
                node_ref.dependencies.push_back(
                    [&shared](std::function<void()> cb) -> Connection {
                        return shared.on_change().connect(
                            [cb = std::move(cb)](const U&) { cb(); });
                    }
                );
                return *this;
            }
```

Add the same overloads to `TableBuilder`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson test widget_tree -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: canvas/table depends_on accepts Derived<T> and Shared<T>"
```

---

### Task 10: Full regression test and cleanup

**Files:**
- No new files — verification only

- [ ] **Step 1: Run full test suite**

Run: `meson test -C builddir --print-errorlogs`
Expected: all PASS

- [ ] **Step 2: Build all examples**

Run: `meson compile -C builddir`
Expected: clean build, no warnings

- [ ] **Step 3: Verify existing examples still work**

Run: `./builddir/model_dashboard` (manual check — should display and be interactive)

- [ ] **Step 4: Final commit if any cleanup was needed**

Only commit if changes were required during the regression check.
