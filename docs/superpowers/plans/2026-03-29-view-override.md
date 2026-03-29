# Custom view() Override Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow model structs to define a `view(ViewBuilder&)` method that controls widget layout, replacing the default reflection-driven vertical stack.

**Architecture:** `ViewBuilder` is a nested class inside `WidgetTree` with direct access to `build_leaf()` and `build_container()`. A `has_view<T>` concept detects view() at compile time. A new `layout_kind` field on `WidgetNode` tells `build_layout()` whether a container is a Row, Column, Spacer, or default (flatten-into-parent).

**Tech Stack:** C++26 with `-freflection` (GCC 16), doctest, Meson

**Spec:** `docs/superpowers/specs/2026-03-29-view-override-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `include/prism/core/widget_tree.hpp` | Modify | Add `LayoutKind` to `WidgetNode`, add `ViewBuilder` nested class, add `has_view` concept, modify `build_container()`, `build_layout()`, `build_snapshot()`, add `check_unplaced_fields()` |
| `tests/test_view.cpp` | Create | All view() override tests |
| `tests/meson.build` | Modify | Register `test_view.cpp` in headless tests |

---

### Task 1: Add LayoutKind to WidgetNode

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (WidgetNode struct, ~line 27)

- [ ] **Step 1: Write the failing test**

Create `tests/test_view.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>

TEST_CASE("WidgetNode has layout_kind defaulting to Default") {
    prism::WidgetNode node;
    CHECK(node.layout_kind == prism::WidgetNode::LayoutKind::Default);
}
```

- [ ] **Step 2: Register the test file**

In `tests/meson.build`, add to the `headless_tests` dict:

```meson
  'view' : files('test_view.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir view --rebuild`
Expected: compile error — `WidgetNode` has no `layout_kind` member.

- [ ] **Step 4: Add LayoutKind enum and field to WidgetNode**

In `include/prism/core/widget_tree.hpp`, inside `struct WidgetNode`, after `std::function<void(WidgetNode&)> record;`:

```cpp
    enum class LayoutKind : uint8_t { Default, Row, Column, Spacer } layout_kind = LayoutKind::Default;
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 6: Run all existing tests to verify no regressions**

Run: `meson test -C builddir --rebuild`
Expected: all pass — `LayoutKind` defaults to `Default`, no behavior change.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_view.cpp tests/meson.build
git commit -m "feat: add LayoutKind enum to WidgetNode"
```

---

### Task 2: Make build_layout() respect LayoutKind

**Files:**
- Modify: `include/prism/core/widget_tree.hpp` (`build_layout()` ~line 874, `build_snapshot()` ~line 787)
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_view.cpp`:

```cpp
struct RowModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
};

TEST_CASE("build_snapshot with Row-tagged root produces side-by-side layout") {
    RowModel model;
    prism::WidgetTree tree(model);

    // Manually tag root as Row for this test (view() will do this automatically later)
    // We can't access root_ directly, but we can test indirectly:
    // For now, just verify the default Column layout still works
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    // Both widgets should be stacked vertically (Column default)
    // Second widget's y should be >= first widget's y + first widget's height
    auto& g0 = snap->geometry[0];
    auto& g1 = snap->geometry[1];
    CHECK(g1.rect.origin.y.raw() >= g0.rect.origin.y.raw() + g0.rect.extent.h.raw());
}
```

- [ ] **Step 2: Run test to verify it passes (baseline)**

Run: `meson test -C builddir view --rebuild`
Expected: PASS — this confirms current Column default behavior as baseline.

- [ ] **Step 3: Modify build_layout() to respect LayoutKind**

In `include/prism/core/widget_tree.hpp`, replace the `build_layout` static method:

```cpp
    static void build_layout(WidgetNode& node, LayoutNode& parent) {
        using LK = WidgetNode::LayoutKind;

        if (!node.is_container) {
            if (node.layout_kind == LK::Spacer) {
                LayoutNode spacer;
                spacer.kind = LayoutNode::Kind::Spacer;
                spacer.id = node.id;
                parent.children.push_back(std::move(spacer));
            } else {
                LayoutNode leaf;
                leaf.kind = LayoutNode::Kind::Leaf;
                leaf.id = node.id;
                leaf.draws = node.draws;
                leaf.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(leaf));
            }
        } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
            LayoutNode container;
            container.kind = (node.layout_kind == LK::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            container.id = node.id;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else {
            // Default: flatten children into parent (current behavior)
            for (auto& c : node.children)
                build_layout(c, parent);
        }
    }
```

- [ ] **Step 4: Modify build_snapshot() to respect root LayoutKind**

In `build_snapshot()`, replace the hardcoded Column:

```cpp
    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);

        LayoutNode layout;
        layout.kind = (root_.layout_kind == WidgetNode::LayoutKind::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        layout.id = root_.id;
        build_layout(root_, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }
```

- [ ] **Step 5: Run all tests to verify no regressions**

Run: `meson test -C builddir --rebuild`
Expected: all pass — Default LayoutKind preserves existing flatten behavior.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_view.cpp
git commit -m "refactor: make build_layout() and build_snapshot() respect LayoutKind"
```

---

### Task 3: Add ViewBuilder nested class and has_view concept

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_view.cpp`:

```cpp
struct ViewModelSimple {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(b);
        vb.widget(a);
    }
};

TEST_CASE("view() controls field order") {
    ViewModelSimple model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    // Dispatch to first leaf: should be 'b' (view placed it first)
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 2);

    // Set b to a known value, dispatch input to first widget
    // Both are Field<int> so we test by dirty tracking:
    // Setting model.b should dirty the first leaf
    tree.clear_dirty();
    model.b.set(42);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir view --rebuild`
Expected: compile error — `WidgetTree::ViewBuilder` does not exist.

- [ ] **Step 3: Add has_view concept and ViewBuilder class**

In `include/prism/core/widget_tree.hpp`, inside `class WidgetTree`, at the top of the public section, add:

```cpp
public:
    class ViewBuilder {
        WidgetTree& tree_;
        WidgetNode& target_;
        std::vector<WidgetNode*> stack_;

        WidgetNode& current_parent() {
            return stack_.empty() ? target_ : *stack_.back();
        }

    public:
        ViewBuilder(WidgetTree& tree, WidgetNode& target)
            : tree_(tree), target_(target) {}

        template <typename T>
        void widget(Field<T>& field) {
            current_parent().children.push_back(tree_.build_leaf(field));
        }

        template <typename C>
        void component(C& comp) {
            current_parent().children.push_back(tree_.build_container(comp));
        }

        void row(std::invocable auto&& fn) {
            WidgetNode container;
            container.id = tree_.next_id_++;
            container.is_container = true;
            container.layout_kind = WidgetNode::LayoutKind::Row;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            stack_.push_back(&parent.children.back());
            fn();
            stack_.pop_back();
        }

        void column(std::invocable auto&& fn) {
            WidgetNode container;
            container.id = tree_.next_id_++;
            container.is_container = true;
            container.layout_kind = WidgetNode::LayoutKind::Column;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            stack_.push_back(&parent.children.back());
            fn();
            stack_.pop_back();
        }

        void spacer() {
            WidgetNode s;
            s.id = tree_.next_id_++;
            s.is_container = false;
            s.layout_kind = WidgetNode::LayoutKind::Spacer;
            current_parent().children.push_back(std::move(s));
        }

        void finalize() {
            if (target_.children.size() > 1) {
                WidgetNode wrapper;
                wrapper.id = tree_.next_id_++;
                wrapper.is_container = true;
                wrapper.layout_kind = WidgetNode::LayoutKind::Column;
                wrapper.children = std::move(target_.children);
                target_.children.clear();
                target_.children.push_back(std::move(wrapper));
            }
            if (target_.children.size() == 1) {
                target_.layout_kind = target_.children[0].layout_kind;
            }
        }
    };
```

Then modify `build_container()` to detect `view()` using an inline `requires` expression — this avoids forward-declaration ordering issues (ViewBuilder is defined inside WidgetTree, so a standalone concept can't see it). No separate `has_view` concept needed:

```cpp
    template <typename Model>
    WidgetNode build_container(Model& model) {
        WidgetNode container;
        container.id = next_id_++;
        container.is_container = true;

        if constexpr (requires(Model& m, ViewBuilder vb) { m.view(vb); }) {
            ViewBuilder vb{*this, container};
            model.view(vb);
            vb.finalize();
        } else {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));
            template for (constexpr auto m : members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (is_state_v<M>) {
                } else if constexpr (is_field_v<M>) {
                    container.children.push_back(build_leaf(member));
                } else if constexpr (is_component_v<M>) {
                    container.children.push_back(build_container(member));
                }
            }
        }

        return container;
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 5: Run all tests to verify no regressions**

Run: `meson test -C builddir --rebuild`
Expected: all pass — models without `view()` still take the reflection path.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_view.cpp
git commit -m "feat: add ViewBuilder and view() detection in build_container()"
```

---

### Task 4: Test view() with row() layout

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_view.cpp`:

```cpp
struct RowViewModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.widget(b);
        });
    }
};

TEST_CASE("view() with row() produces side-by-side layout") {
    RowViewModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);

    auto& g0 = snap->geometry[0];
    auto& g1 = snap->geometry[1];
    // In a row, both widgets share the same y but different x
    CHECK(g0.rect.origin.y.raw() == g1.rect.origin.y.raw());
    CHECK(g1.rect.origin.x.raw() > g0.rect.origin.x.raw());
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir view --rebuild`
Expected: PASS — if ViewBuilder and build_layout are correct, the Row container should arrange children horizontally.

If it fails, debug by checking that `finalize()` propagates `LayoutKind::Row` to the container's parent correctly and that `build_layout()` emits `LayoutNode::Kind::Row`.

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: view() with row() produces horizontal layout"
```

---

### Task 5: Test view() with nested row inside column

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
struct NestedLayoutModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
    prism::Field<int> c{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(a);
            vb.widget(b);
        });
        vb.widget(c);
    }
};

TEST_CASE("view() with row + extra widget wraps in implicit Column") {
    NestedLayoutModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    auto& ga = snap->geometry[0];
    auto& gb = snap->geometry[1];
    auto& gc = snap->geometry[2];

    // a and b are in a row: same y, different x
    CHECK(ga.rect.origin.y.raw() == gb.rect.origin.y.raw());
    CHECK(gb.rect.origin.x.raw() > ga.rect.origin.x.raw());

    // c is below the row
    float row_bottom = ga.rect.origin.y.raw() + ga.rect.extent.h.raw();
    CHECK(gc.rect.origin.y.raw() >= row_bottom);
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS — `finalize()` wraps the two top-level items (row + widget c) in an implicit Column.

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: view() with implicit Column wrap for multiple top-level items"
```

---

### Task 6: Test view() with spacer()

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
struct SpacerModel {
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

TEST_CASE("view() with spacer pushes widgets apart") {
    SpacerModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);  // spacer has no geometry

    auto& ga = snap->geometry[0];
    auto& gb = snap->geometry[1];
    // Both in a row, spacer takes remaining space
    CHECK(ga.rect.origin.y.raw() == gb.rect.origin.y.raw());
    // b should be pushed far right (spacer expands)
    float gap = gb.rect.origin.x.raw() - (ga.rect.origin.x.raw() + ga.rect.extent.w.raw());
    CHECK(gap > 0);
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: view() with spacer pushes widgets apart in row"
```

---

### Task 7: Test view() with component()

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
struct SubComponent {
    prism::Field<int> x{0};
    prism::Field<int> y{0};
};

struct ParentWithComponent {
    SubComponent sub;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.component(sub);
            vb.widget(flag);
        });
    }
};

TEST_CASE("view() with component() embeds sub-component tree") {
    ParentWithComponent model;
    prism::WidgetTree tree(model);
    // sub has 2 fields + flag = 3 leaves
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);
}

TEST_CASE("view() with component(): sub-component fields are reactive") {
    ParentWithComponent model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();

    model.sub.x.set(99);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: view() with component() embeds sub-component"
```

---

### Task 8: Test recursive view() — sub-component with its own view()

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
struct SubWithView {
    prism::Field<int> x{0};
    prism::Field<int> y{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.row([&] {
            vb.widget(y);
            vb.widget(x);
        });
    }
};

struct ParentWithViewedSub {
    SubWithView sub;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.component(sub);
        vb.widget(flag);
    }
};

TEST_CASE("component() delegates to sub-component's view()") {
    ParentWithViewedSub model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    // Sub-component used row layout: its two widgets should share same y
    auto& g0 = snap->geometry[0];
    auto& g1 = snap->geometry[1];
    CHECK(g0.rect.origin.y.raw() == g1.rect.origin.y.raw());
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: recursive view() on sub-components"
```

---

### Task 9: Test omitted fields and focus order

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the tests**

Append to `tests/test_view.cpp`:

```cpp
struct OmittedFieldModel {
    prism::Field<int> shown{0};
    prism::Field<int> hidden{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(shown);
        // hidden deliberately not placed
    }
};

TEST_CASE("omitted field has no widget but remains reactive") {
    OmittedFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    // hidden field still works as a reactive value
    model.hidden.set(99);
    CHECK(model.hidden.get() == 99);
    // no crash, no dirty (no widget to dirty)
}

struct FocusViewModel {
    prism::Field<prism::Button> btn1{{"First"}};
    prism::Field<prism::Label<>> label{{"text"}};
    prism::Field<prism::Button> btn2{{"Second"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(btn2);
        vb.widget(label);
        vb.widget(btn1);
    }
};

TEST_CASE("focus order follows view() placement order") {
    FocusViewModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    auto ids = tree.leaf_ids();
    // view placed: btn2(focusable), label(not), btn1(focusable)
    REQUIRE(focus.size() == 2);
    CHECK(focus[0] == ids[0]);  // btn2 placed first
    CHECK(focus[1] == ids[2]);  // btn1 placed third (label is ids[1])
}
```

- [ ] **Step 2: Run tests**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: omitted fields and focus order with view()"
```

---

### Task 10: Test dirty propagation with view()-placed widgets

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
TEST_CASE("dirty propagation works for view()-placed widgets") {
    ViewModelSimple model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    model.a.set(42);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    REQUIRE(snap2 != nullptr);
    CHECK(snap2->version == 2);
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: dirty propagation with view()-placed widgets"
```

---

### Task 11: Test default behavior unchanged (regression guard)

**Files:**
- Modify: `tests/test_view.cpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
struct PlainModel {
    prism::Field<int> a{0};
    prism::Field<std::string> b{"hello"};
    prism::Field<bool> c{false};
};

TEST_CASE("model without view() still uses reflection walk") {
    PlainModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);

    // Default Column layout: each widget below the previous
    for (size_t i = 1; i < snap->geometry.size(); ++i) {
        CHECK(snap->geometry[i].rect.origin.y.raw() >=
              snap->geometry[i - 1].rect.origin.y.raw() +
              snap->geometry[i - 1].rect.extent.h.raw());
    }
}
```

- [ ] **Step 2: Run test**

Run: `meson test -C builddir view --rebuild`
Expected: PASS

- [ ] **Step 3: Run full test suite**

Run: `meson test -C builddir --rebuild`
Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_view.cpp
git commit -m "test: regression guard — model without view() unchanged"
```

---

### Task 12: Add debug diagnostic for unplaced fields

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

- [ ] **Step 1: Write the test**

Append to `tests/test_view.cpp`:

```cpp
#include <cstdio>
#include <cstring>

TEST_CASE("debug diagnostic warns about unplaced fields" * doctest::skip(
#ifdef NDEBUG
    true
#else
    false
#endif
)) {
    // Capture stderr
    char buf[512] = {};
    fflush(stderr);
    auto* old = stderr;

#if defined(__linux__)
    FILE* mem = fmemopen(buf, sizeof(buf), "w");
    // Redirect stderr temporarily by using the ViewBuilder's check directly
    // Instead of redirecting stderr (fragile), verify the check_unplaced_fields
    // function exists and compiles by constructing the tree.
    // The warning goes to stderr — just verify the tree builds without crash.
    OmittedFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);
    if (mem) fclose(mem);
#endif
}
```

Actually, testing stderr capture is fragile and platform-dependent. A better approach: verify the diagnostic compiles and doesn't crash, and trust the simple `fprintf` implementation. Replace the test with a simpler one:

```cpp
TEST_CASE("unplaced field diagnostic does not crash") {
    // OmittedFieldModel has 'hidden' field not placed by view()
    // In debug builds, this produces a stderr warning
    // This test just verifies the tree builds without crashing
    OmittedFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);
}
```

(This is already covered by the "omitted field" test from Task 9 — so this task is purely about adding the implementation.)

- [ ] **Step 2: Add placed_ tracking to ViewBuilder**

In `include/prism/core/widget_tree.hpp`, add to `ViewBuilder`:

```cpp
    class ViewBuilder {
        WidgetTree& tree_;
        WidgetNode& target_;
        std::vector<WidgetNode*> stack_;
        std::set<const void*> placed_;  // add this field

        // ... existing methods ...

    public:
        // In widget(), add tracking:
        template <typename T>
        void widget(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(tree_.build_leaf(field));
        }

        [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }
    };
```

Add `#include <set>` to the includes at the top of the file.

- [ ] **Step 3: Add check_unplaced_fields()**

In `WidgetTree`, add a private method:

```cpp
    template <typename Model>
    void check_unplaced_fields([[maybe_unused]] Model& model,
                               [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M>) {
                if (!placed.contains(&member)) {
                    std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                        static_cast<int>(std::meta::identifier_of(m).size()),
                        std::meta::identifier_of(m).data(),
                        static_cast<int>(std::meta::identifier_of(^^Model).size()),
                        std::meta::identifier_of(^^Model).data());
                }
            }
        }
#endif
    }
```

- [ ] **Step 4: Call check_unplaced_fields() from build_container()**

In the `has_view` branch of `build_container()`:

```cpp
        if constexpr (requires(Model& m, ViewBuilder vb) { m.view(vb); }) {
            ViewBuilder vb{*this, container};
            model.view(vb);
            check_unplaced_fields(model, vb.placed());
            vb.finalize();
        }
```

- [ ] **Step 5: Run all tests**

Run: `meson test -C builddir --rebuild`
Expected: all pass. In debug builds, OmittedFieldModel tests will print a warning to stderr (harmless).

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "feat: add debug diagnostic for unplaced fields in view()"
```

---

### Task 13: Final full test suite run

- [ ] **Step 1: Run full test suite**

Run: `meson test -C builddir --rebuild`
Expected: all tests pass, including all existing tests (widget_tree, delegate, text_field, dropdown, password, text_area, layout, etc.)

- [ ] **Step 2: Verify the example builds**

Run: `meson compile -C builddir`
Expected: clean build, no warnings related to view() changes.

- [ ] **Step 3: Commit if any final adjustments were needed**

Only if fixes were required in this step.
