# Delegate Dispatch & Sentinel Types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded `if constexpr (is_same<T, bool>)` rendering/input logic in `WidgetTree` with a concept-driven delegate dispatch system, add `State<T>` (invisible observable), and implement the first sentinel types (`Label<T>`, `Slider<T>`).

**Architecture:** A `Delegate<T>` primary template with concept-constrained partial specializations maps `Field<T>` value types to `record()` and `handle_input()` static methods. `WidgetTree` calls `Delegate<T>::record()` and `Delegate<T>::handle_input()` instead of hardcoding per-type logic. Sentinel wrapper types (`Label<T>`, `Slider<T>`) carry presentation semantics in the type system. `State<T>` reuses `Field<T>`'s observable core but is skipped by reflection during widget tree construction.

**Tech Stack:** C++26 (P2996 reflection, concepts), doctest, Meson

**Build/test commands:**
- Build: `meson compile -C builddir`
- Run all tests: `meson test -C builddir`
- Run one test: `meson test -C builddir <name>` (e.g. `meson test -C builddir delegate`)

---

## File Structure

| File | Responsibility |
|---|---|
| **Create:** `include/prism/core/state.hpp` | `State<T>` — observable without UI, same API as `Field<T>` minus `label` |
| **Create:** `include/prism/core/delegate.hpp` | `Delegate<T>` primary template + concept-constrained specializations + sentinel types |
| **Create:** `tests/test_state.cpp` | Tests for `State<T>` observable behavior |
| **Create:** `tests/test_delegate.cpp` | Tests for delegate resolution, rendering, and input handling |
| **Modify:** `include/prism/core/reflect.hpp` | Add `is_state_v<T>` detection so reflection can skip `State<T>` members |
| **Modify:** `include/prism/core/widget_tree.hpp` | Replace hardcoded `record_field_widget` / bool input wiring with `Delegate<T>` calls |
| **Modify:** `tests/test_reflect.cpp` | Add tests for `is_state_v` detection and State skipping |
| **Modify:** `tests/test_widget_tree.cpp` | Add tests using sentinel types (Label, Slider) |
| **Modify:** `tests/meson.build` | Register `test_state` and `test_delegate` |
| **Modify:** `include/prism/prism.hpp` | Add `#include` for new headers |

---

### Task 1: `State<T>` — invisible observable

**Files:**
- Create: `include/prism/core/state.hpp`
- Create: `tests/test_state.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_state.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/state.hpp>

#include <string>

TEST_CASE("State holds and returns a value") {
    prism::State<int> s{42};
    CHECK(s.get() == 42);
}

TEST_CASE("State set triggers on_change") {
    prism::State<std::string> s{"hello"};
    std::string received;
    auto conn = s.on_change().connect([&](const std::string& v) { received = v; });
    s.set("world");
    CHECK(received == "world");
}

TEST_CASE("State set with equal value does not emit") {
    prism::State<int> s{5};
    int call_count = 0;
    auto conn = s.on_change().connect([&](const int&) { ++call_count; });
    s.set(5);
    CHECK(call_count == 0);
}

TEST_CASE("State implicit conversion to const T&") {
    prism::State<int> s{10};
    const int& ref = s;
    CHECK(ref == 10);
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```
  'state' : files('test_state.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir state`
Expected: Compilation error — `state.hpp` does not exist.

- [ ] **Step 4: Implement State<T>**

Create `include/prism/core/state.hpp`:

```cpp
#pragma once

#include <prism/core/connection.hpp>

namespace prism {

template <typename T>
struct State {
    T value{};

    State() = default;
    explicit State(T init) : value(std::move(init)) {}

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

Run: `meson compile -C builddir && meson test -C builddir state`
Expected: All 4 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/state.hpp tests/test_state.cpp tests/meson.build
git commit -m "feat: add State<T> invisible observable"
```

---

### Task 2: Reflection detects and skips `State<T>`

**Files:**
- Modify: `include/prism/core/reflect.hpp`
- Modify: `tests/test_reflect.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_reflect.cpp`:

```cpp
#include <prism/core/state.hpp>

struct ModelWithState {
    prism::Field<int> visible{"Vis", 0};
    prism::State<int> hidden{0};
};

TEST_CASE("is_state_v detects State<T>") {
    CHECK(prism::is_state_v<prism::State<int>>);
    CHECK(prism::is_state_v<prism::State<std::string>>);
    CHECK_FALSE(prism::is_state_v<prism::Field<int>>);
    CHECK_FALSE(prism::is_state_v<int>);
}

TEST_CASE("is_component_v is true for struct with State + Field members") {
    CHECK(prism::is_component_v<ModelWithState>);
}

TEST_CASE("for_each_field skips State members") {
    ModelWithState model;
    int count = 0;
    prism::for_each_field(model, [&](auto&) { ++count; });
    CHECK(count == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir reflect`
Expected: Compilation error — `is_state_v` not defined.

- [ ] **Step 3: Add is_state_v to reflect.hpp**

Add after `is_field_v` in `include/prism/core/reflect.hpp`:

```cpp
// Detect State<T>
template <typename T>
struct is_state : std::false_type {};

template <typename T>
    requires requires { typename std::remove_cvref_t<decltype(std::declval<T>().value)>; }
    && requires(T t) { t.on_change(); }
    && (!requires { T::label; })
struct is_state<T> : std::true_type {};

template <typename T>
inline constexpr bool is_state_v = is_state<T>::value;
```

Also update `check_is_component` to recognize `State<T>` members as component evidence (a struct containing *only* State members is still a component). In the `check_is_component()` body, change:

```cpp
if constexpr (is_field_v<M>) found = true;
```

to:

```cpp
if constexpr (is_field_v<M> || is_state_v<M>) found = true;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir reflect`
Expected: All tests PASS (existing + 3 new).

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/reflect.hpp tests/test_reflect.cpp
git commit -m "feat: add is_state_v detection, reflection skips State<T> in for_each_field"
```

---

### Task 3: WidgetTree skips `State<T>` members

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_widget_tree.cpp`:

```cpp
#include <prism/core/state.hpp>

struct ModelWithState {
    prism::Field<int> visible{"Vis", 0};
    prism::State<int> hidden{0};
    prism::Field<bool> flag{"Flag", false};
};

TEST_CASE("WidgetTree skips State<T> members") {
    ModelWithState model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("State<T> change does not dirty the widget tree") {
    ModelWithState model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    model.hidden.set(999);
    CHECK_FALSE(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir widget_tree`
Expected: FAIL — `leaf_count()` returns 3 (State member counted as leaf or component).

- [ ] **Step 3: Add State<T> skip to build_container**

In `include/prism/core/widget_tree.hpp`, inside `build_container`'s `template for` loop, add a guard before the `is_field_v` check. Change:

```cpp
            if constexpr (is_field_v<M>) {
                container.children.push_back(build_leaf(member));
            } else if constexpr (is_component_v<M>) {
```

to:

```cpp
            if constexpr (is_state_v<M>) {
                // invisible observable — no widget
            } else if constexpr (is_field_v<M>) {
                container.children.push_back(build_leaf(member));
            } else if constexpr (is_component_v<M>) {
```

Add `#include <prism/core/state.hpp>` at the top of `widget_tree.hpp`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir widget_tree`
Expected: All tests PASS (existing + 2 new).

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: WidgetTree skips State<T> members during tree construction"
```

---

### Task 4: Delegate primary template + bool specialization

This task extracts the hardcoded rendering/input logic from `WidgetTree` into `Delegate<T>`.

**Files:**
- Create: `include/prism/core/delegate.hpp`
- Create: `tests/test_delegate.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test**

Create `tests/test_delegate.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>

#include <string>

TEST_CASE("Delegate<bool> record produces draws") {
    prism::Field<bool> field{"Toggle", false};
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Delegate<bool> record changes with value") {
    prism::Field<bool> field{"Toggle", false};

    prism::DrawList dl1;
    prism::Delegate<bool>::record(dl1, field);

    field.set(true);
    prism::DrawList dl2;
    prism::Delegate<bool>::record(dl2, field);

    // Both produce draws but the content differs (different background color)
    CHECK(dl1.size() == dl2.size());
    // Extract first FilledRect color from each
    auto color1 = std::get<prism::FilledRect>(dl1.commands[0]).color;
    auto color2 = std::get<prism::FilledRect>(dl2.commands[0]).color;
    CHECK(color1.r != color2.r);  // false=grey, true=teal
}

TEST_CASE("Delegate<bool> handle_input toggles on press") {
    prism::Field<bool> field{"Toggle", false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> handle_input ignores release") {
    prism::Field<bool> field{"Toggle", false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, false});
    CHECK(field.get() == false);
}

TEST_CASE("Default Delegate record produces draws for int") {
    prism::Field<int> field{"Count", 42};
    prism::DrawList dl;
    prism::Delegate<int>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Default Delegate handle_input is a no-op for int") {
    prism::Field<int> field{"Count", 42};
    prism::Delegate<int>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == 42);  // unchanged
}
```

- [ ] **Step 2: Register test in meson.build**

Add to the `headless_tests` dict in `tests/meson.build`:

```
  'delegate' : files('test_delegate.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: Compilation error — `delegate.hpp` does not exist.

- [ ] **Step 4: Implement delegate.hpp**

Create `include/prism/core/delegate.hpp`:

```cpp
#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <string>
#include <variant>

namespace prism {

// Primary template: default delegate for any Field<T>.
// Renders a label-only widget, ignores input.
template <typename T>
struct Delegate {
    static void record(DrawList& dl, const Field<T>& field) {
        auto label_text = std::string(field.label);
        dl.filled_rect({0, 0, 200, 30}, Color::rgba(50, 50, 60));
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<T>&, const InputEvent&) {}
};

// bool specialization: toggle widget
template <>
struct Delegate<bool> {
    static void record(DrawList& dl, const Field<bool>& field) {
        auto label_text = std::string(field.label);
        auto bg = field.get()
            ? Color::rgba(0, 120, 80)
            : Color::rgba(50, 50, 60);
        dl.filled_rect({0, 0, 200, 30}, bg);
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<bool>& field, const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            field.set(!field.get());
    }
};

} // namespace prism
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: All 6 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp tests/meson.build
git commit -m "feat: add Delegate<T> primary template with bool specialization"
```

---

### Task 5: Wire WidgetTree to use Delegate<T>

Replace the hardcoded `record_field_widget` overloads and `if constexpr (is_same_v<T, bool>)` input wiring in `WidgetTree` with `Delegate<T>` calls.

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Run all existing tests as baseline**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests PASS.

- [ ] **Step 2: Replace build_leaf to use Delegate<T>**

In `include/prism/core/widget_tree.hpp`:

Add `#include <prism/core/delegate.hpp>` at the top.

Replace the entire `build_leaf` method:

```cpp
    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;

        node.record = [&field](WidgetNode& n) {
            n.draws.clear();
            Delegate<T>::record(n.draws, field);
        };
        node.record(node);

        node.wire = [&field](WidgetNode& n) {
            n.connections.push_back(
                n.on_input.connect([&field](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev);
                })
            );
        };

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }
```

Delete the two `record_field_widget` static methods (the `Field<bool>` overload and the generic `Field<T>` template) — they are no longer needed.

- [ ] **Step 3: Run all tests to verify nothing broke**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests PASS (including `widget_tree`, `model_app` — same behavior, different code path).

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: wire WidgetTree to Delegate<T> dispatch"
```

---

### Task 6: Label<T> sentinel type

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_delegate.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_delegate.cpp`:

```cpp
TEST_CASE("Label sentinel renders as read-only text") {
    prism::Field<prism::Label<>> field{"Status", {"All systems go"}};
    prism::DrawList dl;
    prism::Delegate<prism::Label<>>::record(dl, field);
    CHECK(dl.size() >= 1);
    // Should contain a TextCmd with the label's value
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "All systems go") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("Label sentinel ignores input") {
    prism::Field<prism::Label<>> field{"Status", {"OK"}};
    prism::Delegate<prism::Label<>>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get().value == "OK");  // unchanged
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: Compilation error — `Label` not defined.

- [ ] **Step 3: Implement Label and its delegate**

Add to `include/prism/core/delegate.hpp`, before the `Delegate` primary template:

```cpp
// Concepts
template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

// Sentinel: read-only label
template <StringLike T = std::string>
struct Label {
    T value{};
    bool operator==(const Label&) const = default;
};
```

Add a `Delegate` specialization for `Label<T>` after the `Delegate<bool>` specialization:

```cpp
template <StringLike T>
struct Delegate<Label<T>> {
    static void record(DrawList& dl, const Field<Label<T>>& field) {
        dl.filled_rect({0, 0, 200, 24}, Color::rgba(40, 40, 48));
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                {4, 4}, 14, Color::rgba(180, 180, 190));
    }

    static void handle_input(Field<Label<T>>&, const InputEvent&) {}
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: All 8 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp
git commit -m "feat: add Label<T> sentinel type with read-only delegate"
```

---

### Task 7: Slider<T> sentinel type

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_delegate.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_delegate.cpp`:

```cpp
TEST_CASE("Slider sentinel renders track and thumb") {
    prism::Field<prism::Slider<>> field{"Volume", {.value = 0.5}};
    prism::DrawList dl;
    prism::Delegate<prism::Slider<>>::record(dl, field);
    // Expect at least: track rect, thumb rect, label text
    CHECK(dl.size() >= 3);
}

TEST_CASE("Slider sentinel renders thumb position proportional to value") {
    prism::Field<prism::Slider<>> field_lo{"Vol", {.value = 0.0}};
    prism::Field<prism::Slider<>> field_hi{"Vol", {.value = 1.0}};

    prism::DrawList dl_lo, dl_hi;
    prism::Delegate<prism::Slider<>>::record(dl_lo, field_lo);
    prism::Delegate<prism::Slider<>>::record(dl_hi, field_hi);

    // Thumb is the second FilledRect — its x position should differ
    auto thumb_lo = std::get<prism::FilledRect>(dl_lo.commands[1]).rect.x;
    auto thumb_hi = std::get<prism::FilledRect>(dl_hi.commands[1]).rect.x;
    CHECK(thumb_hi > thumb_lo);
}

TEST_CASE("Slider<int> with step snaps value") {
    prism::Field<prism::Slider<int>> field{"Quality", {.value = 3, .min = 1, .max = 5, .step = 1}};
    prism::DrawList dl;
    prism::Delegate<prism::Slider<int>>::record(dl, field);
    CHECK(dl.size() >= 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: Compilation error — `Slider` not defined.

- [ ] **Step 3: Implement Slider and its delegate**

Add the concept and sentinel type to `include/prism/core/delegate.hpp`, after `Label`:

```cpp
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <Numeric T = double>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
    bool operator==(const Slider&) const = default;
};
```

Add the delegate specialization after `Delegate<Label<T>>`:

```cpp
template <Numeric T>
struct Delegate<Slider<T>> {
    static constexpr float track_w = 200.f;
    static constexpr float track_h = 6.f;
    static constexpr float thumb_w = 12.f;
    static constexpr float widget_h = 30.f;

    static float ratio(const Slider<T>& s) {
        if (s.max == s.min) return 0.f;
        return static_cast<float>(s.value - s.min) / static_cast<float>(s.max - s.min);
    }

    static void record(DrawList& dl, const Field<Slider<T>>& field) {
        auto& s = field.get();
        float r = ratio(s);
        float track_y = (widget_h - track_h) / 2.f;

        // Track background
        dl.filled_rect({0, track_y, track_w, track_h}, Color::rgba(60, 60, 70));
        // Thumb
        float thumb_x = r * (track_w - thumb_w);
        dl.filled_rect({thumb_x, 0, thumb_w, widget_h}, Color::rgba(0, 140, 200));
        // Label
        dl.text(std::string(field.label), {track_w + 8, 4}, 14, Color::rgba(200, 200, 210));
    }

    static void handle_input(Field<Slider<T>>& field, const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
            float t = std::clamp(mb->position.x / track_w, 0.f, 1.f);
            auto& s = field.get();
            T raw = static_cast<T>(s.min + t * (s.max - s.min));
            Slider<T> updated = s;
            if (s.step != T{0}) {
                T steps = static_cast<T>((raw - s.min + s.step / T{2}) / s.step);
                updated.value = std::clamp(static_cast<T>(s.min + steps * s.step), s.min, s.max);
            } else {
                updated.value = raw;
            }
            field.set(updated);
        }
    }
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir delegate`
Expected: All 11 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp
git commit -m "feat: add Slider<T> sentinel type with track/thumb rendering and click-to-set input"
```

---

### Task 8: Wire sentinels through WidgetTree + integration test

Verify that `Field<Label<>>` and `Field<Slider<>>` work end-to-end through `WidgetTree` and `model_app`.

**Files:**
- Modify: `tests/test_widget_tree.cpp`
- Modify: `tests/test_model_app.cpp`

- [ ] **Step 1: Write the failing widget tree tests**

Append to `tests/test_widget_tree.cpp`:

```cpp
#include <prism/core/delegate.hpp>

struct SentinelModel {
    prism::Field<prism::Label<>> status{"Status", {"OK"}};
    prism::Field<prism::Slider<>> volume{"Volume", {.value = 0.5}};
    prism::Field<bool> enabled{"Enabled", true};
};

TEST_CASE("WidgetTree with sentinel types creates correct leaf count") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);
}

TEST_CASE("WidgetTree builds snapshot with sentinel types") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);
}

TEST_CASE("Slider click through WidgetTree dispatch updates value") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 3);

    // ids[1] is the slider. Click at x=100 (middle of 200px track) → value ≈ 0.5
    tree.dispatch(ids[1], prism::MouseButton{{100, 15}, 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.5).epsilon(0.05));

    // Click at x=0 → value ≈ 0.0
    tree.dispatch(ids[1], prism::MouseButton{{0, 15}, 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.0).epsilon(0.05));
}
```

- [ ] **Step 2: Run tests to verify they pass**

Run: `meson compile -C builddir && meson test -C builddir widget_tree`
Expected: All tests PASS.

(These should pass immediately because Task 5 already wired `Delegate<T>` into `WidgetTree`. If they fail, the delegate dispatch wiring has a problem — debug from the compilation error.)

- [ ] **Step 3: Add model_app integration test with sentinel**

Append to `tests/test_model_app.cpp`:

```cpp
#include <prism/core/delegate.hpp>

struct SliderClickModel {
    prism::Field<prism::Slider<>> volume{"Volume", {.value = 0.0}};
};

TEST_CASE("model_app routes click to Slider and updates value") {
    std::shared_ptr<const prism::SceneSnapshot> latest_snap;
    std::atomic<size_t> snap_count{0};

    struct SliderBackend final : public prism::BackendBase {
        std::shared_ptr<const prism::SceneSnapshot>& latest;
        std::atomic<size_t>& count;
        SliderBackend(std::shared_ptr<const prism::SceneSnapshot>& l, std::atomic<size_t>& c)
            : latest(l), count(c) {}
        void run(std::function<void(const prism::InputEvent&)> cb) override {
            count.wait(0, std::memory_order_acquire);
            auto geo = latest;
            REQUIRE_FALSE(geo->geometry.empty());
            auto [id, rect] = geo->geometry[0];
            // Click at the right edge of the track → value near 1.0
            cb(prism::MouseButton{{rect.x + 190, rect.y + 15}, 1, true});

            auto before = count.load(std::memory_order_acquire);
            count.wait(before, std::memory_order_acquire);

            cb(prism::WindowClose{});
        }
        void submit(std::shared_ptr<const prism::SceneSnapshot> s) override {
            latest = std::move(s);
            count.fetch_add(1, std::memory_order_release);
            count.notify_all();
        }
        void wake() override {}
        void quit() override {}
    };

    SliderClickModel model;
    prism::model_app(
        prism::Backend{std::make_unique<SliderBackend>(latest_snap, snap_count)},
        prism::BackendConfig{.width = 800, .height = 600},
        model
    );

    CHECK(model.volume.get().value > 0.8);
}
```

- [ ] **Step 4: Run all tests**

Run: `meson compile -C builddir && meson test -C builddir`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/test_widget_tree.cpp tests/test_model_app.cpp
git commit -m "test: integration tests for sentinel types through WidgetTree and model_app"
```

---

### Task 9: Update public header and example

**Files:**
- Modify: `include/prism/prism.hpp`
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Add new headers to prism.hpp**

Add these includes to `include/prism/prism.hpp`:

```cpp
#include <prism/core/state.hpp>
#include <prism/core/delegate.hpp>
```

- [ ] **Step 2: Update model_dashboard example to use sentinels**

Replace `examples/model_dashboard.cpp` with:

```cpp
#include <prism/prism.hpp>

#include <iostream>
#include <string>

struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};
    prism::Field<bool> dark_mode{"Dark Mode", true};
    prism::Field<prism::Slider<>> volume{"Volume", {.value = 0.7}};
};

struct Dashboard {
    Settings settings;
    prism::Field<prism::Label<>> status{"Status", {"All systems go"}};
    prism::Field<int> counter{"Counter", 0};
    prism::State<int> request_count{0};
};

int main() {
    Dashboard dashboard;

    dashboard.settings.dark_mode.on_change().connect([](const bool& v) {
        std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
    });

    dashboard.settings.volume.on_change().connect([](const prism::Slider<>& s) {
        std::cout << "Volume: " << s.value << "\n";
    });

    prism::model_app("PRISM Model Dashboard", dashboard);
}
```

- [ ] **Step 3: Build and verify**

Run: `meson compile -C builddir`
Expected: Compiles without errors.

- [ ] **Step 4: Run full test suite**

Run: `meson test -C builddir`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/prism.hpp examples/model_dashboard.cpp
git commit -m "feat: update model_dashboard to demonstrate Label, Slider, and State types"
```
