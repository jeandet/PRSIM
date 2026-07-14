# Live Object Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give any plain reflectable struct living behind a `Shared<T>` a live, per-field editable UI panel — zero boilerplate, no hand-written mirror struct — closing strategic priority #1 ("struct becomes form/inspector instantly").

**Architecture:** A reflection-generated `FieldMirror<T>` holds one `Field<M>` (or nested `FieldMirror<M>`) per member of `T`, synthesized at compile time via `std::meta::substitute` — no new nominal type for `T` is created, the mirror is a positional `std::tuple`. `Inspector<T>` wraps a `Shared<T>&`, seeds the mirror, and wires bidirectional sync: remote updates flow in via `Shared<T>::observe()`, local edits flow out by reconstructing a whole `T` and calling `Shared<T>::set()`. Both types are ordinary PRISM components with a `view()`, so they compose with the existing `ViewBuilder`/`WidgetTree`/`model_app` machinery unchanged.

**Tech Stack:** C++26, GCC 16 `-freflection` (P2996), Meson, doctest.

## Global Constraints

- Reflection code is gated behind `#if __cpp_impl_reflection` (project convention — this feature has no pre-C++26 fallback, matching other reflection-only features).
- Namespace: `prism::inspector`, in `include/prism/widgets/` (new files depend on `core`, `ui`, and `app` — a new but non-circular dependency direction, since nothing in `app/` depends on `widgets/`).
- No new test infrastructure: register new test binaries in `tests/meson.build`'s `headless_tests` dict, following the existing `'name' : files('test_name.cpp')` pattern — no SDL/backend needed.
- Concurrency model: local→remote push always sets the *whole* `T`; a remote update racing an in-flight local edit is last-write-wins at the whole-`T` level (same semantics as `atomic_cell` elsewhere) — no field-level merge is implemented, and no test should assert merge behavior.
- Single fixed `T` per `Inspector<T>` instance — no runtime type switching.
- Leaf classification for mirror slots: a member `M` is a **leaf** (`Field<M>`) if `Numeric<M> || StringLike<M> || ScopedEnum<M>` (from `prism/ui/delegate.hpp`); otherwise, if `std::is_class_v<M>`, it **recurses** into a nested `FieldMirror<M>`. (`Widget<T>`'s primary template is never empty — it always provides a fallback renderer — so `is_widget_v<T>` is trivially true for every type and cannot be used to distinguish "has a real widget" from "is a plain struct"; do not use it for this classification.)
- Every leaf is rendered as a labeled row: a static `Field<Label<std::string>>` holding the member name via `std::meta::identifier_of`, plus the actual value `Field<M>` — both placed in the same mirror slot (`LeafSlot<M>`), stacked vertically (name above value) via the default reflection walk. Do not attempt side-by-side (hstack) layout in this plan — that's a follow-up refinement, not required for v1.

---

### Task 1: `FieldMirror<T>` — flat reflection-derived tuple with labeled leaf slots

**Files:**
- Create: `include/prism/widgets/field_mirror.hpp`
- Test: `tests/test_field_mirror.cpp`
- Modify: `tests/meson.build` (register test)

**Interfaces:**
- Consumes: `prism::core::Field<T>` (`get()`, `set(T)`, `observe(std::function<void(const T&)>)`), `prism::ui::Numeric<T>`/`StringLike<T>`/`ScopedEnum<T>` concepts, `prism::ui::Label<T>` sentinel (`T value`).
- Produces: `prism::inspector::LeafSlot<M>` (members: `Field<Label<std::string>> name`, `Field<M> value`), `prism::inspector::FieldMirror<T>` (member: `FieldMirrorTuple<T> slots`; methods: `void sync_from(const T&)`, `T build() const`, `template<typename Fn> void for_each_leaf(Fn&&)`). This task covers **flat structs only** (all members are leaf types) — nested-struct recursion is Task 2.

- [ ] **Step 1: Write the failing test for tuple shape and flat sync/build round-trip**

```cpp
// tests/test_field_mirror.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/field_mirror.hpp>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism::inspector {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot; using namespace inspector;
}

struct DeviceState {
    float voltage;
    int mode;
    bool enabled;
    std::string name;
};

TEST_CASE("FieldMirror seeds leaf values and labels from sync_from") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    DeviceState d{3.3f, 2, true, "dev0"};
    mirror.sync_from(d);

    CHECK(std::get<0>(mirror.slots).value.get() == doctest::Approx(3.3f));
    CHECK(std::get<0>(mirror.slots).name.get().value == "voltage");
    CHECK(std::get<1>(mirror.slots).value.get() == 2);
    CHECK(std::get<1>(mirror.slots).name.get().value == "mode");
    CHECK(std::get<2>(mirror.slots).value.get() == true);
    CHECK(std::get<3>(mirror.slots).value.get() == "dev0");
    CHECK(std::get<3>(mirror.slots).name.get().value == "name");
}

TEST_CASE("FieldMirror::build reconstructs T from current slot values") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    DeviceState d{3.3f, 2, true, "dev0"};
    mirror.sync_from(d);

    std::get<1>(mirror.slots).value.set(99);
    DeviceState rebuilt = mirror.build();

    CHECK(rebuilt.voltage == doctest::Approx(3.3f));
    CHECK(rebuilt.mode == 99);
    CHECK(rebuilt.enabled == true);
    CHECK(rebuilt.name == "dev0");
}

TEST_CASE("FieldMirror::for_each_leaf visits every leaf exactly once") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    mirror.sync_from(DeviceState{1.f, 1, false, "x"});

    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 4);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson setup builddir && meson test -C builddir field_mirror -v`
Expected: FAIL — `include/prism/widgets/field_mirror.hpp` does not exist yet (compile error).

- [ ] **Step 3: Write `include/prism/widgets/field_mirror.hpp`**

```cpp
#pragma once

#include <prism/core/field.hpp>
#include <prism/ui/delegate.hpp>

#include <tuple>
#include <type_traits>

#if __cpp_impl_reflection
#include <meta>
#include <ranges>

namespace prism::inspector {
using namespace prism::core;
using namespace prism::ui;

template <typename T>
concept MirrorLeaf = Numeric<T> || StringLike<T> || ScopedEnum<T>;

// One labeled row: a static name caption + the live editable value.
template <typename M>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};
};

template <typename T>
concept NestedMirrorSlot = requires(T& t) { t.slots; };

template <typename T> struct FieldMirror;

template <typename M>
using MirrorSlot = std::conditional_t<MirrorLeaf<M>, LeafSlot<M>, FieldMirror<M>>;

template <typename T>
consteval std::meta::info field_mirror_tuple_info() {
    std::vector<std::meta::info> slot_types;
    for (auto m : std::meta::nonstatic_data_members_of(
             ^^T, std::meta::access_context::unchecked())) {
        auto mtype = std::meta::type_of(m);
        slot_types.push_back(std::meta::substitute(^^MirrorSlot, {mtype}));
    }
    return std::meta::substitute(^^std::tuple, slot_types);
}

template <typename T>
using FieldMirrorTuple = [: field_mirror_tuple_info<T>() :];

template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;

    void sync_from(const T& v) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.sync_from(v.[:m:]);
            } else {
                slot.name.set(Label<std::string>{std::string(std::meta::identifier_of(m))});
                slot.value.set(v.[:m:]);
            }
        }
    }

    T build() const {
        T out{};
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            const auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                out.[:m:] = slot.build();
            } else {
                out.[:m:] = slot.value.get();
            }
        }
        return out;
    }

    template <typename Fn>
    void for_each_leaf(Fn&& fn) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.for_each_leaf(fn);
            } else {
                fn(slot.value);
            }
        }
    }
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir field_mirror -v`
Expected: PASS (3 test cases)

- [ ] **Step 5: Register the test in meson and commit**

Add to `tests/meson.build`'s `headless_tests` dict (alongside `'shared'`):
```meson
  'field_mirror' : files('test_field_mirror.cpp'),
```

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp tests/meson.build
git commit -m "feat: add FieldMirror<T> reflection-derived mirror for flat structs"
```

---

### Task 2: Nested-struct recursion + `view()` for `FieldMirror<T>`

**Files:**
- Modify: `include/prism/widgets/field_mirror.hpp` (add `view()` method)
- Modify: `tests/test_field_mirror.cpp` (add nested-struct test cases)

**Interfaces:**
- Consumes: `prism::app::WidgetTree`, `prism::app::WidgetTree::ViewBuilder::vstack(auto&...)` (from `prism/app/widget_tree.hpp`).
- Produces: `FieldMirror<T>::view(prism::app::WidgetTree::ViewBuilder& vb)` — makes `FieldMirror<T>` a full PRISM component usable inside any `view()` or as a `WidgetTree` root. Recursion through `MirrorSlot`/`sync_from`/`build`/`for_each_leaf` already exists from Task 1 (the `NestedMirrorSlot` branch) — this task proves it end-to-end via a real `WidgetTree`.

- [ ] **Step 1: Write the failing test for nested recursion + tree integration**

Append to `tests/test_field_mirror.cpp`:

```cpp
#include <prism/app/widget_tree.hpp>

struct Inner {
    float scale;
    int count;
};

struct NestedDeviceState {
    float voltage;
    Inner inner;
};

TEST_CASE("FieldMirror recurses into nested plain structs") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    NestedDeviceState d{9.f, {1.5f, 7}};
    mirror.sync_from(d);

    CHECK(std::get<0>(mirror.slots).value.get() == doctest::Approx(9.f));
    auto& inner_mirror = std::get<1>(mirror.slots);
    CHECK(std::get<0>(inner_mirror.slots).value.get() == doctest::Approx(1.5f));
    CHECK(std::get<1>(inner_mirror.slots).value.get() == 7);

    std::get<1>(inner_mirror.slots).value.set(42);
    NestedDeviceState rebuilt = mirror.build();
    CHECK(rebuilt.voltage == doctest::Approx(9.f));
    CHECK(rebuilt.inner.scale == doctest::Approx(1.5f));
    CHECK(rebuilt.inner.count == 42);
}

TEST_CASE("FieldMirror::for_each_leaf visits nested leaves too") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    mirror.sync_from(NestedDeviceState{1.f, {2.f, 3}});
    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 3); // voltage, inner.scale, inner.count
}

TEST_CASE("FieldMirror is a WidgetTree component with one leaf per member") {
    prism::inspector::FieldMirror<DeviceState> mirror;
    mirror.sync_from(DeviceState{1.f, 1, false, "x"});
    prism::WidgetTree tree(mirror);
    // 4 leaves, each rendered as a LeafSlot (name label + value) = 8 rendered leaf widgets
    CHECK(tree.leaf_count() == 8);
}

TEST_CASE("FieldMirror WidgetTree recurses into nested struct") {
    prism::inspector::FieldMirror<NestedDeviceState> mirror;
    mirror.sync_from(NestedDeviceState{1.f, {2.f, 3}});
    prism::WidgetTree tree(mirror);
    // voltage (2) + inner.scale (2) + inner.count (2) = 6
    CHECK(tree.leaf_count() == 6);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir field_mirror -v`
Expected: FAIL — `prism::WidgetTree tree(mirror)` doesn't compile (`FieldMirror<T>` has no `view()`, and its only member `slots` is a `std::tuple` that reflection can't meaningfully walk as a fallback).

- [ ] **Step 3: Add `view()` to `FieldMirror<T>`**

In `include/prism/widgets/field_mirror.hpp`, add the include and method:

```cpp
#include <prism/app/widget_tree.hpp>
```

(add near the top, alongside the other includes)

```cpp
    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        std::apply([&](auto&... s) { vb.vstack(s...); }, slots);
    }
```

(add as a member of `FieldMirror<T>`, after `for_each_leaf`)

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir field_mirror -v`
Expected: PASS (7 test cases total)

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: recurse FieldMirror into nested structs, add WidgetTree view()"
```

---

### Task 3: `Inspector<T>` — remote-to-local sync over `Shared<T>`

**Files:**
- Create: `include/prism/widgets/inspector.hpp`
- Test: `tests/test_inspector.cpp`
- Modify: `tests/meson.build` (register test)

**Interfaces:**
- Consumes: `prism::core::Shared<T>` (`get()`, `set(T)`, `observe(std::function<void(const T&)>)`, `drain_notifications()`), `prism::inspector::FieldMirror<T>` (Task 1/2).
- Produces: `prism::inspector::Inspector<T>` — constructor `explicit Inspector(Shared<T>& source)`, method `void view(prism::app::WidgetTree::ViewBuilder& vb)`. This task covers remote→local sync only (seeding + `Shared<T>::observe()`); local→remote push and `drain()`/`WidgetTree` wiring is Task 4.

- [ ] **Step 1: Write the failing test for initial seeding and remote sync**

```cpp
// tests/test_inspector.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/inspector.hpp>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism::inspector {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot; using namespace inspector;
}

struct DeviceState {
    float voltage;
    int mode;
    bool enabled;
};

TEST_CASE("Inspector seeds mirror from Shared<T> initial value") {
    prism::Shared<DeviceState> source{DeviceState{3.3f, 2, true}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(3.3f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 2);
    CHECK(std::get<2>(inspector.mirror().slots).value.get() == true);
}

TEST_CASE("Inspector syncs mirror when Shared<T> changes and drains") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    source.set(DeviceState{9.f, 5, true});
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(0.f)); // not yet drained

    source.drain_notifications();
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(9.f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 5);
    CHECK(std::get<2>(inspector.mirror().slots).value.get() == true);
}

TEST_CASE("Inspector is a WidgetTree component") {
    prism::Shared<DeviceState> source{DeviceState{1.f, 1, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);
    CHECK(tree.leaf_count() == 6); // 3 members x (name + value)
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir inspector -v`
Expected: FAIL — `include/prism/widgets/inspector.hpp` does not exist, and `inspector.mirror()` accessor doesn't exist yet.

- [ ] **Step 3: Write `include/prism/widgets/inspector.hpp`**

```cpp
#pragma once

#include <prism/widgets/field_mirror.hpp>
#include <prism/core/shared.hpp>
#include <prism/app/widget_tree.hpp>

#if __cpp_impl_reflection

namespace prism::inspector {
using namespace prism::core;

template <typename T>
struct Inspector {
    explicit Inspector(Shared<T>& source) : source_(source) {
        mirror_.sync_from(source_.get());
        source_.observe([this](const T& v) { mirror_.sync_from(v); });
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        mirror_.view(vb);
    }

    [[nodiscard]] FieldMirror<T>& mirror() { return mirror_; }
    [[nodiscard]] const FieldMirror<T>& mirror() const { return mirror_; }

private:
    Shared<T>& source_;
    FieldMirror<T> mirror_;
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir inspector -v`
Expected: PASS (3 test cases)

- [ ] **Step 5: Register the test in meson and commit**

Add to `tests/meson.build`'s `headless_tests` dict:
```meson
  'inspector' : files('test_inspector.cpp'),
```

```bash
git add include/prism/widgets/inspector.hpp tests/test_inspector.cpp tests/meson.build
git commit -m "feat: add Inspector<T> with remote-to-local Shared<T> sync"
```

---

### Task 4: Local-to-remote push + `WidgetTree` drain wiring

**Files:**
- Modify: `include/prism/widgets/inspector.hpp` (add `drain()`, wire `push_local` via `for_each_leaf`)
- Modify: `include/prism/app/widget_tree.hpp:910-917` (opt-in `drain()` detection in `build_node_tree`'s `view()` branch)
- Modify: `tests/test_inspector.cpp` (add round-trip, drain-wiring, and echo-settles tests)

**Interfaces:**
- Consumes: `FieldMirror<T>::for_each_leaf(Fn&&)` (Task 1), `Field<M>::observe(std::function<void(const M&)>)`.
- Produces: `Inspector<T>::drain()` (delegates to `source_.drain_notifications()`); `WidgetTree::build_node_tree`'s `view()` branch now sets `root.drain_fn` when the component exposes `drain()`, so `tree.drain_shared()` (existing method, unchanged) picks up `Inspector<T>` automatically.

- [ ] **Step 1: Write the failing test for local edit pushing back to `Shared<T>`, tree-driven drain, and the bounded echo**

Append to `tests/test_inspector.cpp`:

```cpp
TEST_CASE("Inspector pushes local edits back to Shared<T>, preserving other fields") {
    prism::Shared<DeviceState> source{DeviceState{3.3f, 2, true}};
    prism::inspector::Inspector<DeviceState> inspector(source);

    std::get<1>(inspector.mirror().slots).value.set(99);

    DeviceState updated = source.get();
    CHECK(updated.voltage == doctest::Approx(3.3f));
    CHECK(updated.mode == 99);
    CHECK(updated.enabled == true);
}

TEST_CASE("WidgetTree drains Inspector's Shared<T> via tree.drain_shared()") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);

    source.set(DeviceState{7.f, 3, true});
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(0.f)); // not yet drained

    tree.drain_shared();
    CHECK(std::get<0>(inspector.mirror().slots).value.get() == doctest::Approx(7.f));
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 3);
}

TEST_CASE("Local edit followed by remote drain settles without oscillating") {
    prism::Shared<DeviceState> source{DeviceState{0.f, 0, false}};
    prism::inspector::Inspector<DeviceState> inspector(source);
    prism::WidgetTree tree(inspector);
    tree.drain_shared(); // clear any initial pending state

    // Local edit: pushes {0, 5, false} to source, which also sets pending_ (echo).
    std::get<1>(inspector.mirror().slots).value.set(5);
    CHECK(source.get().mode == 5);

    // First drain consumes the echo — values are already correct, no observable change.
    tree.drain_shared();
    CHECK(std::get<1>(inspector.mirror().slots).value.get() == 5);

    // A second drain must be a true no-op (bounded, not oscillating forever).
    int calls = 0;
    auto conn = source.on_change().connect([&](const DeviceState&) { ++calls; });
    tree.drain_shared();
    CHECK(calls == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir inspector -v`
Expected: FAIL — local edits don't push back yet (`push_local` not wired), and `tree.drain_shared()` doesn't reach `source_.drain_notifications()` (no `drain_fn` wired for a `view()`-based component).

- [ ] **Step 3: Wire `push_local` and add `drain()` in `Inspector<T>`**

In `include/prism/widgets/inspector.hpp`, update the constructor and add `drain()`:

```cpp
    explicit Inspector(Shared<T>& source) : source_(source) {
        mirror_.sync_from(source_.get());
        mirror_.for_each_leaf([this](auto& field) {
            field.observe([this](const auto&) { push_local(); });
        });
        source_.observe([this](const T& v) { mirror_.sync_from(v); });
    }

    void drain() { source_.drain_notifications(); }
```

(a generic lambda converts fine to whatever `std::function<void(const M&)>` each `Field<M>::observe` needs — no change to `core/field.hpp` required)

Add the private `push_local` method to `Inspector<T>`:

```cpp
private:
    Shared<T>& source_;
    FieldMirror<T> mirror_;

    void push_local() { source_.set(mirror_.build()); }
```

- [ ] **Step 4: Add opt-in `drain()` detection to `build_node_tree`'s `view()` branch**

In `include/prism/app/widget_tree.hpp`, modify the existing branch (currently `widget_tree.hpp:910-917`):

```cpp
        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
            ViewBuilder vb{*this, root};
            model.view(vb);
#if __cpp_impl_reflection
            check_unplaced_fields(model, vb.placed());
#endif
            vb.finalize();
            if constexpr (requires(Model& m) { { m.drain() } -> std::same_as<void>; })
                root.drain_fn = [&model] { model.drain(); };
        }
```

(only the added `if constexpr` block at the end is new — everything else in that branch is unchanged)

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir inspector -v`
Expected: PASS (6 test cases total)

- [ ] **Step 6: Run the full test suite to check for regressions**

Run: `meson test -C builddir`
Expected: All tests pass (existing suite + the 10 new `field_mirror`/`inspector` cases). Read the actual pass/fail count and exit code before proceeding — do not infer success from partial output.

- [ ] **Step 7: Commit**

```bash
git add include/prism/widgets/inspector.hpp include/prism/app/widget_tree.hpp tests/test_inspector.cpp
git commit -m "feat: wire Inspector<T> local edits back to Shared<T>, drive drain from WidgetTree"
```

---

## Post-plan note

This plan delivers `Inspector<T>` for a single fixed `T`, editable, embeddable anywhere a PRISM component works (including standalone via `model_app("...", inspector)`). Deliberately deferred (see design spec's Non-goals): field-level merge/conflict resolution, multi-type selection at runtime, a read-only toggle, side-by-side (hstack) label layout, and non-PRISM host embedding.
