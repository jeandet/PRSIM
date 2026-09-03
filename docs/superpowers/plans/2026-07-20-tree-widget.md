# Tree Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a generic, lazily-loaded tree widget (`vb.tree(...)`) covering filesystem, JSON-like, and reflected-recursive-struct use cases with one `TreeSource` contract, built entirely on the existing `LayoutKind::VirtualList` machinery.

**Architecture:** A type-erased `TreeSource` (mirrors `TableSource`) feeds a lazy `visible_rows()` flatten (only expanded nodes get queried for children) into a `TreeController` (owns `List<TreeRow> rows`, selection, detail state). `ViewBuilder::tree(TreeController&)` builds a `LayoutKind::VirtualList` container bound to those rows, plus a small, additive engine change (an opt-in `build_widget` hook on `VirtualList` containers) that lets this specific container hold real keyboard focus for Up/Down/Left/Right navigation — mirroring how `Table` already gets container-level focus, but opt-in instead of hardcoded, so no existing `.list()` caller is affected.

**Tech Stack:** C++23/26 (P2996 reflection where gated), existing PRISM core (`Field<T>`, `List<T>`, `WidgetTree`), doctest, meson.

**Spec:** `docs/superpowers/specs/2026-07-20-tree-widget-design.md` — read it for full rationale on every design choice below (three API tiers, lazy loading, icon/attribute design, non-goals).

## Global Constraints

- No new `LayoutKind` — everything builds on `LayoutKind::VirtualList` (existing).
- `TreeNodeId = uint64_t` (opaque handle, mirrors `WidgetId`'s own alias).
- No dependency on `Field<T>`/`is_field_v` gating for the Tier-3 reflection walk (deliberate, documented deviation — see spec).
- No cycle detection on the Tier-3 pointer-following walk (documented non-goal).
- No container-of-children (`std::vector<Child>`) support on the Tier-3 reflection walk (documented v1 boundary).
- Full test suite (`meson test -C builddir`) must pass with 0 failures before any commit that isn't itself a fix-up of a failing test within the same task.
- Every task ends with a commit.

---

## Verified engine facts (read before starting — these drove every design choice below)

Gathered by reading `include/prism/app/widget_tree.hpp`, `include/prism/ui/node.hpp`, `include/prism/ui/widget_node.hpp` directly (not assumed):

1. **`Node` (compile-time tree, `include/prism/ui/node.hpp`) has no `focus_policy` or `wire` field.** Those live only on `WidgetNode` (runtime tree). The only way a `Node` can customize its resulting `WidgetNode` is via `Node::build_widget` (`std::function<void(WidgetNode&)>`), invoked once during `WidgetTree::build_widget_node()`.
2. **`build_widget_node()`'s `LayoutKind::VirtualList` branch currently never calls `node.build_widget`** (unlike its `Scroll` branch, which does). This is why a plain `vb.list()` container can't be made focusable today — there's no hook.
3. **`build_index()`'s `focus_order_` eligibility check is `!node.is_container || node.layout_kind == LayoutKind::Table`** — containers are excluded from keyboard-focus eligibility except `Table`, which gets a hardcoded carve-out. `WidgetTree::set_focused(id)` requires `id` to already be in `focus_order_`, so without a carve-out, a `VirtualList` container can never hold focus via any path (Tab or click).
4. **`materialize_virtual_list()` recycles `WidgetNode`s per pool *slot*, not per logical item** — a row's `WidgetId` is stable only for as long as it stays in the same viewport slot; it's reassigned across scroll/re-materialization. This rules out tracking keyboard focus on individual rows (confirmed by reading the pool reuse logic directly) — container-level focus (Table's approach) is the only stable option for a virtualized list.
5. Table's own container-level keyboard handling lives inline inside `materialize_table()`, wiring `node.on_input.connect(...)` directly — this plan uses the more general `Node::build_widget` + `wn.wire` mechanism instead (already used by `Scroll` and the `Tabs` bar leaf), which is additive and doesn't require a new per-container `materialize_*` engine function.
6. `ViewBuilder::list<T>()` (`widget_tree.hpp:213`) is the proven precedent this plan's row-binding code mirrors almost verbatim, just specialized to a fixed `TreeRow` type instead of a template parameter `T`.

**Conclusion driving Task 5 below:** the only engine changes needed are (a) one line in `build_widget_node()`'s `VirtualList` branch calling `node.build_widget` if set, and (b) widening the `focus_order_` carve-out to include `LayoutKind::VirtualList`. Both are strictly opt-in (gated on fields no existing `.list()` caller ever sets), so no existing test or shipped feature (including the dogfooded debug tree inspector) is affected.

---

### Task 1: `TreeSource` data contract + `TreeStorage` concept

**Files:**
- Create: `include/prism/ui/tree.hpp`
- Test: `tests/test_tree.cpp`
- Modify: `tests/meson.build` (register `'tree' : files('test_tree.cpp'),` in the `headless_tests` map — the map isn't alphabetized, just append near the other `tree_*`/`table`/`flatten_tree` entries at the end)

**Interfaces:**
- Produces: `prism::ui::TreeNodeId` (`= uint64_t`), `prism::ui::TreeSource` (struct of `std::function`s: `root_count`, `root_at`, `child_count`, `child_at`, `label`, `has_children`, `icon` (optional), `attributes` (optional)), `prism::ui::TreeStorage` concept, `prism::ui::wrap_tree_storage<T>(T&)`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

#include <string>
#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// A tiny in-memory tree: root(1) -> {child(2), child(3)}; child(2) -> {grandchild(4)}
struct FixtureTree {
    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(prism::TreeNodeId id) const {
        if (id == 1) return 2;
        if (id == 2) return 1;
        return 0;
    }
    prism::TreeNodeId child_at(prism::TreeNodeId id, size_t i) const {
        if (id == 1) return i == 0 ? 2 : 3;
        if (id == 2) return 4;
        return 0;
    }
    std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(prism::TreeNodeId id) const { return id == 1 || id == 2; }
};

static_assert(prism::TreeStorage<FixtureTree>);

TEST_CASE("wrap_tree_storage produces a valid TreeSource") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    CHECK(src.root_count() == 1);
    CHECK(src.root_at(0) == 1);
    CHECK(src.child_count(1) == 2);
    CHECK(src.child_at(1, 0) == 2);
    CHECK(src.child_at(1, 1) == 3);
    CHECK(src.label(4) == "n4");
    CHECK(src.has_children(1) == true);
    CHECK(src.has_children(3) == false);
    CHECK_FALSE(src.icon);        // optional, unset when the source has no icon() method
    CHECK_FALSE(src.attributes);  // optional, unset when the source has no attributes() method
}

struct FixtureTreeWithIcon : FixtureTree {
    std::optional<std::string> icon(prism::TreeNodeId id) const {
        return id == 1 ? std::optional<std::string>{"root-icon"} : std::nullopt;
    }
};

TEST_CASE("wrap_tree_storage forwards icon() when the source provides it") {
    FixtureTreeWithIcon data;
    auto src = prism::wrap_tree_storage(data);
    REQUIRE(src.icon);
    CHECK(src.icon(1) == std::optional<std::string>{"root-icon"});
    CHECK(src.icon(2) == std::nullopt);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson setup builddir` (if not already set up) `&& meson compile -C builddir test_tree`
Expected: FAIL — `include/prism/ui/tree.hpp` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

```cpp
// include/prism/ui/tree.hpp
#pragma once

#include <prism/core/types.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism::ui {
using namespace prism::core;

using TreeNodeId = uint64_t;

struct TreeSource {
    std::function<size_t()> root_count;
    std::function<TreeNodeId(size_t)> root_at;
    std::function<size_t(TreeNodeId)> child_count;
    std::function<TreeNodeId(TreeNodeId, size_t)> child_at;
    std::function<std::string(TreeNodeId)> label;
    std::function<bool(TreeNodeId)> has_children;
    std::function<std::optional<std::string>(TreeNodeId)> icon;
    std::function<std::vector<std::pair<std::string, std::string>>(TreeNodeId)> attributes;
};

template <typename T>
concept TreeStorage = requires(const T& t, TreeNodeId id, size_t i) {
    { t.root_count() } -> std::convertible_to<size_t>;
    { t.root_at(i) } -> std::convertible_to<TreeNodeId>;
    { t.child_count(id) } -> std::convertible_to<size_t>;
    { t.child_at(id, i) } -> std::convertible_to<TreeNodeId>;
    { t.label(id) } -> std::convertible_to<std::string>;
    { t.has_children(id) } -> std::convertible_to<bool>;
};

template <TreeStorage T>
TreeSource wrap_tree_storage(T& data) {
    TreeSource src;
    src.root_count = [&data] { return data.root_count(); };
    src.root_at = [&data](size_t i) { return data.root_at(i); };
    src.child_count = [&data](TreeNodeId id) { return data.child_count(id); };
    src.child_at = [&data](TreeNodeId id, size_t i) { return data.child_at(id, i); };
    src.label = [&data](TreeNodeId id) { return data.label(id); };
    src.has_children = [&data](TreeNodeId id) { return data.has_children(id); };
    if constexpr (requires(const T& t, TreeNodeId id) { t.icon(id); }) {
        src.icon = [&data](TreeNodeId id) { return data.icon(id); };
    }
    if constexpr (requires(const T& t, TreeNodeId id) { t.attributes(id); }) {
        src.attributes = [&data](TreeNodeId id) { return data.attributes(id); };
    }
    return src;
}

} // namespace prism::ui
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree && ./builddir/tests/test_tree`
Expected: PASS, all `TEST_CASE`s green.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/tree.hpp tests/test_tree.cpp tests/meson.build
git commit -m "feat: add TreeSource data contract and wrap_tree_storage()"
```

---

### Task 2: `TreeRow` + lazy `visible_rows()`

**Files:**
- Modify: `include/prism/ui/tree.hpp`
- Modify: `tests/test_tree.cpp`

**Interfaces:**
- Consumes: `TreeSource`, `TreeNodeId` (Task 1).
- Produces: `TreeRow` (`id`, `label`, `icon`, `depth`, `has_children`, `expanded`, `selected`), `visible_rows(const TreeSource&, const std::set<TreeNodeId>&, std::optional<TreeNodeId> selected = std::nullopt) -> std::vector<TreeRow>`.

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/test_tree.cpp
#include <set>

TEST_CASE("visible_rows shows only roots when nothing is expanded") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded;
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == 1);
    CHECK(rows[0].label == "n1");
    CHECK(rows[0].depth == 0);
    CHECK(rows[0].has_children == true);
    CHECK(rows[0].expanded == false);
}

TEST_CASE("visible_rows descends into expanded nodes, depth-first") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded{1, 2};
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].id == 1); CHECK(rows[0].depth == 0);
    CHECK(rows[1].id == 2); CHECK(rows[1].depth == 1);
    CHECK(rows[2].id == 4); CHECK(rows[2].depth == 2); // child(2)'s grandchild, visited before sibling 3
    CHECK(rows[3].id == 3); CHECK(rows[3].depth == 1);
}

TEST_CASE("visible_rows never queries children of a collapsed node") {
    // The core lazy-loading guarantee: child_count/child_at must never be invoked
    // for a node that isn't in `expanded`, regardless of how many children it has.
    prism::TreeSource src;
    src.root_count = [] { return 1; };
    src.root_at = [](size_t) -> prism::TreeNodeId { return 1; };
    src.has_children = [](prism::TreeNodeId) { return true; };
    src.label = [](prism::TreeNodeId id) { return "n" + std::to_string(id); };
    src.child_count = [](prism::TreeNodeId) -> size_t {
        FAIL("child_count queried for a collapsed node");
        return 0;
    };
    src.child_at = [](prism::TreeNodeId, size_t) -> prism::TreeNodeId {
        FAIL("child_at queried for a collapsed node");
        return 0;
    };
    std::set<prism::TreeNodeId> expanded; // empty -- root stays collapsed
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].has_children == true);
}

TEST_CASE("visible_rows marks the selected node's row") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded{1};
    auto rows = prism::visible_rows(src, expanded, prism::TreeNodeId{3});
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].selected == false); // id 1
    CHECK(rows[1].selected == false); // id 2
    CHECK(rows[2].selected == true);  // id 3
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree`
Expected: FAIL to compile — `TreeRow`/`visible_rows` undefined.

- [ ] **Step 3: Write minimal implementation**

Append to `include/prism/ui/tree.hpp` (before the closing `} // namespace prism::ui`):

```cpp
#include <set>

struct TreeRow {
    TreeNodeId id = 0;
    std::string label;
    std::optional<std::string> icon;
    int depth = 0;
    bool has_children = false;
    bool expanded = false;
    bool selected = false;

    bool operator==(const TreeRow&) const = default;
};

namespace detail_tree {
inline void flatten_node(const TreeSource& source, TreeNodeId id, int depth,
                          const std::set<TreeNodeId>& expanded,
                          std::optional<TreeNodeId> selected,
                          std::vector<TreeRow>& out) {
    TreeRow row;
    row.id = id;
    row.label = source.label(id);
    row.icon = source.icon ? source.icon(id) : std::nullopt;
    row.depth = depth;
    row.has_children = source.has_children(id);
    row.expanded = expanded.contains(id);
    row.selected = selected.has_value() && *selected == id;
    out.push_back(row);

    if (row.has_children && row.expanded) {
        size_t n = source.child_count(id);
        for (size_t i = 0; i < n; ++i)
            flatten_node(source, source.child_at(id, i), depth + 1, expanded, selected, out);
    }
}
} // namespace detail_tree

inline std::vector<TreeRow> visible_rows(const TreeSource& source,
                                          const std::set<TreeNodeId>& expanded,
                                          std::optional<TreeNodeId> selected = std::nullopt) {
    std::vector<TreeRow> rows;
    size_t n = source.root_count();
    for (size_t i = 0; i < n; ++i)
        detail_tree::flatten_node(source, source.root_at(i), 0, expanded, selected, rows);
    return rows;
}
```

Move the `#include <set>` to the top of the file with the other includes rather than mid-file (shown inline above only to mark where the new dependency comes from).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree && ./builddir/tests/test_tree`
Expected: PASS.

Then run the full suite before committing (per this repo's standing rule: read the real pass/fail count and exit code, don't infer success from a partial grep):

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/tree.hpp tests/test_tree.cpp
git commit -m "feat: add TreeRow and lazy visible_rows() flatten"
```

---

### Task 3: `TreeDetail` + `Widget<TreeRow>` + `Widget<std::optional<TreeDetail>>`

**Files:**
- Modify: `include/prism/ui/tree.hpp` (add `#include <prism/ui/delegate.hpp>`, `#include <prism/core/field.hpp>` at top)
- Create: `tests/test_tree_widget.cpp`
- Modify: `tests/meson.build` (register `'tree_widget' : files('test_tree_widget.cpp'),`)

**Interfaces:**
- Consumes: `TreeRow` (Task 2), `prism::ui::Widget<T>` primary template, `WidgetNode`, `Field<T>`, `node_vs`/`node_theme`/`detail::make_rect`/`detail::make_point`/`detail::widget_w` (all from `include/prism/ui/delegate.hpp`).
- Produces: `TreeDetail` (`label`, `attributes`), `prism::ui::Widget<TreeRow>` (`record`/`handle_input`, `static constexpr Height row_h{22.f}`, `focus_policy = FocusPolicy::none`), `prism::ui::Widget<std::optional<TreeDetail>>`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_widget.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
prism::Theme test_theme;
prism::WidgetNode make_node() {
    prism::WidgetNode node;
    node.theme = &test_theme;
    return node;
}
}

TEST_CASE("Widget<TreeRow> renders the row's label") {
    prism::TreeRow row;
    row.label = "hello";
    prism::Field<prism::TreeRow> field{row};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TreeRow>::record(dl, field, node);

    bool found = false;
    for (auto& cmd : dl.commands)
        if (auto* t = std::get_if<prism::TextCmd>(&cmd))
            if (t->text.find("hello") != std::string_view::npos) found = true;
    CHECK(found);
}

TEST_CASE("Widget<TreeRow> shows an expand marker only when has_children is true") {
    prism::TreeRow leaf;
    leaf.label = "leaf";
    leaf.has_children = false;
    prism::Field<prism::TreeRow> leaf_field{leaf};
    prism::DrawList leaf_dl;
    auto leaf_node = make_node();
    prism::Widget<prism::TreeRow>::record(leaf_dl, leaf_field, leaf_node);

    prism::TreeRow branch;
    branch.label = "branch";
    branch.has_children = true;
    branch.expanded = false;
    prism::Field<prism::TreeRow> branch_field{branch};
    prism::DrawList branch_dl;
    auto branch_node = make_node();
    prism::Widget<prism::TreeRow>::record(branch_dl, branch_field, branch_node);

    auto text_of = [](const prism::DrawList& dl) {
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd)) return std::string(t->text);
        return std::string{};
    };
    CHECK(text_of(leaf_dl).find(">") == std::string::npos);
    CHECK(text_of(branch_dl).find(">") != std::string::npos);
}

TEST_CASE("Widget<std::optional<TreeDetail>> shows 'No selection' when empty") {
    prism::Field<std::optional<prism::TreeDetail>> field{std::nullopt};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<std::optional<prism::TreeDetail>>::record(dl, field, node);

    bool found = false;
    for (auto& cmd : dl.commands)
        if (auto* t = std::get_if<prism::TextCmd>(&cmd))
            if (t->text.find("No selection") != std::string_view::npos) found = true;
    CHECK(found);
}

TEST_CASE("Widget<std::optional<TreeDetail>> renders label and attributes when set") {
    prism::TreeDetail detail;
    detail.label = "my-node";
    detail.attributes = {{"size", "42"}, {"kind", "file"}};
    prism::Field<std::optional<prism::TreeDetail>> field{detail};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<std::optional<prism::TreeDetail>>::record(dl, field, node);

    auto has_text = [&dl](std::string_view needle) {
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd))
                if (t->text.find(needle) != std::string_view::npos) return true;
        return false;
    };
    CHECK(has_text("my-node"));
    CHECK(has_text("size: 42"));
    CHECK(has_text("kind: file"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree_widget`
Expected: FAIL to compile — `TreeDetail`/`Widget<TreeRow>`/`Widget<std::optional<TreeDetail>>` undefined.

- [ ] **Step 3: Write minimal implementation**

Add to the top of `include/prism/ui/tree.hpp`:

```cpp
#include <prism/core/field.hpp>
#include <prism/ui/delegate.hpp>
```

Append inside `namespace prism::ui { ... }`, after `TreeRow`'s definition:

```cpp
struct TreeDetail {
    std::string label;
    std::vector<std::pair<std::string, std::string>> attributes;

    bool operator==(const TreeDetail&) const = default;
};

template <>
struct Widget<TreeRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height row_h{22.f};

    static void record(DrawList& dl, const Field<TreeRow>& field, WidgetNode& node) {
        auto& row = field.get();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        bool highlight = vs.hovered || row.selected;
        auto bg = highlight ? t.surface_hover : t.surface;
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, row_h), bg);

        DX indent{static_cast<float>(row.depth) * 16.f};
        std::string marker = row.has_children ? (row.expanded ? "v " : "> ") : "  ";
        std::string icon_part = row.icon ? (*row.icon + " ") : std::string{};
        dl.text(marker + icon_part + row.label,
                 detail::make_point(X{8.f} + indent, Y{4.f}), 13, t.text);
    }

    // Selection/expand-toggle/keyboard nav are handled centrally by TreeController via the
    // container's row-click and container-level keyboard wiring (see ViewBuilder::tree(),
    // Task 5/6) -- an individual row has nothing of its own to mutate on input.
    static void handle_input(Field<TreeRow>&, const InputEvent&, WidgetNode&) {}
};

template <>
struct Widget<std::optional<TreeDetail>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height panel_h{200.f};

    static void record(DrawList& dl, const Field<std::optional<TreeDetail>>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, panel_h), t.surface);

        auto& value = field.get();
        if (!value) {
            dl.text("No selection", detail::make_point(X{8.f}, Y{8.f}), 13, t.text);
            return;
        }
        Y y{8.f};
        dl.text(value->label, detail::make_point(X{8.f}, y), 13, t.text);
        y = y + DY{18.f};
        for (auto& [k, v] : value->attributes) {
            dl.text(k + ": " + v, detail::make_point(X{8.f}, y), 13, t.text);
            y = y + DY{18.f};
        }
    }

    static void handle_input(Field<std::optional<TreeDetail>>&, const InputEvent&, WidgetNode&) {}
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree_widget && ./builddir/tests/test_tree_widget`
Expected: PASS.

Then run the full suite before committing:

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/tree.hpp tests/test_tree_widget.cpp tests/meson.build
git commit -m "feat: add TreeDetail and Widget<TreeRow>/Widget<optional<TreeDetail>> delegates"
```

---

### Task 4: `TreeController`

**Files:**
- Modify: `include/prism/ui/tree.hpp` (add `#include <prism/core/list.hpp>`)
- Create: `tests/test_tree_controller.cpp`
- Modify: `tests/meson.build` (register `'tree_controller' : files('test_tree_controller.cpp'),`)

**Interfaces:**
- Consumes: `TreeSource`, `TreeRow`, `visible_rows` (Tasks 1-2), `TreeDetail` (Task 3), `List<T>`/`Field<T>` (existing core).
- Produces: `prism::ui::TreeController` — public members `List<TreeRow> rows`, `Field<std::optional<TreeNodeId>> selected`, `Field<std::optional<TreeDetail>> detail`; public methods `explicit TreeController(TreeSource source)`, `void refresh()`, `void on_row_clicked(size_t index, const TreeRow& row)`, `std::optional<size_t> on_key(const InputEvent& ev)` (returns the row index that should be scrolled into view, or `std::nullopt`).

No `WidgetTree` dependency — this class is fully unit-testable in isolation, which is what this task's tests do.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_controller.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>
#include <prism/input/input_event.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
// root(1) -> {child(2), child(3)}; child(2) -> {grandchild(4)}
struct FixtureTree {
    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(prism::TreeNodeId id) const {
        if (id == 1) return 2;
        if (id == 2) return 1;
        return 0;
    }
    prism::TreeNodeId child_at(prism::TreeNodeId id, size_t i) const {
        if (id == 1) return i == 0 ? 2 : 3;
        if (id == 2) return 4;
        return 0;
    }
    std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(prism::TreeNodeId id) const { return id == 1 || id == 2; }
    std::vector<std::pair<std::string, std::string>> attributes(prism::TreeNodeId id) const {
        return {{"id", std::to_string(id)}};
    }
};
}

TEST_CASE("TreeController starts with only the root visible, collapsed") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    REQUIRE(ctrl.rows.size() == 1);
    CHECK(ctrl.rows[0].id == 1);
    CHECK(ctrl.rows[0].expanded == false);
    CHECK(ctrl.selected.get() == std::nullopt);
}

TEST_CASE("clicking a row with children selects it and toggles expand") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]);

    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
    REQUIRE(ctrl.rows.size() == 3); // root + its two children now visible
    CHECK(ctrl.rows[0].expanded == true);
    REQUIRE(ctrl.detail.get().has_value());
    CHECK(ctrl.detail.get()->label == "n1");
    CHECK(ctrl.detail.get()->attributes == std::vector<std::pair<std::string,std::string>>{{"id", "1"}});
}

TEST_CASE("clicking an already-expanded row collapses it again") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand
    ctrl.on_row_clicked(0, ctrl.rows[0]); // collapse
    REQUIRE(ctrl.rows.size() == 1);
    CHECK(ctrl.rows[0].expanded == false);
}

TEST_CASE("clicking a leaf row selects it without changing row count") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand root -> rows = [1, 2, 3]
    REQUIRE(ctrl.rows.size() == 3);
    ctrl.on_row_clicked(2, ctrl.rows[2]); // click leaf id 3
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{3});
    CHECK(ctrl.rows.size() == 3);
}

TEST_CASE("Down arrow moves selection to the next visible row and returns its index") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand root -> rows = [1, 2, 3]
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::down, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 1);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("Up arrow at the first row does not move selection and returns nullopt") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::up, 0});
    CHECK_FALSE(idx.has_value());
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Right arrow expands a collapsed selected node without moving selection") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::right, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
    CHECK(ctrl.rows[0].expanded == true);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1}); // unchanged
}

TEST_CASE("Right arrow on an already-expanded node moves to its first child") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::right, 0});
    REQUIRE(idx.has_value());
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("Left arrow collapses an expanded selected node without moving selection") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::left, 0});
    REQUIRE(idx.has_value());
    CHECK(ctrl.rows[0].expanded == false);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Left arrow on a collapsed child moves selection to its parent") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand root -> rows = [1, 2, 3]
    ctrl.selected.set(prism::TreeNodeId{3});             // select leaf child, no children of its own

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::left, 0});
    REQUIRE(idx.has_value());
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Enter on the selected row has the same effect as clicking it") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::enter, 0});
    REQUIRE(idx.has_value());
    CHECK(ctrl.rows[0].expanded == true);
}

TEST_CASE("on_key ignores non-KeyPress events") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    auto idx = ctrl.on_key(prism::MouseMove{});
    CHECK_FALSE(idx.has_value());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree_controller`
Expected: FAIL to compile — `TreeController` undefined.

- [ ] **Step 3: Write minimal implementation**

Add `#include <prism/core/list.hpp>` and `#include <prism/input/input_event.hpp>` to the top of `include/prism/ui/tree.hpp`. Append, after the `Widget<std::optional<TreeDetail>>` specialization:

```cpp
class TreeController {
public:
    explicit TreeController(TreeSource source) : source_(std::move(source)) {
        refresh();
    }

    List<TreeRow> rows;
    Field<std::optional<TreeNodeId>> selected;
    Field<std::optional<TreeDetail>> detail;

    void refresh() {
        auto fresh = visible_rows(source_, expanded_, selected.get());
        while (!rows.empty()) rows.erase(rows.size() - 1);
        for (auto& r : fresh) rows.push_back(r);
    }

    void on_row_clicked(size_t, const TreeRow& row) {
        selected.set(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
        populate_detail(row.id);
        refresh();
    }

    // Returns the index in `rows` that should be scrolled into view, if selection moved or
    // an expand/collapse happened; nullopt if `ev` wasn't a handled KeyPress.
    std::optional<size_t> on_key(const InputEvent& ev) {
        auto* kp = std::get_if<KeyPress>(&ev);
        if (!kp) return std::nullopt;

        std::vector<TreeRow> snapshot;
        for (size_t i = 0; i < rows.size(); ++i) snapshot.push_back(rows[i]);
        if (snapshot.empty()) return std::nullopt;

        auto sel = selected.get();
        size_t cur = 0;
        bool has_cur = false;
        if (sel) {
            for (size_t i = 0; i < snapshot.size(); ++i)
                if (snapshot[i].id == *sel) { cur = i; has_cur = true; break; }
        }

        if (kp->key == keys::down) {
            size_t next = has_cur ? cur + 1 : 0;
            if (next >= snapshot.size()) return std::nullopt;
            select_row(snapshot[next].id);
            return next;
        }
        if (kp->key == keys::up) {
            if (!has_cur || cur == 0) return std::nullopt;
            select_row(snapshot[cur - 1].id);
            return cur - 1;
        }
        if (kp->key == keys::right) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.has_children && !row.expanded) {
                expanded_.insert(row.id);
                refresh();
                return cur;
            }
            if (row.expanded && cur + 1 < snapshot.size()) {
                select_row(snapshot[cur + 1].id);
                return cur + 1;
            }
            return std::nullopt;
        }
        if (kp->key == keys::left) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.expanded) {
                expanded_.erase(row.id);
                refresh();
                return cur;
            }
            for (size_t i = cur; i-- > 0; ) {
                if (snapshot[i].depth < row.depth) {
                    select_row(snapshot[i].id);
                    return i;
                }
            }
            return std::nullopt;
        }
        if (kp->key == keys::enter || kp->key == keys::space) {
            if (!has_cur) return std::nullopt;
            on_row_clicked(cur, snapshot[cur]);
            return cur;
        }
        return std::nullopt;
    }

private:
    void select_row(TreeNodeId id) {
        selected.set(id);
        populate_detail(id);
    }

    void populate_detail(TreeNodeId id) {
        TreeDetail d;
        d.label = source_.label(id);
        d.attributes = source_.attributes ? source_.attributes(id)
                                           : std::vector<std::pair<std::string, std::string>>{};
        detail.set(d);
    }

    TreeSource source_;
    std::set<TreeNodeId> expanded_;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree_controller && ./builddir/tests/test_tree_controller`
Expected: PASS.

Then run the full suite before committing:

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/tree.hpp tests/test_tree_controller.cpp tests/meson.build
git commit -m "feat: add TreeController (refresh, click, and keyboard-nav logic)"
```

---

### Task 5: Engine hook + `ViewBuilder::tree()` (container, row binding, click)

This is the task described in "Verified engine facts" above — the engine change and its only caller are built together because the engine change has no independently observable effect (see the note there on why Task 5 isn't split further).

**Files:**
- Modify: `include/prism/app/widget_tree.hpp`:
  - Add `#include <prism/ui/tree.hpp>` near the top, alongside the existing `#include <prism/ui/table.hpp>` (line 7).
  - In `build_widget_node()`'s `LayoutKind::VirtualList` branch (around line 863-869), add one line calling `node.build_widget` if set.
  - In `build_index()`'s focus-eligibility check (around line 1121-1123), widen the `LayoutKind::Table` carve-out to also include `LayoutKind::VirtualList`.
  - Add a new `ViewBuilder::tree(TreeController& ctrl)` method, in the `ViewBuilder` class body near `list<T>()` and `table()`.
- Create: `tests/test_tree_view_builder.cpp`
- Modify: `tests/meson.build` (register `'tree_view_builder' : files('test_tree_view_builder.cpp'),`)

**Interfaces:**
- Consumes: `TreeController`, `TreeRow`, `Widget<TreeRow>`, `Widget<std::optional<TreeDetail>>` (Tasks 3-4); `Node`, `WidgetNode`, `Field<T>`, `List<T>::on_insert/on_remove/on_update`, `ViewBuilder::hstack`/`widget`/`push_container` (existing).
- Produces: `void ViewBuilder::tree(TreeController& ctrl)` — embeds a tree list + detail pane as an `hstack`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_view_builder.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
// root(1) -> {child(2), child(3)}
struct FixtureTree {
    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(prism::TreeNodeId id) const { return id == 1 ? 2 : 0; }
    prism::TreeNodeId child_at(prism::TreeNodeId, size_t i) const { return i == 0 ? 2 : 3; }
    std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(prism::TreeNodeId id) const { return id == 1; }
};

struct TreeModel {
    prism::TreeController ctrl;
    TreeModel() : ctrl(prism::wrap_tree_storage(fixture_)) {}
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
private:
    FixtureTree fixture_;
};
}

TEST_CASE("vb.tree() builds without crashing and produces geometry") {
    TreeModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap->geometry.size() > 0);
}

TEST_CASE("the tree container is focusable via focus_order") {
    TreeModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    // Exactly one focusable target: the tree container itself (rows are focus_policy::none).
    CHECK(tree.focus_order().size() == 1);
}

TEST_CASE("clicking the root row expands it and updates the row count") {
    TreeModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!model.ctrl.rows.empty());
    // The root row is the sole entry before any click.
    CHECK(model.ctrl.rows.size() == 1);

    // Find the root row's on-screen WidgetId via the snapshot geometry produced by the
    // VirtualList's own row-binding (mirrors the pattern already established in
    // tests/test_tree_inspector_controller.cpp and test_virtual_list.cpp) -- confirm empirically
    // which geometry entry belongs to the row before trusting an assumed index or ordering here;
    // do not assume front()/back() without checking against a real dump first.
    REQUIRE(!snap->geometry.empty());
    auto row_id = snap->geometry.front().first;

    tree.dispatch(row_id, prism::MouseButton{prism::Point{prism::X{5}, prism::Y{5}}, 1, true});

    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
    CHECK(model.ctrl.rows.size() == 3); // root + 2 children now visible
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree_view_builder`
Expected: FAIL to compile — `ViewBuilder` has no member `tree`.

- [ ] **Step 3: Write minimal implementation**

In `include/prism/app/widget_tree.hpp`, add near the top (after line 7's `#include <prism/ui/table.hpp>`):

```cpp
#include <prism/ui/tree.hpp>
```

In `build_widget_node()` (private section), inside the `else if (node.layout_kind == LayoutKind::VirtualList)` branch, change:

```cpp
            } else if (node.layout_kind == LayoutKind::VirtualList) {
                auto vls = std::make_shared<VirtualListState>();
                vls->item_count = ItemCount{node.vlist_item_count};
                vls->event_policy = node.scroll_event_policy;
                if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
                wn.edit_state = vls;
            } else if (node.layout_kind == LayoutKind::Table && node.table_state) {
```

to:

```cpp
            } else if (node.layout_kind == LayoutKind::VirtualList) {
                auto vls = std::make_shared<VirtualListState>();
                vls->item_count = ItemCount{node.vlist_item_count};
                vls->event_policy = node.scroll_event_policy;
                if (node.vlist_bind_row) vls->bind_row = node.vlist_bind_row;
                if (node.vlist_unbind_row) vls->unbind_row = node.vlist_unbind_row;
                wn.edit_state = vls;
                // Opt-in only: no existing .list() caller sets build_widget, so this is a no-op
                // for every VirtualList container except the one ViewBuilder::tree() builds below.
                if (node.build_widget)
                    node.build_widget(wn);
            } else if (node.layout_kind == LayoutKind::Table && node.table_state) {
```

In `build_index()`, change:

```cpp
        if (node.focus_policy != FocusPolicy::none &&
            (!node.is_container || node.layout_kind == LayoutKind::Table))
            focus_order_.push_back(node.id);
```

to:

```cpp
        if (node.focus_policy != FocusPolicy::none &&
            (!node.is_container || node.layout_kind == LayoutKind::Table ||
             node.layout_kind == LayoutKind::VirtualList))
            focus_order_.push_back(node.id);
```

In the `ViewBuilder` class body (public section, near `list<T>()`), add:

```cpp
        void tree(TreeController& ctrl) {
            hstack([&] {
                Node container;
                container.id = tree_.next_id_++;
                container.is_leaf = false;
                container.layout_kind = LayoutKind::VirtualList;
                container.vlist_item_count = ctrl.rows.size();

                container.build_widget = [](WidgetNode& wn) {
                    wn.focus_policy = FocusPolicy::tab_and_click;
                };

                container.vlist_bind_row = [&ctrl](WidgetNode& wn, size_t index) {
                    auto field_ptr = std::make_shared<Field<TreeRow>>(ctrl.rows[index]);
                    wn.edit_state = std::shared_ptr<void>(field_ptr);
                    wn.focus_policy = FocusPolicy::none; // rows never take focus; the container does
                    wn.dirty = true;
                    wn.is_container = false;
                    wn.draws.clear();
                    wn.overlay_draws.clear();
                    wn.record = [field_ptr](WidgetNode& node) {
                        node.draws.clear();
                        node.overlay_draws.clear();
                        Widget<TreeRow>::record(node.draws, *field_ptr, node);
                    };
                    wn.record(wn);
                    wn.wire = [field_ptr, &ctrl, index](WidgetNode& node) {
                        node.connections.push_back(
                            node.on_input.connect([&ctrl, index](const InputEvent& ev) {
                                auto* mb = std::get_if<MouseButton>(&ev);
                                if (mb && mb->pressed && mb->button == 1 && index < ctrl.rows.size())
                                    ctrl.on_row_clicked(index, ctrl.rows[index]);
                            })
                        );
                    };
                };

                container.vlist_unbind_row = [](WidgetNode& wn) {
                    wn.connections.clear();
                    wn.draws.clear();
                    wn.overlay_draws.clear();
                    wn.edit_state.reset();
                    wn.wire = nullptr;
                    wn.record = nullptr;
                    wn.dirty = false;
                };

                container.vlist_on_insert = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                    return ctrl.rows.on_insert().connect([cb = std::move(cb)](size_t, const auto&) { cb(); });
                };
                container.vlist_on_remove = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                    return ctrl.rows.on_remove().connect([cb = std::move(cb)](size_t) { cb(); });
                };
                container.vlist_on_update = [&ctrl](size_t, std::function<void()> cb) -> Connection {
                    return ctrl.rows.on_update().connect([cb = std::move(cb)](size_t, const auto&) { cb(); });
                };

                current_parent().children.push_back(std::move(container));
                widget(ctrl.detail);
            });
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree_view_builder && ./builddir/tests/test_tree_view_builder`
Expected: PASS. If the click test's row-id assumption (`snap->geometry.front().first`) doesn't match the root row in practice, add a small throwaway debug dump of `snap->geometry` (id + rect) to determine the right entry empirically, then fix the assertion — do not guess a second time, mirroring the established project discipline in `docs/superpowers/plans/2026-07-16-live-tree-inspector.md` and the memory this repo already has on the topic (verify plan test geometry against real traversal order, not assumption).

Then run the FULL suite to confirm the engine changes are backward-compatible:

Run: `meson test -C builddir`
Expected: all existing tests (in particular `virtual_list`, `table`, `tabs`, `flatten_tree`, `tree_inspector_model`, `tree_inspector_controller`) still pass, 0 regressions.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_tree_view_builder.cpp tests/meson.build
git commit -m "feat: add ViewBuilder::tree() with opt-in focusable VirtualList container"
```

---

### Task 6: Keyboard navigation wiring (Up/Down/Left/Right/Enter + auto-scroll)

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (extend `ViewBuilder::tree()` from Task 5)
- Modify: `tests/test_tree_view_builder.cpp`

**Interfaces:**
- Consumes: `TreeController::on_key()` (Task 4), `WidgetTree::scroll_row_into_view(WidgetId, size_t, Height)` (existing, already used by the debug tree inspector), `WidgetTree::set_focused`/`focus_order()`/`dispatch()` (existing).
- Produces: container-level `KeyPress` handling inside the same `ViewBuilder::tree()` method.

- [ ] **Step 1: Write the failing test**

```cpp
// append to tests/test_tree_view_builder.cpp

TEST_CASE("Down arrow on the focused tree container moves selection") {
    TreeModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(tree.focus_order().size() == 1);
    auto container_id = tree.focus_order()[0];
    tree.set_focused(container_id);

    model.ctrl.selected.set(prism::TreeNodeId{1});
    model.ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand root -> rows = [1, 2, 3]
    tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();

    tree.dispatch(container_id, prism::KeyPress{prism::keys::down, 0});
    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("keyboard navigation scrolls the newly selected row into view") {
    // A tree with enough rows that the last one starts off-screen in a short viewport --
    // same shape of test as test_tree_inspector_controller.cpp's auto-scroll case.
    struct WideFixture {
        size_t root_count() const { return 1; }
        prism::TreeNodeId root_at(size_t) const { return 100; }
        size_t child_count(prism::TreeNodeId id) const { return id == 100 ? 20 : 0; }
        prism::TreeNodeId child_at(prism::TreeNodeId, size_t i) const { return 200 + i; }
        std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
        bool has_children(prism::TreeNodeId id) const { return id == 100; }
    };
    struct WideModel {
        prism::TreeController ctrl;
        WideModel() : ctrl(prism::wrap_tree_storage(fixture_)) {}
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
    private:
        WideFixture fixture_;
    };

    WideModel model;
    prism::WidgetTree tree(model);
    model.ctrl.selected.set(prism::TreeNodeId{100});
    model.ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand -> 21 rows
    tree.build_snapshot(300, 80, 1); // short viewport, only a handful of rows fit
    tree.clear_dirty();

    REQUIRE(tree.focus_order().size() == 1);
    auto container_id = tree.focus_order()[0];
    tree.set_focused(container_id);

    // Move down 19 times to reach the last child (n219), which should start off-screen.
    for (int i = 0; i < 19; ++i)
        tree.dispatch(container_id, prism::KeyPress{prism::keys::down, 0});

    CHECK(model.ctrl.selected.get() == std::optional<prism::TreeNodeId>{219});

    auto snap = tree.build_snapshot(300, 80, 2);
    bool found = false;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd))
                if (t->text.find("n219") != std::string_view::npos) found = true;
    CHECK(found); // scroll_row_into_view should have brought it into the materialized range
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree_view_builder && ./builddir/tests/test_tree_view_builder`
Expected: FAIL — the container's `on_input` doesn't yet route `KeyPress` to `ctrl.on_key()`, so selection never moves and the second test's row never scrolls into view.

- [ ] **Step 3: Write minimal implementation**

In `ViewBuilder::tree()` (Task 5's method), change the `container.build_widget` assignment to also wire keyboard input via `wn.wire`. `container.id` is already assigned (by `container.id = tree_.next_id_++;`, earlier in the same method) at the point this lambda is *created*, so it can be captured by value directly — no indirection needed even though the lambda itself only *runs* later:

```cpp
                container.build_widget = [&ctrl, container_id = container.id, &tree = tree_](WidgetNode& wn) {
                    wn.focus_policy = FocusPolicy::tab_and_click;
                    wn.wire = [&ctrl, container_id, &tree](WidgetNode& self) {
                        self.connections.push_back(
                            self.on_input.connect([&ctrl, container_id, &tree](const InputEvent& ev) {
                                auto idx = ctrl.on_key(ev);
                                if (idx)
                                    tree.scroll_row_into_view(container_id, *idx, Widget<TreeRow>::row_h);
                            })
                        );
                    };
                };
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree_view_builder && ./builddir/tests/test_tree_view_builder`
Expected: PASS. If the second test's exact "19 Down presses reaches n219" assumption doesn't hold (off-by-one from root-row inclusion, etc.), adjust the loop count empirically against a debug dump of `model.ctrl.selected.get()` after each press rather than guessing — same discipline as Task 5's Step 4 note.

Then re-run the full suite:

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_tree_view_builder.cpp
git commit -m "feat: wire keyboard navigation and auto-scroll into ViewBuilder::tree()"
```

---

### Task 7: Tier-3 reflection adapter — `wrap_struct_tree()`

**Files:**
- Modify: `include/prism/ui/tree.hpp` (reflection-gated section, `#if __cpp_impl_reflection`)
- Create: `tests/test_tree_reflect.cpp`
- Modify: `tests/meson.build` (register `'tree_reflect' : files('test_tree_reflect.cpp'),`)

**Interfaces:**
- Consumes: `TreeSource`, `TreeNodeId` (Task 1); P2996 reflection (`std::meta::nonstatic_data_members_of`, `std::meta::identifier_of`, `template for`) — same idiom as `wrap_row_storage` in `include/prism/ui/table.hpp`.
- Produces: `template <typename T> TreeSource wrap_struct_tree(T& root)` (reflection-gated).

Per the spec's classification rules: a member that is a raw pointer / `std::optional<X>` / smart-pointer to a reflectable class `X` is a child slot (non-null descends, null → not shown); a directly-nested class/struct member is always a child slot; everything else (int, string, enum, bool, ...) becomes an `attributes()` entry, not a tree row. `TreeNodeId` is the pointer/object address reinterpreted as `uint64_t`. No cycle detection (documented non-goal). No container-of-children members (documented v1 boundary — `std::vector<Child>` members are simply skipped, neither a child slot nor an attribute).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_tree_reflect.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

#if __cpp_impl_reflection

namespace {
struct BinaryNode {
    int value = 0;
    BinaryNode* A = nullptr;
    BinaryNode* B = nullptr;
};
}

TEST_CASE("wrap_struct_tree exposes a root and descends into non-null pointer members") {
    BinaryNode leaf_a;
    leaf_a.value = 10;
    BinaryNode root;
    root.value = 1;
    root.A = &leaf_a;
    root.B = nullptr;

    auto src = prism::wrap_struct_tree(root);
    REQUIRE(src.root_count() == 1);
    auto root_id = src.root_at(0);
    CHECK(root_id == reinterpret_cast<prism::TreeNodeId>(&root));

    CHECK(src.has_children(root_id) == true);
    REQUIRE(src.child_count(root_id) == 1); // only A -- B is null, not shown at all
    auto a_id = src.child_at(root_id, 0);
    CHECK(a_id == reinterpret_cast<prism::TreeNodeId>(&leaf_a));
    CHECK(src.label(a_id) == "A"); // labeled by the member name that referenced it
    CHECK(src.has_children(a_id) == false); // leaf_a's own A/B are both null
}

TEST_CASE("wrap_struct_tree's root is labeled by its own type name") {
    BinaryNode root;
    auto src = prism::wrap_struct_tree(root);
    CHECK(src.label(src.root_at(0)) == "BinaryNode");
}

TEST_CASE("wrap_struct_tree collects primitive members as attributes, not child rows") {
    BinaryNode root;
    root.value = 42;
    auto src = prism::wrap_struct_tree(root);
    auto root_id = src.root_at(0);

    REQUIRE(src.attributes);
    auto attrs = src.attributes(root_id);
    bool found = false;
    for (auto& [k, v] : attrs)
        if (k == "value" && v == "42") found = true;
    CHECK(found);
    // `value` must not appear as a child row
    CHECK(src.child_count(root_id) == 0); // both A and B are null here
}

#endif // __cpp_impl_reflection
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C builddir test_tree_reflect`
Expected: FAIL to compile — `wrap_struct_tree` undefined (skip this whole task's build if the toolchain lacks `__cpp_impl_reflection`; on GCC 16 with `-freflection` it should be available, matching every other reflection-gated test in this repo).

- [ ] **Step 3: Write minimal implementation**

Add `#include <fmt/format.h>` and `#include <unordered_map>` to `include/prism/ui/tree.hpp`'s includes. Append, inside a new `#if __cpp_impl_reflection` block (mirroring `include/prism/ui/table.hpp`'s own reflection-gated section):

```cpp
#if __cpp_impl_reflection
#include <meta>

namespace detail_tree {

// Matches std::optional<X> where X is a reflectable class (excluding std::string, which is
// itself a class type but must be treated as a plain value, not something to recurse into).
template <typename T>
struct IsOptionalOfClass : std::false_type {};
template <typename X>
struct IsOptionalOfClass<std::optional<X>>
    : std::bool_constant<std::is_class_v<X> && !std::is_same_v<X, std::string>> {};

// Recursively classifies every member of X: a pointer/std::optional to a reflectable class is
// a child slot (descend only if non-null); a directly-nested reflectable class member is always
// a child slot; everything else (int, string, enum, bool, ...) becomes an attribute, never a
// tree row. std::vector<...> members (including vector-of-reflectable-class -- the documented
// v1 non-goal) match none of these branches and are silently skipped.
//
// `entry` is built up as a LOCAL value and inserted into `cache` only once, at the very end --
// never held as a reference across the recursive calls below. cache[id] / cache.emplace(...)
// can rehash std::unordered_map and invalidate references to every other element; taking a
// reference up front and using it after a nested populate_struct_tree_cache() call (which also
// inserts into the same cache) would be a dangling-reference bug.
template <typename X, typename Cache>
void populate_struct_tree_cache(X& obj, std::string_view label, Cache& cache) {
    TreeNodeId id = reinterpret_cast<TreeNodeId>(std::addressof(obj));
    if (cache.contains(id)) return; // already visited

    typename Cache::mapped_type entry;
    entry.label = std::string(label);

    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^X, std::meta::access_context::unchecked()));

    template for (constexpr auto m : members) {
        auto member_name = std::meta::identifier_of(m);
        auto& member = obj.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;

        if constexpr (std::is_pointer_v<M> && std::is_class_v<std::remove_pointer_t<M>>
                      && !std::is_same_v<std::remove_pointer_t<M>, std::string>) {
            if (member != nullptr) {
                entry.children.push_back(reinterpret_cast<TreeNodeId>(member));
                populate_struct_tree_cache(*member, member_name, cache);
            }
        } else if constexpr (IsOptionalOfClass<M>::value) {
            if (member.has_value()) {
                entry.children.push_back(reinterpret_cast<TreeNodeId>(std::addressof(*member)));
                populate_struct_tree_cache(*member, member_name, cache);
            }
        } else if constexpr (std::is_class_v<M> && !std::is_same_v<M, std::string>) {
            entry.children.push_back(reinterpret_cast<TreeNodeId>(std::addressof(member)));
            populate_struct_tree_cache(member, member_name, cache);
        } else if constexpr (std::is_arithmetic_v<M>) {
            entry.attributes.emplace_back(std::string(member_name), fmt::to_string(member));
        } else if constexpr (std::is_same_v<M, std::string>) {
            entry.attributes.emplace_back(std::string(member_name), member);
        }
    }

    cache.emplace(id, std::move(entry));
}

} // namespace detail_tree

template <typename T>
TreeSource wrap_struct_tree(T& root) {
    struct Entry {
        std::string label;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<TreeNodeId> children;
    };
    auto cache = std::make_shared<std::unordered_map<TreeNodeId, Entry>>();
    detail_tree::populate_struct_tree_cache(root, std::meta::identifier_of(^^T), *cache);

    TreeSource src;
    src.root_count = [] { return size_t{1}; };
    src.root_at = [&root](size_t) -> TreeNodeId {
        return reinterpret_cast<TreeNodeId>(std::addressof(root));
    };
    src.label = [cache](TreeNodeId id) { return cache->at(id).label; };
    src.has_children = [cache](TreeNodeId id) { return !cache->at(id).children.empty(); };
    src.child_count = [cache](TreeNodeId id) { return cache->at(id).children.size(); };
    src.child_at = [cache](TreeNodeId id, size_t i) { return cache->at(id).children[i]; };
    src.attributes = [cache](TreeNodeId id) { return cache->at(id).attributes; };
    return src;
}

#endif // __cpp_impl_reflection
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson compile -C builddir test_tree_reflect && ./builddir/tests/test_tree_reflect`
Expected: PASS.

Then run the full suite before committing:

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/tree.hpp tests/test_tree_reflect.cpp tests/meson.build
git commit -m "feat: add wrap_struct_tree() reflection adapter for recursive C++ structs"
```

---

### Task 8: Filesystem example (Tier 2) + example registration

**Files:**
- Create: `examples/model_tree_browser.cpp`
- Modify: `examples/meson.build` (add `executable('model_tree_browser', 'model_tree_browser.cpp', dependencies : [prism_dep])`, after the existing `model_plot` entry)

**Interfaces:**
- Consumes: `TreeStorage` concept, `wrap_tree_storage()`, `TreeController`, `ViewBuilder::tree()` (all prior tasks), `wrap_struct_tree()` (Task 7).

Examples in this repo are build-only targets (not run by `meson test`), matching `hello_rect`/`model_dashboard`/`model_plot` — verification here is "it builds," same bar as those.

- [ ] **Step 1: Write the example**

```cpp
// examples/model_tree_browser.cpp
#include <prism/app/model_app.hpp>
#include <prism/ui/tree.hpp>

#include <filesystem>
#include <functional>
#include <unordered_map>

namespace fs = std::filesystem;

// Tier 2 hand-written TreeSource adapter over std::filesystem: TreeNodeId is a hash of the
// path, children are enumerated lazily (only when a directory is expanded), matching the
// TreeStorage concept from include/prism/ui/tree.hpp.
class FileTreeSource {
public:
    explicit FileTreeSource(fs::path root) : root_(std::move(root)) {
        cache_path(root_);
    }

    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return path_id(root_); }

    size_t child_count(prism::TreeNodeId id) const { return children_of(id).size(); }
    prism::TreeNodeId child_at(prism::TreeNodeId id, size_t i) const {
        return path_id(children_of(id)[i]);
    }
    std::string label(prism::TreeNodeId id) const { return by_id_.at(id).filename().string(); }
    bool has_children(prism::TreeNodeId id) const {
        std::error_code ec;
        return fs::is_directory(by_id_.at(id), ec);
    }
    std::optional<std::string> icon(prism::TreeNodeId id) const {
        std::error_code ec;
        return fs::is_directory(by_id_.at(id), ec) ? std::optional<std::string>{""}  // Nerd Font folder
                                                    : std::optional<std::string>{""}; // Nerd Font file
    }

private:
    prism::TreeNodeId path_id(const fs::path& p) const {
        auto id = static_cast<prism::TreeNodeId>(std::filesystem::hash_value(p));
        by_id_.emplace(id, p);
        return id;
    }
    void cache_path(const fs::path& p) const { path_id(p); }

    std::vector<fs::path> children_of(prism::TreeNodeId id) const {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return {};
        std::vector<fs::path> out;
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(it->second, ec)) {
            out.push_back(entry.path());
            cache_path(entry.path());
        }
        return out;
    }

    fs::path root_;
    mutable std::unordered_map<prism::TreeNodeId, fs::path> by_id_;
};

struct BrowserModel {
    FileTreeSource source{fs::current_path()};
    prism::TreeController ctrl{prism::wrap_tree_storage(source)};

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};

int main() {
    BrowserModel model;
    return prism::model_app(model, {.title = "PRISM Tree Browser -- Filesystem"});
}
```

- [ ] **Step 2: Register and build**

Add to `examples/meson.build` (after the `model_plot` executable block):

```meson
executable('model_tree_browser', 'model_tree_browser.cpp',
  dependencies : [prism_dep],
)
```

Run: `meson compile -C builddir model_tree_browser`
Expected: builds without errors. (If `prism::model_app`'s exact call signature differs from the sketch above — check `examples/model_dashboard.cpp` or `examples/model_plot.cpp` for the real signature and match it — this is the one place in this plan where the exact `main()`/`model_app` call shape should be copied from an existing example rather than assumed.)

- [ ] **Step 3: Run the full suite one last time**

Run: `meson test -C builddir`
Expected: all tests pass, 0 failures, exit code 0. Read the actual pass/fail count from the output — don't infer success from a partial grep.

- [ ] **Step 4: Commit**

```bash
git add examples/model_tree_browser.cpp examples/meson.build
git commit -m "docs: add filesystem tree-browser example (Tier 2 TreeSource adapter)"
```

---

## Self-Review Notes

**Spec coverage:** every spec section has a task — data contract (Task 1), lazy flatten (Task 2), render delegates + detail panel (Task 3), controller/selection/click (Task 4), `ViewBuilder::tree()` + focus engine hook (Task 5), keyboard nav (Task 6), Tier-3 reflection (Task 7), Tier-2 filesystem example (Task 8). The spec's non-goals (multi-select, drag-and-drop, in-place edit, tooltips, cycle detection, container-of-children reflection, header/hscroll, dedicated `LayoutKind::Tree`) are deliberately not tasked — consistent with the spec.

**Type consistency check:** `TreeNodeId` (Task 1) used consistently through Tasks 2-8. `TreeRow` (Task 2) fields (`id`, `label`, `icon`, `depth`, `has_children`, `expanded`, `selected`) match what Task 3's `Widget<TreeRow>::record` reads and what Task 4's `TreeController` constructs via `visible_rows`. `TreeController`'s public surface (`rows`, `selected`, `detail`, `on_row_clicked`, `on_key`) as used in Task 5/6's `ViewBuilder::tree()` matches Task 4's definition exactly.

**Known empirical-verification checkpoints** (flagged inline at the relevant step, not glossed over): Task 5 Step 4 (which `snap->geometry` entry is the root row) and Task 6 Step 4 (exact Down-press count to reach a specific row) both depend on real traversal/geometry order that this plan's author could not execute against a live build. Both steps include an explicit instruction to dump real output and adjust rather than guess — this mirrors the project's own established discipline (see `[[feedback-verify-plan-test-geometry]]` in project memory) after two prior incidents where assumed geometry/traversal order produced silently-wrong test assertions.

**Task 7 caveat:** the reflection-gated `wrap_struct_tree()` implementation in this plan is the most speculative part of the plan (P2996 reflection code that could not be compiled ahead of time). The task's test-first steps still apply — if the exact `template for`/`nonstatic_data_members_of` mechanics don't compile as written, use `include/prism/ui/table.hpp`'s `wrap_row_storage` (already compiling in this codebase) as the working reference implementation to adapt from, rather than debugging the reflection syntax from first principles.
