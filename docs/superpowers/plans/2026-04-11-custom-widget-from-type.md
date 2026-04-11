# Custom Widget from Type — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename `Delegate<T>` to `Widget<T>`, add `is_widget_v<T>` concept, change `EditState` from closed `std::variant` to `std::any` with `get_or_create<S>()` helper, so users can define custom widget types that propagate everywhere.

**Architecture:** Pure rename + concept addition + type erasure change. The dispatch mechanism (compile-time template specialization captured in lambdas) is unchanged. `Widget<T>` becomes the single public extension point. `EditState` moves from a closed variant to `std::any` so user-defined sentinels can store arbitrary edit state.

**Tech Stack:** C++23, Meson, doctest

**Spec:** `docs/superpowers/specs/2026-04-11-custom-widget-from-type-design.md`

---

## File Map

**Modify:**
- `include/prism/ui/delegate.hpp` — rename all `Delegate<T>` → `Widget<T>`, add `is_widget_v` concept, change `EditState` to `std::any`, add `get_or_create<S>()` to `WidgetNode`
- `include/prism/ui/widget_node.hpp` — `Delegate<T>` → `Widget<T>` in `node_leaf` / `node_readonly_leaf`, add `get_or_create<S>()` method
- `include/prism/app/widget_tree.hpp` — `Delegate<T>` → `Widget<T>` in vlist bind, scroll state access
- `include/prism/delegates/text_delegates.hpp` — rename all `Delegate<>` method bodies, replace `std::holds_alternative`/`std::get` with `get_or_create`
- `include/prism/delegates/dropdown_delegates.hpp` — same
- `include/prism/delegates/tabs_delegates.hpp` — same
- `tests/test_delegate.cpp` — rename all `Delegate<>` references to `Widget<>`
- `tests/test_dropdown.cpp` — same
- `tests/test_text_field.cpp` — same
- `tests/test_password.cpp` — same
- `tests/test_text_area.cpp` — same
- `tests/test_tabs.cpp` — same
- `README.md` — rename references
- `doc/design/delegates-and-sentinels.md` — rename references
- `doc/design/README.md` — rename references
- `doc/design/input-events.md` — rename references
- `doc/design/reactivity.md` — rename references
- `doc/design/proxy-widget.md` — rename references
- `doc/design/dynamic-node-tree.md` — rename references

**Rename:**
- `tests/test_delegate.cpp` → `tests/test_widget.cpp`
- `tests/meson.build` — update test name from `'delegate'` to `'widget'`

---

### Task 1: Change EditState from variant to std::any and add get_or_create

The variant is a closed set — users can't add custom edit states. This must change first since subsequent tasks depend on the new API.

**Files:**
- Modify: `include/prism/ui/delegate.hpp`
- Modify: `include/prism/ui/widget_node.hpp`
- Test: `tests/test_delegate.cpp`

- [ ] **Step 1: Write a failing test for get_or_create**

Add to `tests/test_delegate.cpp`:

```cpp
struct CustomEditState {
    int cursor = 0;
    bool open = false;
};

TEST_CASE("WidgetNode::get_or_create returns default-constructed state") {
    WidgetNode node;
    node.id = 99;
    auto& state = node.get_or_create<CustomEditState>();
    CHECK(state.cursor == 0);
    CHECK(state.open == false);
}

TEST_CASE("WidgetNode::get_or_create returns existing state") {
    WidgetNode node;
    node.id = 99;
    auto& state = node.get_or_create<CustomEditState>();
    state.cursor = 42;
    auto& state2 = node.get_or_create<CustomEditState>();
    CHECK(state2.cursor == 42);
}

TEST_CASE("WidgetNode::get_or_create replaces wrong type") {
    WidgetNode node;
    node.id = 99;
    node.get_or_create<CustomEditState>().cursor = 10;
    auto& text = node.get_or_create<TextEditState>();
    CHECK(text.cursor == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile && ./tests/test_delegate --test-case="*get_or_create*"`
Expected: Compile error — `get_or_create` doesn't exist yet.

- [ ] **Step 3: Change EditState from variant to std::any**

In `include/prism/ui/delegate.hpp`, replace the `EditState` variant:

```cpp
// REMOVE this:
using EditState = std::variant<
    std::monostate,
    TextEditState,
    TextAreaEditState,
    DropdownEditState,
    ScrollState,
    TabBarEditState,
    std::shared_ptr<VirtualListState>,
    std::shared_ptr<TableState>,
    std::shared_ptr<TabsState>,
    std::shared_ptr<void>
>;

// REPLACE with:
using EditState = std::any;
```

Add `#include <any>` if not already present.

- [ ] **Step 4: Add get_or_create to WidgetNode**

In `include/prism/ui/widget_node.hpp`, add to the `WidgetNode` struct:

```cpp
template <typename S>
S& get_or_create() {
    auto* p = std::any_cast<S>(&edit_state);
    if (!p) {
        edit_state = S{};
        p = std::any_cast<S>(&edit_state);
    }
    return *p;
}
```

- [ ] **Step 5: Migrate all ensure_*_state helpers to use get_or_create**

In `include/prism/delegates/text_delegates.hpp`, replace:

```cpp
// BEFORE:
inline TextEditState& ensure_text_edit_state(WidgetNode& node) {
    if (!std::holds_alternative<TextEditState>(node.edit_state))
        node.edit_state = TextEditState{};
    return std::get<TextEditState>(node.edit_state);
}
```

```cpp
// AFTER:
inline TextEditState& ensure_text_edit_state(WidgetNode& node) {
    return node.get_or_create<TextEditState>();
}
```

Same pattern for:
- `ensure_text_area_edit_state` in `text_delegates.hpp`
- `ensure_dropdown_state` in `dropdown_delegates.hpp`
- `ensure_tabs_state` in `tabs_delegates.hpp`
- `ensure_scroll_state` in `widget_tree.hpp`

Also fix any inline `std::holds_alternative`/`std::get` patterns in `widget_tree.hpp` (e.g. line 163):

```cpp
// BEFORE:
if (!std::holds_alternative<ScrollState>(wn.edit_state))
    wn.edit_state = ScrollState{};
auto& ss = std::get<ScrollState>(wn.edit_state);

// AFTER:
auto& ss = wn.get_or_create<ScrollState>();
```

And any `std::get<std::shared_ptr<...>>` patterns — replace with `std::any_cast`.

- [ ] **Step 6: Fix all std::get / std::holds_alternative / std::shared_ptr<void> usages**

Search for remaining `std::get<`, `std::holds_alternative<`, and `std::shared_ptr<void>` patterns related to `edit_state` across the codebase. These all need migration:

- `std::get<std::shared_ptr<VirtualListState>>` → `std::any_cast<std::shared_ptr<VirtualListState>>`
- `std::get<std::shared_ptr<TableState>>` → `std::any_cast<std::shared_ptr<TableState>>`
- `std::get<std::shared_ptr<TabsState>>` → `std::any_cast<std::shared_ptr<TabsState>>`
- `std::get<std::shared_ptr<void>>` → `std::any_cast<std::shared_ptr<void>>`
- All `std::holds_alternative<X>(node.edit_state)` → `node.edit_state.type() == typeid(X)` or just use `get_or_create<X>()` where the intent is ensure+get.

- [ ] **Step 7: Run all tests**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile && meson test`
Expected: All tests pass, including the new `get_or_create` tests.

- [ ] **Step 8: Commit**

```bash
git add include/prism/ui/delegate.hpp include/prism/ui/widget_node.hpp \
    include/prism/delegates/text_delegates.hpp \
    include/prism/delegates/dropdown_delegates.hpp \
    include/prism/delegates/tabs_delegates.hpp \
    include/prism/app/widget_tree.hpp \
    tests/test_delegate.cpp
git commit -m "refactor: EditState from variant to std::any, add get_or_create<S>()"
```

---

### Task 2: Rename Delegate<T> → Widget<T> in delegate.hpp

The core rename. All template specializations and the primary template.

**Files:**
- Modify: `include/prism/ui/delegate.hpp`

- [ ] **Step 1: Rename the primary template and all specializations**

In `include/prism/ui/delegate.hpp`, do a global find-and-replace:
- `struct Delegate` → `struct Widget`
- `Delegate<` → `Widget<`

This covers:
- Primary template `Delegate<T>` (line ~289)
- `Delegate<Numeric T>` (line ~305)
- `Delegate<StringLike T>` (line ~323)
- `Delegate<Checkbox>` (line ~371)
- `Delegate<bool>` (line ~413)
- `Delegate<Label<T>>` (line ~445)
- `Delegate<Slider<T, O>>` (line ~472)
- `Delegate<Button>` (line ~557)
- `Delegate<TextField<T>>` (line ~590)
- `Delegate<Password<T>>` (line ~605)
- `Delegate<TextArea<T>>` (line ~619)
- `Delegate<ScopedEnum T>` (line ~635)
- `Delegate<Dropdown<T>>` (line ~643)
- `Delegate<TabBar<>>` (line ~651)

- [ ] **Step 2: Add is_widget_v concept**

Add after the primary template in `delegate.hpp`:

```cpp
/// True if Widget<T> has been specialized with record() and handle_input().
template <typename T>
concept is_widget_v = requires(DrawList& dl, const Field<T>& cf, Field<T>& f,
                                const InputEvent& ev, WidgetNode& node) {
    Widget<T>::record(dl, cf, node);
    Widget<T>::handle_input(f, ev, node);
};
```

- [ ] **Step 3: Verify compilation**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile`
Expected: Compile errors in files that still reference `Delegate<`. This is expected — we fix them in the next tasks.

- [ ] **Step 4: Commit (won't compile yet — intermediate)**

```bash
git add include/prism/ui/delegate.hpp
git commit -m "refactor: rename Delegate<T> to Widget<T>, add is_widget_v concept"
```

---

### Task 3: Rename Delegate<T> → Widget<T> in delegate implementation files

**Files:**
- Modify: `include/prism/delegates/text_delegates.hpp`
- Modify: `include/prism/delegates/dropdown_delegates.hpp`
- Modify: `include/prism/delegates/tabs_delegates.hpp`

- [ ] **Step 1: Rename in text_delegates.hpp**

Global find-and-replace `Delegate<` → `Widget<` in `include/prism/delegates/text_delegates.hpp`.

- [ ] **Step 2: Rename in dropdown_delegates.hpp**

Global find-and-replace `Delegate<` → `Widget<` in `include/prism/delegates/dropdown_delegates.hpp`.

- [ ] **Step 3: Rename in tabs_delegates.hpp**

Global find-and-replace `Delegate<` → `Widget<` in `include/prism/delegates/tabs_delegates.hpp`.

- [ ] **Step 4: Commit**

```bash
git add include/prism/delegates/
git commit -m "refactor: rename Delegate<T> to Widget<T> in delegate implementations"
```

---

### Task 4: Rename Delegate<T> → Widget<T> in widget infrastructure + is_widget_v dispatch

The primary `Widget<T>` template is empty. `node_leaf<T>()` uses `if constexpr (is_widget_v<T>)` to branch between custom widget rendering and a default fallback (filled rect with hover). This gives a clear compile-time signal for whether a type has a real widget.

**Files:**
- Modify: `include/prism/ui/widget_node.hpp`
- Modify: `include/prism/app/widget_tree.hpp`

- [ ] **Step 1: Rewrite node_leaf with is_widget_v dispatch**

In `include/prism/ui/widget_node.hpp`, replace `node_leaf<T>()`:

```cpp
template <typename T>
Node node_leaf(Field<T>& field, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;

    n.build_widget = [&field](WidgetNode& wn) {
        if constexpr (is_widget_v<T>) {
            wn.focus_policy = Widget<T>::focus_policy;
            if constexpr (requires { Widget<T>::expand; })
                wn.expand = Widget<T>::expand;
            if constexpr (requires { Widget<T>::expand_axis; })
                wn.expand_axis = Widget<T>::expand_axis;
            wn.record = [&field](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Widget<T>::record(node.draws, field, node);
            };
            wn.wire = [&field](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&field, &node](const InputEvent& ev) {
                        Widget<T>::handle_input(field, ev, node);
                    })
                );
            };
        } else {
            // Default fallback: filled rect with hover highlight, no input
            wn.focus_policy = FocusPolicy::none;
            wn.record = [&field](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                const auto& vs = node_vs(node);
                auto bg = vs.hovered ? Color::rgba(70, 70, 80)
                                     : Color::rgba(50, 50, 60);
                auto [w, h] = node.node_allocated();
                node.draws.push_filled_rect({X{0}, Y{0}}, {w, h}, bg);
            };
        }
        wn.record(wn);
    };

    n.on_change = [&field](std::function<void()> cb) -> Connection {
        return field.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}
```

- [ ] **Step 2: Rewrite node_readonly_leaf with is_widget_v dispatch**

Same pattern for `node_readonly_leaf<T>()`:

```cpp
template <typename T, typename Observable>
Node node_readonly_leaf(Observable& obs, WidgetId& next_id) {
    Node n;
    n.id = next_id++;
    n.is_leaf = true;

    n.build_widget = [&obs](WidgetNode& wn) {
        wn.focus_policy = FocusPolicy::none;
        if constexpr (is_widget_v<T>) {
            wn.record = [&obs](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Field<T> tmp{obs.get()};
                Widget<T>::record(node.draws, tmp, node);
            };
        } else {
            wn.record = [&obs](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                const auto& vs = node_vs(node);
                auto bg = vs.hovered ? Color::rgba(70, 70, 80)
                                     : Color::rgba(50, 50, 60);
                auto [w, h] = node.node_allocated();
                node.draws.push_filled_rect({X{0}, Y{0}}, {w, h}, bg);
            };
        }
        wn.record(wn);
    };

    n.on_change = [&obs](std::function<void()> cb) -> Connection {
        return obs.on_change().connect([cb = std::move(cb)](const T&) { cb(); });
    };

    return n;
}
```

- [ ] **Step 3: Rename in widget_tree.hpp**

In `include/prism/app/widget_tree.hpp`, replace all `Delegate<T>` → `Widget<T>` in vlist_bind_row and any other references:
- `Delegate<T>::focus_policy` → `Widget<T>::focus_policy`
- `Delegate<T>::record` → `Widget<T>::record`
- `Delegate<T>::handle_input` → `Widget<T>::handle_input`

- [ ] **Step 4: Build and verify compilation**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile`
Expected: Compiles (source files done). Test files will still have `Delegate<` but the library itself should compile.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/widget_node.hpp include/prism/app/widget_tree.hpp
git commit -m "refactor: rename Delegate<T> to Widget<T> in widget infrastructure, add is_widget_v dispatch"
```

---

### Task 5: Rename in test files and rename test_delegate.cpp

**Files:**
- Modify + Rename: `tests/test_delegate.cpp` → `tests/test_widget.cpp`
- Modify: `tests/test_dropdown.cpp`
- Modify: `tests/test_text_field.cpp`
- Modify: `tests/test_password.cpp`
- Modify: `tests/test_text_area.cpp`
- Modify: `tests/test_tabs.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Rename test_delegate.cpp to test_widget.cpp**

```bash
cd /var/home/jeandet/Documents/prog/PRSIM
git mv tests/test_delegate.cpp tests/test_widget.cpp
```

- [ ] **Step 2: Global replace Delegate< → Widget< in all test files**

In each of these files, find-and-replace `Delegate<` → `Widget<`:
- `tests/test_widget.cpp` (the renamed file)
- `tests/test_dropdown.cpp`
- `tests/test_text_field.cpp`
- `tests/test_password.cpp`
- `tests/test_text_area.cpp`
- `tests/test_tabs.cpp`

Also replace any string literals like `"Delegate"` in test names if they exist.

- [ ] **Step 3: Update tests/meson.build**

Change:
```meson
'delegate' : files('test_delegate.cpp'),
```
To:
```meson
'widget' : files('test_widget.cpp'),
```

- [ ] **Step 4: Build and run all tests**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile && meson test`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add tests/test_widget.cpp tests/test_dropdown.cpp tests/test_text_field.cpp \
    tests/test_password.cpp tests/test_text_area.cpp tests/test_tabs.cpp \
    tests/meson.build
git commit -m "refactor: rename Delegate<T> to Widget<T> in all test files"
```

---

### Task 6: Write a test for user-defined custom widget

Prove the extension story works end-to-end: a user type with `Widget<T>` specialization, custom `EditState`, renders and handles input through the standard pipeline.

**Files:**
- Modify: `tests/test_widget.cpp`

- [ ] **Step 1: Write the test**

Add to `tests/test_widget.cpp`:

```cpp
// --- User-defined sentinel: a Knob with custom edit state ---
struct KnobEditState {
    bool dragging = false;
};

struct Knob {
    double value = 0.5;
    double min = 0.0;
    double max = 1.0;
};

template <>
struct prism::Widget<Knob> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Knob>& field, WidgetNode& node) {
        auto& es = node.get_or_create<KnobEditState>();
        const auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(80, 80, 80) : Color::rgba(60, 60, 60);
        dl.push_filled_rect({X{0}, Y{0}}, {Width{40}, Height{40}}, bg);
        (void)es;
        (void)field;
    }

    static void handle_input(Field<Knob>& field, const InputEvent& ev, WidgetNode& node) {
        auto& es = node.get_or_create<KnobEditState>();
        if (auto* mb = std::get_if<MouseButtonEvent>(&ev)) {
            if (mb->pressed) {
                es.dragging = true;
                auto range = field.get().max - field.get().min;
                field.set({field.get().value + range * 0.1,
                           field.get().min, field.get().max});
            } else {
                es.dragging = false;
            }
        }
    }
};

TEST_CASE("User-defined Widget<Knob> renders through node_leaf pipeline") {
    Field<Knob> knob{{.value = 0.5, .min = 0.0, .max = 1.0}};
    WidgetId next_id = 1;
    auto node = node_leaf(knob, next_id);
    CHECK(node.is_leaf);
    CHECK(node.id == 1);

    // Build into a WidgetNode and verify rendering
    WidgetNode wn;
    wn.id = node.id;
    node.build_widget(wn);
    CHECK(wn.focus_policy == FocusPolicy::tab_and_click);
    CHECK(wn.draws.commands().size() > 0);
}

TEST_CASE("User-defined Widget<Knob> handles input and uses custom edit state") {
    Field<Knob> knob{{.value = 0.5, .min = 0.0, .max = 1.0}};
    WidgetId next_id = 1;
    auto node = node_leaf(knob, next_id);

    WidgetNode wn;
    wn.id = node.id;
    node.build_widget(wn);

    // Wire input
    wn.wire(wn);

    // Simulate mouse press
    MouseButtonEvent press{.x = X{20}, .y = Y{20}, .button = 1, .pressed = true};
    wn.on_input.emit(InputEvent{press});

    // Value should have increased by 10% of range
    CHECK(knob.get().value == doctest::Approx(0.6));

    // Edit state should show dragging
    auto& es = wn.get_or_create<KnobEditState>();
    CHECK(es.dragging == true);
}
```

- [ ] **Step 2: Run the tests**

Run: `cd /var/home/jeandet/Documents/prog/PRSIM/builddir && meson compile && ./tests/test_widget --test-case="*Knob*"`
Expected: Both tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_widget.cpp
git commit -m "test: user-defined Widget<Knob> with custom edit state"
```

---

### Task 7: Update documentation

**Files:**
- Modify: `README.md`
- Modify: `doc/design/delegates-and-sentinels.md`
- Modify: `doc/design/README.md`
- Modify: `doc/design/input-events.md`
- Modify: `doc/design/reactivity.md`
- Modify: `doc/design/proxy-widget.md`
- Modify: `doc/design/dynamic-node-tree.md`

- [ ] **Step 1: Update README.md**

Replace all `Delegate<T>` → `Widget<T>`, `Delegate<` → `Widget<`, `"Delegates"` → `"Widgets"` where it refers to the type dispatch mechanism. Keep the section header `## Sentinel Types & Widgets` (was `## Sentinel Types & Delegates`).

Key lines to update:
- Line 33: `Delegate<T>` → `Widget<T>`
- Line 182: section title
- Line 204: concept resolution description
- Line 304: sequence diagram label
- Line 322: table entry
- Line 360: phase 3 description
- Line 377: doc link text

- [ ] **Step 2: Update doc/design/delegates-and-sentinels.md**

Global replace `Delegate<` → `Widget<` throughout. Consider renaming the file to `widgets-and-sentinels.md` and updating any links.

- [ ] **Step 3: Update remaining design docs**

In each of these files, replace `Delegate<` → `Widget<`:
- `doc/design/README.md`
- `doc/design/input-events.md`
- `doc/design/reactivity.md`
- `doc/design/proxy-widget.md`
- `doc/design/dynamic-node-tree.md`

- [ ] **Step 4: Commit**

```bash
git add README.md doc/design/
git commit -m "docs: rename Delegate<T> to Widget<T> throughout documentation"
```

---

### Task 8: Final verification

- [ ] **Step 1: Full rebuild from clean**

```bash
cd /var/home/jeandet/Documents/prog/PRSIM/builddir
meson compile --clean && meson compile
```

- [ ] **Step 2: Run all tests**

```bash
meson test
```

Expected: All tests pass.

- [ ] **Step 3: Grep for any remaining Delegate< references**

```bash
cd /var/home/jeandet/Documents/prog/PRSIM
grep -rn 'Delegate<' include/ tests/ examples/ README.md doc/ --include='*.hpp' --include='*.cpp' --include='*.md'
```

Expected: Zero matches (excluding subprojects).

- [ ] **Step 4: Run showcase SVG generation (if applicable)**

```bash
cd /var/home/jeandet/Documents/prog/PRSIM/builddir
rm -f examples/showcase/*.svg
meson compile
```

Verify SVGs regenerate correctly.

- [ ] **Step 5: Commit any fixes**

If step 3 found stragglers, fix and commit:

```bash
git add -u
git commit -m "fix: remove remaining Delegate<T> references"
```
