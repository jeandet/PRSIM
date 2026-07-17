# Tree Inspector Detail Panel & Auto-scroll Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a live, right-side detail panel (full `NodeRow` field dump for the clicked row) and
auto-scroll (the debug window's row list scrolls to reveal whichever row corresponds to the
currently-hovered main-window node) to the existing tree inspector debug window.

**Architecture:** A new generic `WidgetTree::scroll_row_into_view` primitive (Task 1), then
`TreeInspectorController` gains a reference to the debug window's own `WidgetTree` to drive
auto-scroll through it (Task 2), then the detail panel rides the same already-wired
`refresh()`/`on_row_clicked()` entry points (Task 3).

**Tech Stack:** C++26, GCC 16 `-freflection`, Meson, doctest.

**Spec:** `docs/superpowers/specs/2026-07-17-tree-inspector-detail-panel-autoscroll-design.md`

## Global Constraints

- Build/verify with `meson test -C builddir` (or `ninja -C builddir && meson test -C builddir`)
  after every task; read the actual pass/fail count printed by doctest, don't infer from a partial
  grep.
- `include/prism/widgets/debug/tree_inspector.hpp` is only ever included behind
  `#ifdef PRISM_DEBUG_TOOLS_ENABLED` (see `model_app.hpp:11-13`) — nothing inside the file itself
  needs its own additional `#ifdef PRISM_DEBUG_TOOLS_ENABLED` guard (the one exception, the existing
  `row.name` fallback in `flatten_node`, already there and unrelated to this plan, stays as is).
- `WidgetTree::scroll_row_into_view` (Task 1) is a generic, always-compiled `WidgetTree` method —
  same unconditional-compile treatment as the existing `scroll_at`/`scroll_to` it sits beside, not
  gated by `PRISM_DEBUG_TOOLS_ENABLED`.
- Row height: the tree inspector already declares `static constexpr Height row_h{22.f}` on
  `Widget<NodeRow>` (`tree_inspector.hpp:94`) — reuse `Widget<NodeRow>::row_h` everywhere the debug
  row list's row height is needed; do not re-declare or hardcode `22.f` a second time.
- `TreeInspectorController` has all 4 special member functions deleted (copy/move ctor+assign) —
  preserve this; nothing in this plan needs it copyable or movable.
- When a plan step's expected test behavior depends on this codebase's layout/traversal internals
  (row ordering in `SceneSnapshot::geometry`, `ViewBuilder::finalize()`'s hoist, `VirtualList`
  materialization timing), the step says so explicitly and gives a fallback: run it, and if the
  first result doesn't match, adjust the *test's* fixture/constants (not the production code) after
  confirming the mismatch is in test setup, not a real bug — this codebase has twice previously
  shipped a wrong assumption about `WidgetTree` traversal baked into a test's setup rather than the
  code under test (see `tests/test_flatten_tree.cpp`'s `expanded`-set history for the precedent).

---

## Task 1: `WidgetTree::scroll_row_into_view`

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (add the new method after `scroll_to`, which ends at
  line 554, right before `leaf_ids()` at line 556)
- Test: `tests/test_virtual_list.cpp` (append — this file already has the `StringListModel` fixture
  and the sibling `scroll_at` tests this new method's tests are modeled on, lines 155-191)

**Interfaces:**
- Produces: `WidgetTree::scroll_row_into_view(WidgetId container_id, size_t row_index, Height
  row_h)` — public method. No-op (does not mark the tree dirty) if `container_id` isn't found, has
  no scroll state, or the row at `row_index` is already fully within the current viewport. Otherwise
  clamps and applies the minimal scroll needed to bring `[row_index * row_h, row_index * row_h +
  row_h)` into `[current_offset, current_offset + viewport_h)`, via the existing `scroll_to`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_virtual_list.cpp — append after "scroll_at clamps VirtualList to bounds" (line 191)

TEST_CASE("scroll_row_into_view scrolls down to reveal an off-screen-below row, converges to a no-op") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    // Measure the real row height rather than assuming one — see this file's own
    // "VirtualList items at correct Y position after scroll" test for the same convention.
    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);

    // StringListModel::view() is a single vb.list(items) call — LayoutKind::VirtualList, not
    // Row/Column, so ViewBuilder::finalize()'s single-child hoist never fires here; root()'s
    // one child is the VirtualList container itself (same pattern already used in
    // tests/test_debug_name.cpp and tests/test_table.cpp).
    auto container_id = tree.root().children.front().id;

    tree.scroll_row_into_view(container_id, 30, prism::Height{item_h});
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    tree.scroll_row_into_view(container_id, 30, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty()); // already revealed — second identical call is a no-op
}

TEST_CASE("scroll_row_into_view scrolls up to reveal an off-screen-above row") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);
    auto container_id = tree.root().children.front().id;

    tree.scroll_row_into_view(container_id, 40, prism::Height{item_h}); // scroll deep down first
    tree.clear_dirty();

    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h}); // now ask for row 0
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("scroll_row_into_view is a no-op when the row already fits in the viewport") {
    StringListModel model;
    model.items.push_back("only one");

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);

    auto container_id = tree.root().children.front().id;
    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("scroll_row_into_view on an unknown container id is a safe no-op") {
    StringListModel model;
    model.items.push_back("only one");
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    tree.scroll_row_into_view(999999, 0, prism::Height{30.f});
    CHECK_FALSE(tree.any_dirty());
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`scroll_row_into_view` doesn't exist yet).

- [ ] **Step 3: Implement `scroll_row_into_view`**

```cpp
// include/prism/app/widget_tree.hpp — insert right after scroll_to (ends line 554), before leaf_ids():

    void scroll_row_into_view(WidgetId container_id, size_t row_index, Height row_h) {
        auto it = index_.find(container_id);
        if (it == index_.end()) return;
        auto sv = get_scroll_view(*it->second);
        if (!sv) return;

        DY row_top{static_cast<float>(row_index) * row_h.raw()};
        DY row_bottom = row_top + DY{row_h.raw()};
        DY vp_top = sv->offset;
        DY vp_bottom = vp_top + DY{sv->viewport_h.raw()};
        DY max_off{std::max(0.f, sv->content_h.raw() - sv->viewport_h.raw())};

        if (row_bottom > vp_bottom)
            scroll_to(container_id, DY{std::clamp(row_bottom.raw() - sv->viewport_h.raw(), 0.f, max_off.raw())});
        else if (row_top < vp_top)
            scroll_to(container_id, DY{std::clamp(row_top.raw(), 0.f, max_off.raw())});
    }
```
This mirrors the inline row-scroll-into-view logic `materialize_table` already uses for `Table`'s
selected-row scrolling (`widget_tree.hpp:1504-1515`), generalized via the existing private
`get_scroll_view()` helper, which already normalizes `Scroll`/`VirtualList`/`Table` into one
`ScrollView{offset, viewport_h, content_h, ...}` shape. Reuses the existing public `scroll_to`
(just above, lines 545-554) to apply the clamped offset and mark the container dirty — no
duplicated clamp/dirty logic.

- [ ] **Step 4: Build and run — new tests pass, no regressions**

```bash
ninja -C builddir
meson test -C builddir virtual_list
meson test -C builddir 2>&1 | tail -5
```
If any of the four new tests behaves unexpectedly (e.g. the "off-screen-below" case never marks
the tree dirty), first suspect the fixture — check the measured `item_h` and viewport (`400, 100`)
actually put row 30/40 outside the initial visible range before assuming `scroll_row_into_view`
itself is wrong.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_virtual_list.cpp
git commit -m "feat: add WidgetTree::scroll_row_into_view

Generalizes the inline row-scroll-into-view logic Table's selected-row
handling already used, via the existing get_scroll_view() abstraction
that unifies Scroll/VirtualList/Table scroll state — no new
per-layout-kind branching. Needed by the tree inspector's auto-scroll
(next task), but generic enough to sit alongside scroll_at/scroll_to
for any future caller."
```

---

## Task 2: Auto-scroll wiring in `TreeInspectorController`

**Files:**
- Modify: `include/prism/widgets/debug/tree_inspector.hpp`
- Modify: `include/prism/app/model_app.hpp`
- Modify: `tests/test_tree_inspector_controller.cpp`
- Test (regression only, no edits): `tests/test_model_app.cpp` — the existing end-to-end
  integration test ("hotkey attach then hover-select then row-click-highlight then hotkey detach")
  is not modified by this task; running it unmodified in Step 6 is the regression proof that this
  task's `TreeInspectorController`/`model_app.hpp` changes don't break the real hotkey path. Real,
  meaningful coverage of the new auto-scroll behavior itself lives in Step 1's dedicated
  controller-level tests, which control row count and viewport precisely — something that
  end-to-end test's single-leaf fixture and uncontrolled default window size cannot do without
  much more fixture complexity for no added assurance.

**Interfaces:**
- Consumes: `WidgetTree::scroll_row_into_view` (Task 1), `Widget<NodeRow>::row_h` (existing,
  `tree_inspector.hpp:94`).
- Produces: `TreeInspectorController`'s constructor becomes 3-argument: `TreeInspectorController(
  WidgetTree& main_tree, WidgetTree& debug_tree, TreeInspectorModel& debug_model)`. Every existing
  and new call site must use the 3-argument form from this task onward.

- [ ] **Step 1: Update the three existing controller tests to the (about to be) new 3-argument constructor, and write the new failing auto-scroll test**

```cpp
// tests/test_tree_inspector_controller.cpp — replace all three existing
// `prism::debug::TreeInspectorController controller(main_tree, debug_model);` call sites'
// surrounding setup with (add a debug_tree local before each, then pass it):

TEST_CASE("TreeInspectorController refresh populates rows from the main tree") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    controller.refresh();
    CHECK(debug_model.rows.size() >= 2); // root + the int field's leaf
}

TEST_CASE("clicking a debug row sets the main tree's highlight") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    REQUIRE(!debug_model.rows.empty());
    controller.on_row_clicked(0, debug_model.rows[debug_model.rows.size() - 1]); // a leaf, not the root

    auto snap2 = main_tree.build_snapshot(400, 300, 2);
    bool found = false;
    for (auto& cmd : snap2->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found = true;
    CHECK(found);
}

TEST_CASE("hovering the main tree updates the debug model's selection on refresh") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto leaf_id = snap->geometry.back().first;
    main_tree.update_hover(leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    CHECK(debug_model.selected.get() == std::optional<prism::WidgetId>{leaf_id});
}

// New: auto-scroll. Needs the MAIN tree to have many direct sibling leaves — flatten_tree only
// descends into a container's children when that container's own id is in TreeInspectorController's
// expanded_ set, and the constructor only auto-expands the *root* — so the leaves must be direct
// children of the model's root wrapper node (not nested inside their own container) to all be
// visible without any prior expand-click.
namespace {
struct ManyLeavesModel {
    prism::Field<int> f0{0}, f1{0}, f2{0}, f3{0}, f4{0}, f5{0}, f6{0}, f7{0}, f8{0}, f9{0},
                       f10{0}, f11{0}, f12{0}, f13{0}, f14{0}, f15{0}, f16{0}, f17{0}, f18{0}, f19{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(f0, f1, f2, f3, f4, f5, f6, f7, f8, f9,
                   f10, f11, f12, f13, f14, f15, f16, f17, f18, f19);
    }
};
}

TEST_CASE("hovering an off-screen row in the main tree scrolls the debug tree to reveal it") {
    ManyLeavesModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    // Two refresh+snapshot cycles to let the debug window's own VirtualList state stabilize
    // (its viewport_h/content_h are only (re)computed during the debug tree's own
    // build_snapshot/materialize step — see tests/test_virtual_list.cpp's "VirtualList stabilizes
    // after two frames" for the same two-cycle convention) before asserting scroll behavior.
    controller.refresh();
    debug_tree.build_snapshot(200, 80, 1); // deliberately short — only a few debug rows fit
    debug_tree.clear_dirty();
    controller.refresh();
    debug_tree.build_snapshot(200, 80, 2);
    debug_tree.clear_dirty();

    // Hover the LAST leaf (f19) — with 21 rows total (root + 20 leaves) in a viewport that only
    // fits a handful at row_h 22px (80/22 ≈ 3-4 rows), this leaf starts off-screen.
    auto main_snap = main_tree.build_snapshot(400, 300, 3);
    auto last_leaf_id = main_snap->geometry.back().first;
    main_tree.update_hover(last_leaf_id);

    controller.refresh();
    CHECK(debug_tree.any_dirty());
}
```
If the final `CHECK` doesn't pass on first run, verify empirically (not by re-reading source) that
`f19`'s row really is row index 20 and really is off-screen at viewport height 80 — dump
`debug_model.rows.size()` and compare against `80 / Widget<NodeRow>::row_h.raw()` before assuming
`scroll_row_into_view`'s wiring (added in Step 3 below) is broken; adjust the viewport height or
leaf count in this test's fixture if the margin turns out too tight, per this plan's Global
Constraints note on verifying layout-dependent test setups empirically.

Also add this guard-path test — the hovered node's row can legitimately be absent from the current
flatten (its ancestor isn't in `TreeInspectorController`'s `expanded_` set), and `refresh()` must
skip the scroll call rather than crash. A nested component starts collapsed by default (the
constructor only auto-expands the root), so this needs no explicit collapse action to set up:

```cpp
// tests/test_tree_inspector_controller.cpp — append
namespace {
struct NestedLeaf { prism::Field<int> inner{0}; void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(inner); } };
struct NestedMainModel {
    NestedLeaf nested;
    prism::Field<int> value{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.component(nested);
        vb.widget(value);
    }
};
}

TEST_CASE("hovering a node hidden by a collapsed ancestor does not crash and does not scroll") {
    NestedMainModel main_model;
    prism::WidgetTree main_tree(main_model);
    auto main_snap = main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    // The last geometry leaf is NestedLeaf::inner — nested one level below "nested", which
    // TreeInspectorController's expanded_ does not contain by default.
    auto inner_leaf_id = main_snap->geometry.back().first;
    main_tree.update_hover(inner_leaf_id);

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);

    controller.refresh(); // must not crash
    CHECK_FALSE(debug_tree.any_dirty()); // hovered row isn't in the flatten — nothing to scroll to
}
```

- [ ] **Step 2: Run to verify the new test fails (and the three updated tests fail to compile) — expected, since the constructor is still 2-argument**

```bash
ninja -C builddir
```
Expected: compile failure (`TreeInspectorController(main_tree, debug_tree, debug_model)` — no
matching 3-argument constructor yet).

- [ ] **Step 3: Change `TreeInspectorController`'s constructor to 3 arguments, capture the debug list's container id, wire auto-scroll into `refresh()`**

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — TreeInspectorController, replace the
// constructor and refresh():

class TreeInspectorController {
public:
    TreeInspectorController(WidgetTree& main_tree, WidgetTree& debug_tree, TreeInspectorModel& debug_model)
        : main_tree_(&main_tree), debug_tree_(&debug_tree), debug_model_(&debug_model) {
        expanded_.insert(main_tree_->root().id);
        // TreeInspectorModel::view() today is a single vb.list(rows, ...) call — LayoutKind::
        // VirtualList, not Row/Column, so ViewBuilder::finalize()'s single-child hoist never
        // fires; debug_tree_->root()'s one child is the VirtualList container itself (same
        // pattern used in tests/test_debug_name.cpp and tests/test_table.cpp).
        list_container_id_ = debug_tree_->root().children.front().id;
        debug_model_->on_click = [this](size_t index, const NodeRow& row) {
            on_row_clicked(index, row);
        };
    }

    TreeInspectorController(const TreeInspectorController&) = delete;
    TreeInspectorController& operator=(const TreeInspectorController&) = delete;
    TreeInspectorController(TreeInspectorController&&) = delete;
    TreeInspectorController& operator=(TreeInspectorController&&) = delete;

    void refresh() {
        auto rows = flatten_tree(*main_tree_, expanded_);
        while (!debug_model_->rows.empty())
            debug_model_->rows.erase(debug_model_->rows.size() - 1);
        for (auto& row : rows)
            debug_model_->rows.push_back(row);

        auto hovered = main_tree_->hovered_id();
        if (hovered != 0) {
            debug_model_->selected.set(hovered);
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].id == hovered) {
                    debug_tree_->scroll_row_into_view(list_container_id_, i, Widget<NodeRow>::row_h);
                    break;
                }
            }
        }
    }

    void on_row_clicked(size_t, const NodeRow& row) {
        main_tree_->set_debug_highlight(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
    }

private:
    WidgetTree* main_tree_;
    WidgetTree* debug_tree_;
    TreeInspectorModel* debug_model_;
    WidgetId list_container_id_;
    std::set<WidgetId> expanded_;
};
```
Note: `on_row_clicked`'s body is unchanged in this task — the detail-panel additions to it come in
Task 3.

- [ ] **Step 4: Update the production call site in `model_app.hpp`**

```cpp
// include/prism/app/model_app.hpp — inside ctx.set_global_key_handler's attach branch, change:
            debug_window_id = registry.add(*win, debug_model);
            debug_controller.emplace(*primary_entry->tree, debug_model);
// to:
            debug_window_id = registry.add(*win, debug_model);
            auto* debug_entry = registry.find(*debug_window_id);
            if (!debug_entry) return;
            debug_controller.emplace(*primary_entry->tree, *debug_entry->tree, debug_model);
```

- [ ] **Step 5: Build and run — new/updated tests pass, no regressions**

```bash
ninja -C builddir
meson test -C builddir tree_inspector_controller
meson test -C builddir model_app
meson test -C builddir 2>&1 | tail -5
```

- [ ] **Step 6: Build and run the full suite (regression proof, no test edits)**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: no regressions; the existing, unmodified end-to-end integration test in
`tests/test_model_app.cpp` ("hotkey attach then hover-select then row-click-highlight then hotkey
detach") still passes as is — that test's own existing assertions (`rows1.size() >= 2`,
`main_tree_ptr->hovered_id() == leaf_id`, etc.) already depend on `TreeInspectorController`
attaching correctly through the real `model_app()` hotkey path, so it failing would directly expose
a broken Step 4 wiring change without this task needing to add a new assertion of its own.

- [ ] **Step 7: Commit**

```bash
git add include/prism/widgets/debug/tree_inspector.hpp include/prism/app/model_app.hpp \
        tests/test_tree_inspector_controller.cpp
git commit -m "feat: auto-scroll the debug window to reveal the hovered node's row

TreeInspectorController now holds a reference to the debug window's
own WidgetTree (constructor signature change) and calls the new
scroll_row_into_view on every refresh() when the hovered node has a
visible row. Closes the 'Still not implemented, deliberately
deferred' scroll_to gap the original tree inspector spec called out."
```

---

## Task 3: Detail panel

**Files:**
- Modify: `include/prism/widgets/debug/tree_inspector.hpp`
- Modify: `tests/test_tree_inspector_model.cpp`
- Modify: `tests/test_tree_inspector_controller.cpp`
- Test (regression only, no edits): `tests/test_model_app.cpp` — same rationale as Task 2: the
  existing end-to-end integration test is not modified here either; its unmodified pass in Step 8
  is the regression proof that this task's `view()`/hstack change doesn't break the real hotkey
  path. Real, meaningful coverage of the detail panel lives in Step 1's dedicated
  `test_tree_inspector_model.cpp`/`test_tree_inspector_controller.cpp` tests.

**Interfaces:**
- Produces: `TreeInspectorModel::detail` (`Field<std::optional<NodeRow>>`), `Widget<
  std::optional<NodeRow>>` (namespace `prism::ui`), `NodeRow::operator==` (needed for
  `Field<std::optional<NodeRow>>::set` to compile — `ObservableValue<T,_>::set` guards on `value ==
  new_value`).

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_tree_inspector_model.cpp — append

TEST_CASE("Widget<std::optional<NodeRow>> shows a placeholder when empty") {
    prism::Field<std::optional<prism::debug::NodeRow>> field{std::nullopt};
    prism::Theme theme;
    prism::WidgetNode node;
    node.theme = &theme;

    prism::DrawList dl;
    prism::Widget<std::optional<prism::debug::NodeRow>>::record(dl, field, node);

    bool has_text = false;
    for (auto& cmd : dl.commands)
        if (std::holds_alternative<prism::TextCmd>(cmd)) has_text = true;
    CHECK(has_text);
}

TEST_CASE("Widget<std::optional<NodeRow>> renders every field when populated") {
    prism::debug::NodeRow row;
    row.name = "some_field";
    row.layout_kind_name = "Row";
    row.dirty = true;
    prism::Field<std::optional<prism::debug::NodeRow>> field{row};
    prism::Theme theme;
    prism::WidgetNode node;
    node.theme = &theme;

    prism::DrawList dl;
    prism::Widget<std::optional<prism::debug::NodeRow>>::record(dl, field, node);

    int text_commands = 0;
    for (auto& cmd : dl.commands)
        if (std::holds_alternative<prism::TextCmd>(cmd)) ++text_commands;
    // name, layout kind, rect, dirty, hovered, focused, pressed = 7 lines
    CHECK(text_commands == 7);
}

TEST_CASE("TreeInspectorModel::view places the list and detail pane side by side") {
    prism::debug::TreeInspectorModel model;
    prism::WidgetTree tree(model);
    // ViewBuilder::finalize()'s single-child Row/Column hoist fires here: view()'s top level is
    // now a single hstack (Row) call, so tree.root() itself becomes that Row, and its two
    // children (list, detail) are spliced directly onto it — verify this empirically rather
    // than assuming it holds after the view() change below (this exact codebase has twice
    // shipped a wrong WidgetTree-traversal assumption baked into a test before — see this
    // plan's Global Constraints).
    REQUIRE(tree.root().children.size() == 2);
    CHECK(tree.root().children[0].layout_kind == prism::LayoutKind::VirtualList);
}
```

```cpp
// tests/test_tree_inspector_controller.cpp — append

TEST_CASE("clicking a debug row populates the detail panel with that row's data") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    REQUIRE(!debug_model.rows.empty());
    auto clicked = debug_model.rows[debug_model.rows.size() - 1];
    controller.on_row_clicked(0, clicked);

    REQUIRE(debug_model.detail.get().has_value());
    CHECK(debug_model.detail.get()->id == clicked.id);
}

TEST_CASE("detail panel stays live on the next refresh") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    REQUIRE(!debug_model.rows.empty());
    auto clicked = debug_model.rows[debug_model.rows.size() - 1];
    controller.on_row_clicked(0, clicked);
    REQUIRE(debug_model.detail.get().has_value());

    // clicked.dirty is false here (build_snapshot + clear_dirty in this test's setup ran
    // before the click, and on_row_clicked's set_debug_highlight marks the tree's root dirty
    // for republish purposes, not the individual clicked leaf — confirmed via
    // WidgetTree::set_debug_highlight's mark_dirty_by_id(root_.id) call). Mutating the field
    // now flips that specific leaf's own dirty flag via the existing Field-to-WidgetNode
    // dirty wiring; the next refresh() must pick up the new value, not the stale one
    // captured at click time.
    CHECK_FALSE(clicked.dirty);
    main_model.value.set(main_model.value.get() + 1);
    controller.refresh();

    CHECK(debug_model.detail.get()->id == clicked.id);
    CHECK(debug_model.detail.get()->dirty == true);
}

TEST_CASE("clicking a different row switches the detail panel") {
    MainModel main_model;
    prism::WidgetTree main_tree(main_model);
    main_tree.build_snapshot(400, 300, 1);
    main_tree.clear_dirty();

    prism::debug::TreeInspectorModel debug_model;
    prism::WidgetTree debug_tree(debug_model);
    prism::debug::TreeInspectorController controller(main_tree, debug_tree, debug_model);
    controller.refresh();

    REQUIRE(debug_model.rows.size() >= 2);
    auto row_a = debug_model.rows[0];
    auto row_b = debug_model.rows[debug_model.rows.size() - 1];
    REQUIRE(row_a.id != row_b.id);

    controller.on_row_clicked(0, row_a);
    CHECK(debug_model.detail.get()->id == row_a.id);

    controller.on_row_clicked(0, row_b);
    CHECK(debug_model.detail.get()->id == row_b.id);
}

```

Note on scope: the spec's "selection disappears" testing bullet (a click-triggered ancestor
collapse hiding the previously-selected row) turns out to be unreachable through
`on_row_clicked` as designed — *any* click that collapses a container also reassigns
`detail_selected_` to that same container (still-visible, since only its descendants get hidden),
so no sequence of clicks alone can leave `detail_selected_` pointing at a now-hidden row. Confirmed
by tracing `on_row_clicked`'s Step 5 body below: it unconditionally sets `detail_selected_ = row.id`
on every call, including the one that also toggles `expanded_`. Genuinely exercising the
`if (!found)` branch would need the underlying main-tree structure to change out from under an
already-selected id (e.g. a `List<T>` shrinking to zero items) — this plan doesn't add a dedicated
test for that (it would depend on unverified `VirtualList` row-recycling/unbind internals outside
this plan's scope), but keeps the `if (!found) { detail_selected_.reset(); ... }` branch in `refresh()`
(Step 5 below) since it's a direct, low-risk consequence of the same "look up by id in the fresh
flatten" logic the other three tests above already exercise.

- [ ] **Step 2: Run to verify they fail**

```bash
ninja -C builddir
```
Expected: compile failure (`TreeInspectorModel::detail`, `Widget<std::optional<NodeRow>>` don't
exist yet).

- [ ] **Step 3: Add `NodeRow::operator==`**

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — NodeRow struct, add as the last member:
struct NodeRow {
    WidgetId id = 0;
    std::string name;
    std::string layout_kind_name;
    int depth = 0;
    Rect rect;
    bool dirty = false;
    bool hovered = false;
    bool focused = false;
    bool pressed = false;
    bool has_children = false;
    bool expanded = false;

    bool operator==(const NodeRow&) const = default;
};
```
Needed because `Field<std::optional<NodeRow>>::set(...)` (via `ObservableValue::set`) guards on
`value == new_value` before emitting a change — without this, the new `detail` field below fails to
compile.

- [ ] **Step 4: Add `TreeInspectorModel::detail`, change `view()` to an `hstack`, add
  `Widget<std::optional<NodeRow>>`**

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — add #include <fmt/format.h> to the top
// include block, alongside the existing <set>/<string>/<string_view>/<vector>.
```

```cpp
// Widget<NodeRow>'s existing specialization stays as is; add this new one directly after it,
// still inside the same `namespace prism::ui { ... }` block (tree_inspector.hpp:85-121):

template <>
struct Widget<std::optional<prism::debug::NodeRow>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height panel_h{200.f};

    static void record(DrawList& dl, const Field<std::optional<prism::debug::NodeRow>>& field,
                        WidgetNode& node) {
        auto& t = node_theme(node);
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, panel_h), t.surface);

        auto& value = field.get();
        if (!value) {
            dl.text("No selection", detail::make_point(X{8.f}, Y{8.f}), 13, t.text);
            return;
        }

        auto& row = *value;
        Y y{8.f};
        auto line = [&](const std::string& text) {
            dl.text(text, detail::make_point(X{8.f}, y), 13, t.text);
            y = y + DY{18.f};
        };
        line("name: " + row.name);
        line("layout: " + row.layout_kind_name);
        line(fmt::format("rect: {}, {}, {}, {}", row.rect.origin.x.raw(), row.rect.origin.y.raw(),
                          row.rect.extent.w.raw(), row.rect.extent.h.raw()));
        line(std::string("dirty: ") + (row.dirty ? "true" : "false"));
        line(std::string("hovered: ") + (row.hovered ? "true" : "false"));
        line(std::string("focused: ") + (row.focused ? "true" : "false"));
        line(std::string("pressed: ") + (row.pressed ? "true" : "false"));
    }

    static void handle_input(Field<std::optional<prism::debug::NodeRow>>&, const InputEvent&,
                              WidgetNode&) {
        // Read-only debug output — nothing to mutate on input.
    }
};
```

```cpp
// TreeInspectorModel — add the detail field and change view():
struct TreeInspectorModel {
    List<NodeRow> rows;
    Field<std::optional<WidgetId>> selected;
    Field<std::optional<NodeRow>> detail;
    std::function<void(size_t, const NodeRow&)> on_click;

    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.list<NodeRow>(rows, [this](size_t index, const NodeRow& row) {
                if (on_click) on_click(index, row);
            });
            vb.widget(detail);
        });
    }
};
```

- [ ] **Step 5: Wire `detail_selected_` into `TreeInspectorController`**

```cpp
// include/prism/widgets/debug/tree_inspector.hpp — TreeInspectorController:

    void refresh() {
        auto rows = flatten_tree(*main_tree_, expanded_);
        while (!debug_model_->rows.empty())
            debug_model_->rows.erase(debug_model_->rows.size() - 1);
        for (auto& row : rows)
            debug_model_->rows.push_back(row);

        auto hovered = main_tree_->hovered_id();
        if (hovered != 0) {
            debug_model_->selected.set(hovered);
            for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].id == hovered) {
                    debug_tree_->scroll_row_into_view(list_container_id_, i, Widget<NodeRow>::row_h);
                    break;
                }
            }
        }

        if (detail_selected_) {
            bool found = false;
            for (auto& row : rows) {
                if (row.id == *detail_selected_) {
                    debug_model_->detail.set(row);
                    found = true;
                    break;
                }
            }
            if (!found) {
                detail_selected_.reset();
                debug_model_->detail.set(std::nullopt);
            }
        }
    }

    void on_row_clicked(size_t, const NodeRow& row) {
        main_tree_->set_debug_highlight(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
        detail_selected_ = row.id;
        debug_model_->detail.set(row);
    }

private:
    WidgetTree* main_tree_;
    WidgetTree* debug_tree_;
    TreeInspectorModel* debug_model_;
    WidgetId list_container_id_;
    std::set<WidgetId> expanded_;
    std::optional<WidgetId> detail_selected_;
```

- [ ] **Step 6: Build and run the new tests**

```bash
ninja -C builddir
meson test -C builddir tree_inspector_model
meson test -C builddir tree_inspector_controller
```
Also re-run Task 2's auto-scroll controller test specifically — this task's `view()` change
(single `vb.list(...)` → `vb.hstack(...)`) is exactly the kind of `WidgetTree`-structure change that
could silently break the `list_container_id_ = debug_tree_->root().children.front().id` capture
from Task 2 if the hoist doesn't behave as the Step 4/Step 1 comments above assume:

```bash
meson test -C builddir tree_inspector_controller -- --test-case="hovering an off-screen row in the main tree scrolls the debug tree to reveal it"
```
If this specific test starts failing after Step 4's `view()` change (and didn't before), the hoist
assumption is wrong — dump `debug_tree.root().layout_kind` and `.children.size()` right after
construction to see the real structure before changing any production code.

- [ ] **Step 7: Build and run the full suite (regression proof, no test edits)**

```bash
ninja -C builddir
meson test -C builddir 2>&1 | tail -5
```
Expected: full suite passes, no regressions — in particular the existing, unmodified end-to-end
integration test in `tests/test_model_app.cpp` still passes as is, which is the regression proof
that Step 4's `TreeInspectorModel::view()` hstack change doesn't break the real hotkey path. Record
the final pass count.

- [ ] **Step 8: Commit**

```bash
git add include/prism/widgets/debug/tree_inspector.hpp tests/test_tree_inspector_model.cpp \
        tests/test_tree_inspector_controller.cpp
git commit -m "feat: add a live detail panel to the tree inspector debug window

TreeInspectorModel::view() is now an hstack of the existing row list
plus a new read-only Widget<std::optional<NodeRow>> pane, populated on
row click and kept live on every refresh() via
TreeInspectorController::detail_selected_. Resets to 'No selection' if
the selected row's ancestor collapses out of the flatten. Closes the
'no detail beyond the row itself' gap found during post-merge
dogfooding of the original tree inspector."
```
