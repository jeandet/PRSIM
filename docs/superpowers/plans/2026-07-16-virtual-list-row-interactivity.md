# VirtualList Row Interactivity Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `VirtualList` rows genuinely interactive — mutations from clicking/typing on a row write back to the source `List<T>` (currently a silent no-op), and add an optional per-row click callback for selection.

**Architecture:** Both fixes live entirely inside `ViewBuilder::list<T>`/`vlist_bind_row` in `include/prism/app/widget_tree.hpp`. Write-back subscribes to the row's already-detached `Field<T>`'s `on_change()` and pushes the new value into the source `List<T>` via `.set(index, ...)`. The click callback is a new optional parameter, wired as one more `on_input` connection that inspects `MouseButton` directly.

**Tech Stack:** C++26, GCC 16 `-freflection`, Meson, doctest.

## Global Constraints

- Verified row-recycling safety fact (do not re-derive, just rely on it): `vlist_unbind_row` (`widget_tree.hpp:222-230`) always calls `wn.connections.clear()` — which genuinely disconnects via `Connection::~Connection()` — before a pooled node is later re-bound to a different index via `vlist_bind_row`. A write-back closure capturing `index` by value is therefore safe: no stale closure from a previous binding can ever fire after rebinding.
- `Field<T>` API (`include/prism/core/field.hpp:18,21,31`): `const T& get() const`, `void set(T new_value)` (equality-guarded — only fires `on_change()` when the value actually differs), `SenderHub<const T&>& on_change()`.
- Default parameter value (`on_row_click = nullptr`) must keep every existing `.list()` call site (`examples/model_dashboard.cpp:222`, `tests/test_virtual_list.cpp`) compiling and behaving identically — no existing test may need modification.
- Build/verify with `meson test -C builddir` after every task; read the actual pass/fail count.

---

## Task 1: Fix row write-back (the core bug)

**Files:**
- Modify: `include/prism/app/widget_tree.hpp:191-220` (`ViewBuilder::list`/`vlist_bind_row`)
- Test: `tests/test_virtual_list.cpp`

**Interfaces:**
- Produces: rows rendered via `.list()` now persist `Widget<T>::handle_input`'s mutations back to the
  source `List<T>`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_virtual_list.cpp — add after the existing List reactivity tests

#include <prism/ui/delegate.hpp> // for Checkbox

struct CheckboxListModel {
    prism::List<prism::Checkbox> items;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.list(items);
    }
};

TEST_CASE("clicking a VirtualList row writes the mutation back to the source List") {
    CheckboxListModel model;
    model.items.push_back({.checked = false, .label = "Enable feature"});

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto row_id = snap->geometry.front().first;

    tree.dispatch(row_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    CHECK(model.items[0].checked == true);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
ninja -C builddir
meson test -C builddir virtual_list
```
Expected: FAIL — `model.items[0].checked` is still `false` (today's dead end: `handle_input` mutates
only the detached `Field<Checkbox>` copy, never written back).

- [ ] **Step 3: Fix `vlist_bind_row` to write back on `on_change`**

In `include/prism/app/widget_tree.hpp`, inside `ViewBuilder::list<T>`'s `vlist_bind_row` lambda,
change the `wire` assignment from:

```cpp
        wn.wire = [field_ptr](WidgetNode& node) {
            node.connections.push_back(
                node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                    Widget<T>::handle_input(*field_ptr, ev, node);
                })
            );
        };
```

to:

```cpp
        wn.wire = [field_ptr, &items, index](WidgetNode& node) {
            node.connections.push_back(
                node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                    Widget<T>::handle_input(*field_ptr, ev, node);
                })
            );
            node.connections.push_back(
                field_ptr->on_change().connect([field_ptr, &items, index](const T&) {
                    if (index < items.size()) items.set(index, field_ptr->get());
                })
            );
        };
```

(`items` and `index` are already in scope — `items` is the outer `vlist_bind_row`'s captured
`List<T>&`, `index` is `vlist_bind_row`'s own parameter — no signature change needed for this step.)

- [ ] **Step 4: Build and run — verify the new test passes, and no regression**

```bash
ninja -C builddir
meson test -C builddir virtual_list
meson test -C builddir 2>&1 | tail -5
```
Expected: the new test passes; full suite pass count unchanged otherwise (every existing `.list()`
usage is display-only, so the new `on_change` connection simply never fires for them).

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "fix: VirtualList row mutations now write back to the source List<T>

vlist_bind_row cloned each row into a detached Field<T> copy and never
wrote handle_input's mutations back — any interactive .list() row
(checkbox, delete button, inline edit) silently did nothing. Fixed by
subscribing to the detached Field's on_change() and pushing the new
value back via List<T>::set(), reusing the only existing precedent for
outward mutation in the codebase (model_dashboard.cpp's on_change
subscription pattern)."
```

---

## Task 2: Add `on_row_click` callback for selection

**Files:**
- Modify: `include/prism/app/widget_tree.hpp:191-220`
- Test: `tests/test_virtual_list.cpp`

**Interfaces:**
- Consumes: Task 1's `wire` closure (extended further here).
- Produces: `ViewBuilder::list<T>(List<T>& items, std::function<void(size_t, const T&)> on_row_click
  = nullptr)`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_virtual_list.cpp — add after the write-back test

struct ClickRow { int id; };

struct ClickRowListModel {
    prism::List<ClickRow> items;
    std::function<void(size_t, const ClickRow&)> on_row_click;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.list(items, on_row_click);
    }
};

namespace prism::ui {
template <> struct Widget<ClickRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static void record(DrawList&, const Field<ClickRow>&, WidgetNode&) {}
    static void handle_input(Field<ClickRow>&, const InputEvent&, WidgetNode&) {}
};
}

TEST_CASE("clicking a VirtualList row invokes on_row_click with the index and value") {
    ClickRowListModel model;
    model.items.push_back({.id = 100});
    model.items.push_back({.id = 200});

    std::vector<std::pair<size_t, int>> clicks;
    model.on_row_click = [&](size_t index, const ClickRow& row) {
        clicks.emplace_back(index, row.id);
    };

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(snap->geometry.size() >= 2);
    // geometry is z-ordered; the two row ids are the first two leaf entries
    auto row0_id = snap->geometry[0].first;
    auto row1_id = snap->geometry[1].first;

    tree.dispatch(row0_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});
    tree.dispatch(row1_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    REQUIRE(clicks.size() == 2);
    CHECK(clicks[0] == std::make_pair(size_t{0}, 100));
    CHECK(clicks[1] == std::make_pair(size_t{1}, 200));
}

TEST_CASE("existing .list() calls without on_row_click still compile and work") {
    prism::List<std::string> items;
    items.push_back("unchanged");
    struct M { prism::List<std::string>* items_ptr; void view(prism::WidgetTree::ViewBuilder& vb) { vb.list(*items_ptr); } };
    M model{&items};
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure — `ViewBuilder::list` doesn't accept a second argument yet.

- [ ] **Step 3: Add the parameter and wire the click connection**

In `include/prism/app/widget_tree.hpp`, change `ViewBuilder::list`'s signature and body:

```cpp
template <typename T>
void list(List<T>& items, std::function<void(size_t, const T&)> on_row_click = nullptr) {
    // ... existing container/vlist_unbind_row setup is unchanged ...
    container.vlist_bind_row = [&items, on_row_click](WidgetNode& wn, size_t index) {
        auto field_ptr = std::make_shared<Field<T>>(items[index]);
        wn.edit_state = std::shared_ptr<void>(field_ptr);
        wn.focus_policy = Widget<T>::focus_policy;
        wn.dirty = true;
        wn.is_container = false;
        wn.draws.clear();
        wn.overlay_draws.clear();
        wn.record = [field_ptr](WidgetNode& node) {
            node.draws.clear();
            node.overlay_draws.clear();
            Widget<T>::record(node.draws, *field_ptr, node);
        };
        wn.record(wn);
        wn.wire = [field_ptr, &items, index, on_row_click](WidgetNode& node) {
            node.connections.push_back(
                node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                    Widget<T>::handle_input(*field_ptr, ev, node);
                })
            );
            node.connections.push_back(
                field_ptr->on_change().connect([field_ptr, &items, index](const T&) {
                    if (index < items.size()) items.set(index, field_ptr->get());
                })
            );
            if (on_row_click) {
                node.connections.push_back(
                    node.on_input.connect([on_row_click, &items, index](const InputEvent& ev) {
                        auto* mb = std::get_if<MouseButton>(&ev);
                        if (mb && mb->pressed && mb->button == 1 && index < items.size())
                            on_row_click(index, items[index]);
                    })
                );
            }
        };
    };
    // ... rest of the existing method body (vlist_unbind_row, container setup, etc.)
    //     is unchanged — only the signature and vlist_bind_row body above change.
}
```

- [ ] **Step 4: Build and run — new tests pass, no regression**

```bash
ninja -C builddir
meson test -C builddir virtual_list
meson test -C builddir 2>&1 | tail -5
```
Expected: both new test cases pass; full suite pass count unchanged otherwise.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: add optional on_row_click(index, value) callback to ViewBuilder::list

Default nullptr preserves every existing .list() call site unchanged.
Fires independently of Widget<T>::handle_input, on the same
MouseButton press — needed for row selection (e.g. the tree inspector)
without requiring every row type to know about selection."
```

---

## Task 3: Out-of-range guard test + final verification

**Files:**
- Test: `tests/test_virtual_list.cpp`

**Interfaces:**
- Consumes: Tasks 1-2's guarded `index < items.size()` checks.

- [ ] **Step 1: Write the test**

```cpp
// tests/test_virtual_list.cpp — add after the click-callback tests

TEST_CASE("a stale row closure does not crash if the list shrinks before the next event") {
    CheckboxListModel model;
    model.items.push_back({.checked = false, .label = "row 0"});
    model.items.push_back({.checked = false, .label = "row 1"});

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(snap->geometry.size() >= 2);
    auto row1_id = snap->geometry[1].first;

    // Shrink the list out from under the already-bound row 1 closure.
    model.items.erase(1);

    // Must not crash — the guard inside the on_change/on_row_click connections checks
    // index < items.size() before reading/writing.
    tree.dispatch(row1_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    CHECK(model.items.size() == 1);
}
```

This test is expected to **pass already** given Tasks 1-2's guards (`if (index < items.size())`) —
it's a regression guard proving those guards are actually present and correct, not new behavior to
implement.

- [ ] **Step 2: Build and run**

```bash
ninja -C builddir
meson test -C builddir virtual_list
```
Expected: passes without crashing (run under a sanitizer build if available, to also confirm no
heap-buffer-overflow/use-after-free — this is exactly the kind of bug ASan/UBSan would catch if the
guard were missing or wrong).

- [ ] **Step 3: Run the full test suite one final time**

```bash
meson test -C builddir 2>&1 | tail -5
```
Read the actual pass/fail count and exit code — this is the final verification for the whole plan.

- [ ] **Step 4: Commit**

```bash
git add tests/test_virtual_list.cpp
git commit -m "test: cover VirtualList row interactivity against a shrinking list"
```
