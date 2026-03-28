# Keyboard Focus Management — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add keyboard focus tracking to the widget tree — Tab/Shift+Tab cycling, click-to-focus, Space/Enter activation — following Qt conventions.

**Architecture:** `FocusPolicy` enum on each `Delegate<T>` drives which widgets are focusable. `WidgetTree` maintains a `focus_order_` vector and `focused_id_`. `model_app` routes Tab/Space/Enter `KeyPress` events through the tree. Key constants live in `input_event.hpp` (no SDL dependency in core).

**Tech Stack:** C++26, doctest, stdexec

**Spec:** `docs/superpowers/specs/2026-03-28-keyboard-focus-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `include/prism/core/input_event.hpp` | Modify | Add `keys::` and `mods::` constants |
| `include/prism/core/delegate.hpp` | Modify | Add `FocusPolicy` enum, `focus_policy` per Delegate, keyboard activation, focus rings |
| `include/prism/core/widget_tree.hpp` | Modify | Add `WidgetNode::focus_policy`, focus tracking members/methods |
| `include/prism/core/model_app.hpp` | Modify | Route Tab, Shift+Tab, Space, Enter, click-to-focus |
| `tests/test_delegate.cpp` | Modify | Keyboard activation tests |
| `tests/test_widget_tree.cpp` | Modify | Focus tracking and cycling tests |

---

### Task 1: Add key/mod constants to input_event.hpp

**Files:**
- Modify: `include/prism/core/input_event.hpp`

- [ ] **Step 1: Add keys and mods namespaces**

At the end of the `prism` namespace in `include/prism/core/input_event.hpp`, before the closing `}`, add:

```cpp
namespace keys {
    inline constexpr int32_t tab   = 0x09;   // matches SDLK_TAB
    inline constexpr int32_t space = 0x20;   // matches SDLK_SPACE
    inline constexpr int32_t enter = 0x0D;   // matches SDLK_RETURN
}

namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT
}
```

- [ ] **Step 2: Verify build**

Run:
```bash
ninja -C builddir
```

Expected: Build succeeds. Constants are unused so far, no warnings (constexpr inline suppresses unused-variable).

- [ ] **Step 3: Commit**

```bash
git add include/prism/core/input_event.hpp
git commit -m "feat: add key/mod constants to input_event.hpp (no SDL dependency)"
```

---

### Task 2: Add FocusPolicy and focus_policy to Delegate

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_delegate.cpp`

- [ ] **Step 1: Write the failing test**

Add at the end of `tests/test_delegate.cpp`:

```cpp
TEST_CASE("FocusPolicy: non-interactive delegates are not focusable") {
    CHECK(prism::Delegate<int>::focus_policy == prism::FocusPolicy::none);
    CHECK(prism::Delegate<std::string>::focus_policy == prism::FocusPolicy::none);
    CHECK(prism::Delegate<prism::Label<>>::focus_policy == prism::FocusPolicy::none);
}

TEST_CASE("FocusPolicy: interactive delegates are focusable") {
    CHECK(prism::Delegate<bool>::focus_policy == prism::FocusPolicy::tab_and_click);
    CHECK(prism::Delegate<prism::Slider<>>::focus_policy == prism::FocusPolicy::tab_and_click);
    CHECK(prism::Delegate<prism::Button>::focus_policy == prism::FocusPolicy::tab_and_click);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate --test-case="FocusPolicy*"
```

Expected: FAIL — `FocusPolicy` is not defined yet.

- [ ] **Step 3: Add FocusPolicy enum and focus_policy to each Delegate**

In `include/prism/core/delegate.hpp`, add the enum before the primary `Delegate<T>` template:

```cpp
enum class FocusPolicy : uint8_t { none, tab_and_click };
```

Add `static constexpr FocusPolicy focus_policy` to each Delegate specialization:

| Delegate | Value |
|---|---|
| Primary `Delegate<T>` | `FocusPolicy::none` |
| `Delegate<StringLike T>` | `FocusPolicy::none` |
| `Delegate<Label<T>>` | `FocusPolicy::none` |
| `Delegate<bool>` | `FocusPolicy::tab_and_click` |
| `Delegate<Slider<T>>` | `FocusPolicy::tab_and_click` |
| `Delegate<Button>` | `FocusPolicy::tab_and_click` |

Example for the primary template:
```cpp
template <typename T>
struct Delegate {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    // ... existing record(), handle_input()
};
```

Example for `Delegate<bool>`:
```cpp
template <>
struct Delegate<bool> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    // ... existing record(), handle_input()
};
```

Apply the same pattern to all six Delegate types.

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate
```

Expected: All tests pass, including the two new FocusPolicy tests.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp
git commit -m "feat: add FocusPolicy enum and focus_policy to all Delegate types"
```

---

### Task 3: Add keyboard activation to Delegate<bool> and Delegate<Button>

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_delegate.cpp`

- [ ] **Step 1: Write the failing tests**

Add at the end of `tests/test_delegate.cpp`:

```cpp
TEST_CASE("Delegate<bool> toggles on Space key") {
    prism::Field<bool> field{false};
    prism::WidgetVisualState vs;
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, vs);
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> toggles on Enter key") {
    prism::Field<bool> field{false};
    prism::WidgetVisualState vs;
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, vs);
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> ignores other keys") {
    prism::Field<bool> field{false};
    prism::WidgetVisualState vs;
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{0x41, 0}, vs);  // 'A'
    CHECK(field.get() == false);
}

TEST_CASE("Button activates on Space key") {
    prism::Field<prism::Button> field{{"Go"}};
    prism::WidgetVisualState vs;
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, vs);
    CHECK(field.get().click_count == 1);
}

TEST_CASE("Button activates on Enter key") {
    prism::Field<prism::Button> field{{"Go"}};
    prism::WidgetVisualState vs;
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, vs);
    CHECK(field.get().click_count == 1);
}

TEST_CASE("Button ignores other keys") {
    prism::Field<prism::Button> field{{"Go"}};
    prism::WidgetVisualState vs;
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{0x41, 0}, vs);
    CHECK(field.get().click_count == 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate --test-case="*Space*"
```

Expected: FAIL — `handle_input` currently only checks for `MouseButton`.

- [ ] **Step 3: Update Delegate<bool>::handle_input**

Replace the existing `handle_input` in `Delegate<bool>`:

```cpp
static void handle_input(Field<bool>& field, const InputEvent& ev, WidgetVisualState&) {
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        field.set(!field.get());
    } else if (auto* kp = std::get_if<KeyPress>(&ev);
               kp && (kp->key == keys::space || kp->key == keys::enter)) {
        field.set(!field.get());
    }
}
```

- [ ] **Step 4: Update Delegate<Button>::handle_input**

Replace the existing `handle_input` in `Delegate<Button>`:

```cpp
static void handle_input(Field<Button>& field, const InputEvent& ev, WidgetVisualState&) {
    bool activate = false;
    if (auto* mb = std::get_if<MouseButton>(&ev))
        activate = mb->pressed;
    else if (auto* kp = std::get_if<KeyPress>(&ev))
        activate = (kp->key == keys::space || kp->key == keys::enter);

    if (activate) {
        auto btn = field.get();
        btn.click_count++;
        field.set(btn);
    }
}
```

- [ ] **Step 5: Run all delegate tests**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate
```

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp
git commit -m "feat: add Space/Enter keyboard activation to bool and Button delegates"
```

---

### Task 4: Add focus ring rendering to interactive delegates

**Files:**
- Modify: `include/prism/core/delegate.hpp`
- Modify: `tests/test_delegate.cpp`

- [ ] **Step 1: Write the failing tests**

Add at the end of `tests/test_delegate.cpp`:

```cpp
TEST_CASE("Delegate<bool> renders focus ring when focused") {
    prism::Field<bool> field{false};
    prism::WidgetVisualState vs{.focused = true};
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field, vs);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Delegate<bool> no focus ring when not focused") {
    prism::Field<bool> field{false};
    prism::WidgetVisualState vs{};
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field, vs);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK_FALSE(has_outline);
}

TEST_CASE("Slider renders focus ring when focused") {
    prism::Field<prism::Slider<>> field{{.value = 0.5}};
    prism::WidgetVisualState vs{.focused = true};
    prism::DrawList dl;
    prism::Delegate<prism::Slider<>>::record(dl, field, vs);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Button renders focus ring when focused") {
    prism::Field<prism::Button> field{{"Go"}};
    prism::WidgetVisualState vs{.focused = true};
    prism::DrawList dl;
    prism::Delegate<prism::Button>::record(dl, field, vs);
    int outline_count = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) outline_count++;
    }
    CHECK(outline_count >= 2);  // existing border + focus ring
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate --test-case="*focus ring*"
```

Expected: FAIL — `Delegate<bool>` produces no `RectOutline` when focused.

- [ ] **Step 3: Add focus ring to Delegate<bool>::record**

At the end of `Delegate<bool>::record`, after the `filled_rect` call, add:

```cpp
if (vs.focused)
    dl.rect_outline({-1, -1, 202, 32}, Color::rgba(80, 160, 240), 2.0f);
```

- [ ] **Step 4: Add focus ring to Delegate<Slider<T>>::record**

At the end of `Delegate<Slider<T>>::record`, add:

```cpp
if (vs.focused)
    dl.rect_outline({-1, -1, track_w + 2, widget_h + 2}, Color::rgba(80, 160, 240), 2.0f);
```

- [ ] **Step 5: Add focus ring to Delegate<Button>::record**

At the end of `Delegate<Button>::record`, after the existing text call, add:

```cpp
if (vs.focused)
    dl.rect_outline({-2, -2, 204, 36}, Color::rgba(80, 160, 240), 2.0f);
```

Note: Button already has a border outline at `{0,0,200,32}`. The focus ring is slightly outside at `{-2,-2,204,36}` to distinguish from the normal border.

- [ ] **Step 6: Run all delegate tests**

Run:
```bash
ninja -C builddir && ./builddir/test_delegate
```

Expected: All tests pass. Existing tests that check `dl.size()` or index into `dl.commands` still pass because focus ring is only added when `vs.focused = true`, and existing tests use default `vs` (focused = false).

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/delegate.hpp tests/test_delegate.cpp
git commit -m "feat: add focus ring rendering to bool, Slider, and Button delegates"
```

---

### Task 5: Add focus tracking to WidgetTree

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing tests**

Add a new model at the end of `tests/test_widget_tree.cpp`:

```cpp
struct FocusModel {
    prism::Field<prism::Label<>> title{{"Hello"}};
    prism::Field<bool> toggle{false};
    prism::Field<prism::Slider<>> slider{{.value = 0.5}};
    prism::Field<prism::Button> btn{{"Click"}};
    prism::Field<int> count{0};
};
```

Then add the test cases:

```cpp
TEST_CASE("focus_order contains only focusable widgets in struct order") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    // ids: [0]=Label, [1]=bool, [2]=Slider, [3]=Button, [4]=int
    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 3);
    CHECK(focus[0] == ids[1]);  // bool
    CHECK(focus[1] == ids[2]);  // Slider
    CHECK(focus[2] == ids[3]);  // Button
}

TEST_CASE("focused_id starts at 0") {
    FocusModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.focused_id() == 0);
}

TEST_CASE("set_focused on focusable widget sets focused_id") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);
    CHECK(tree.focused_id() == focus[0]);
}

TEST_CASE("set_focused on non-focusable widget is no-op") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    tree.set_focused(ids[0]);  // Label — not focusable
    CHECK(tree.focused_id() == 0);
}

TEST_CASE("set_focused marks old and new widget dirty") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);
    tree.clear_dirty();

    tree.set_focused(focus[1]);
    CHECK(tree.any_dirty());
}

TEST_CASE("clear_focus resets focused_id and marks dirty") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);
    tree.clear_dirty();

    tree.clear_focus();
    CHECK(tree.focused_id() == 0);
    CHECK(tree.any_dirty());
}

TEST_CASE("focus_next cycles forward through focusable widgets") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.focus_next();
    CHECK(tree.focused_id() == focus[0]);
    tree.focus_next();
    CHECK(tree.focused_id() == focus[1]);
    tree.focus_next();
    CHECK(tree.focused_id() == focus[2]);
    tree.focus_next();  // wraps
    CHECK(tree.focused_id() == focus[0]);
}

TEST_CASE("focus_prev cycles backward through focusable widgets") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.focus_prev();  // no focus → last
    CHECK(tree.focused_id() == focus[2]);
    tree.focus_prev();
    CHECK(tree.focused_id() == focus[1]);
    tree.focus_prev();
    CHECK(tree.focused_id() == focus[0]);
    tree.focus_prev();  // wraps
    CHECK(tree.focused_id() == focus[2]);
}

TEST_CASE("focus_next on model with no focusable widgets is no-op") {
    SimpleModel model;  // Field<int> + Field<string> — both non-focusable
    prism::WidgetTree tree(model);
    tree.focus_next();
    CHECK(tree.focused_id() == 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
ninja -C builddir && ./builddir/test_widget_tree --test-case="focus*"
```

Expected: FAIL — `focus_order()`, `focused_id()`, `set_focused()`, `clear_focus()`, `focus_next()`, `focus_prev()` don't exist yet.

- [ ] **Step 3: Add focus_policy to WidgetNode**

In `include/prism/core/widget_tree.hpp`, add to the `WidgetNode` struct:

```cpp
struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    FocusPolicy focus_policy = FocusPolicy::none;
    WidgetVisualState visual_state;
    // ... rest unchanged
};
```

- [ ] **Step 4: Set focus_policy in build_leaf**

In the `build_leaf` method, after `node.is_container = false;`, add:

```cpp
node.focus_policy = Delegate<T>::focus_policy;
```

- [ ] **Step 5: Add focus members and methods to WidgetTree**

Add private members:

```cpp
WidgetId focused_id_ = 0;
std::vector<WidgetId> focus_order_;
```

Modify `build_index` to build `focus_order_` — after the existing loop body, add collection of focusable nodes:

```cpp
void build_index(WidgetNode& node) {
    index_[node.id] = &node;
    if (node.wire) {
        node.wire(node);
        node.wire = nullptr;
    }
    if (!node.is_container && node.focus_policy != FocusPolicy::none)
        focus_order_.push_back(node.id);
    for (auto& c : node.children)
        build_index(c);
}
```

Add public methods:

```cpp
[[nodiscard]] WidgetId focused_id() const { return focused_id_; }

[[nodiscard]] const std::vector<WidgetId>& focus_order() const { return focus_order_; }

void set_focused(WidgetId id) {
    if (id == focused_id_) return;
    if (std::find(focus_order_.begin(), focus_order_.end(), id) == focus_order_.end()) return;
    if (auto it = index_.find(focused_id_); it != index_.end()) {
        it->second->visual_state.focused = false;
        it->second->dirty = true;
    }
    focused_id_ = id;
    if (auto it = index_.find(focused_id_); it != index_.end()) {
        it->second->visual_state.focused = true;
        it->second->dirty = true;
    }
}

void clear_focus() {
    if (focused_id_ == 0) return;
    if (auto it = index_.find(focused_id_); it != index_.end()) {
        it->second->visual_state.focused = false;
        it->second->dirty = true;
    }
    focused_id_ = 0;
}

void focus_next() {
    if (focus_order_.empty()) return;
    if (focused_id_ == 0) {
        set_focused(focus_order_.front());
        return;
    }
    auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
    if (it == focus_order_.end() || ++it == focus_order_.end())
        set_focused(focus_order_.front());
    else
        set_focused(*it);
}

void focus_prev() {
    if (focus_order_.empty()) return;
    if (focused_id_ == 0) {
        set_focused(focus_order_.back());
        return;
    }
    auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
    if (it == focus_order_.begin())
        set_focused(focus_order_.back());
    else
        set_focused(*std::prev(it));
}
```

Add `#include <algorithm>` at the top if not already present (it is — already included via delegate.hpp).

- [ ] **Step 6: Run all widget_tree tests**

Run:
```bash
ninja -C builddir && ./builddir/test_widget_tree
```

Expected: All tests pass (old and new).

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_widget_tree.cpp
git commit -m "feat: add focus tracking, Tab cycling, and focus_order to WidgetTree"
```

---

### Task 6: Add keyboard dispatch to focused widget via WidgetTree

**Files:**
- Modify: `tests/test_widget_tree.cpp`

- [ ] **Step 1: Write the failing tests**

Add at the end of `tests/test_widget_tree.cpp`:

```cpp
TEST_CASE("Space dispatched to focused bool toggles it") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);  // bool toggle
    tree.dispatch(tree.focused_id(), prism::KeyPress{prism::keys::space, 0});
    CHECK(model.toggle.get() == true);
}

TEST_CASE("Enter dispatched to focused Button increments click_count") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[2]);  // Button
    tree.dispatch(tree.focused_id(), prism::KeyPress{prism::keys::enter, 0});
    CHECK(model.btn.get().click_count == 1);
}
```

- [ ] **Step 2: Run tests to verify they pass**

These tests use the existing `dispatch()` method plus the keyboard activation from Task 3. They should pass immediately — this is a verification that the pieces work together.

Run:
```bash
ninja -C builddir && ./builddir/test_widget_tree --test-case="*dispatched*"
```

Expected: PASS — `dispatch()` sends `KeyPress` to the widget's `on_input` hub, which calls `Delegate<T>::handle_input`, which now handles `KeyPress` (from Task 3).

- [ ] **Step 3: Commit**

```bash
git add tests/test_widget_tree.cpp
git commit -m "test: add integration tests for keyboard activation via WidgetTree dispatch"
```

---

### Task 7: Route keyboard events in model_app

**Files:**
- Modify: `include/prism/core/model_app.hpp`

- [ ] **Step 1: Add Tab/Shift+Tab routing**

In `model_app.hpp`, inside the event dispatch lambda (after the `MouseButton` block), add:

```cpp
if (auto* kp = std::get_if<KeyPress>(&ev)) {
    if (kp->key == keys::tab) {
        if (kp->mods & mods::shift)
            tree.focus_prev();
        else
            tree.focus_next();
    } else if (kp->key == keys::space || kp->key == keys::enter) {
        if (tree.focused_id() != 0)
            tree.dispatch(tree.focused_id(), ev);
    }
}
```

- [ ] **Step 2: Add click-to-focus in the MouseButton block**

Modify the existing `MouseButton` handling block. Replace:

```cpp
if (auto* mb = std::get_if<MouseButton>(&ev); mb && current_snap) {
    if (auto id = hit_test(*current_snap, mb->position)) {
        tree.set_pressed(*id, mb->pressed);
        tree.dispatch(*id, ev);
    }
}
```

With:

```cpp
if (auto* mb = std::get_if<MouseButton>(&ev); mb && current_snap) {
    auto id = hit_test(*current_snap, mb->position);
    if (id) {
        tree.set_pressed(*id, mb->pressed);
        if (mb->pressed)
            tree.set_focused(*id);  // no-op if non-focusable
        tree.dispatch(*id, ev);
    } else if (mb->pressed) {
        tree.clear_focus();
    }
}
```

- [ ] **Step 3: Build and run all tests**

Run:
```bash
ninja -C builddir && meson test -C builddir
```

Expected: All tests pass. `model_app.hpp` is tested indirectly through `test_model_app.cpp` (existing tests should still pass since they don't send keyboard events).

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/model_app.hpp
git commit -m "feat: route Tab, Space, Enter, and click-to-focus in model_app"
```

---

### Task 8: Visual verification

**Files:** None modified — this task verifies the full integration.

- [ ] **Step 1: Run the model_dashboard example**

Run:
```bash
./builddir/examples/model_dashboard
```

Expected:
- Press Tab: blue focus ring appears on "Dark Mode" bool toggle (first focusable widget)
- Press Tab again: focus ring moves to "Volume" slider
- Press Tab again: focus ring moves to "Counter" button
- Press Tab again: wraps back to "Dark Mode"
- Press Shift+Tab: focus moves backward
- Press Space while focused on bool toggle: it toggles
- Press Enter while focused on Button: click_count increments
- Click on a widget: focus ring moves to it
- Click on empty space: focus ring disappears

- [ ] **Step 2: Check that existing interactions still work**

- Click on bool toggle: still toggles
- Click on slider: still sets value
- Click on button: still increments
- Hover effects: still visible (hover highlight changes on mouse move)

If everything works, the keyboard focus implementation is complete.
