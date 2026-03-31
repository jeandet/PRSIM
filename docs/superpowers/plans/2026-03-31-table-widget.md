# Table Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a read-only data table widget to PRISM with dual storage model (column-oriented + row-oriented), row selection, virtual row recycling, and horizontal scroll.

**Architecture:** New `LayoutKind::Table` with type-erased `TableSource` bridging two compile-time concepts (`ColumnStorage` / `RowStorage`). Reuses existing virtual list pool recycling and scroll infrastructure. Header row is fixed, body rows are virtualized.

**Tech Stack:** C++23/26, doctest, SDL3 (rendering), existing PRISM core (Field<T>, List<T>, DrawList, WidgetTree)

---

### Task 1: Add LayoutKind::Table and TableState

**Files:**
- Modify: `include/prism/core/delegate.hpp:36` (LayoutKind enum)
- Modify: `include/prism/core/delegate.hpp:215-223` (EditState variant)
- Create: `include/prism/core/table.hpp`
- Create: `tests/test_table.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write the failing test — TableState exists and LayoutKind::Table compiles**

In `tests/test_table.cpp`:

```cpp
#include <doctest/doctest.h>
#include <prism/core/table.hpp>
#include <prism/core/delegate.hpp>

TEST_CASE("LayoutKind::Table exists") {
    auto kind = prism::LayoutKind::Table;
    CHECK(kind == prism::LayoutKind::Table);
}

TEST_CASE("TableState default-constructs") {
    prism::TableState ts;
    CHECK(ts.column_count == 0);
    CHECK(ts.row_count() == 0);
    CHECK(ts.selected_row.get() == std::nullopt);
}
```

- [ ] **Step 2: Register test in build system**

In `tests/meson.build`, add `test_table.cpp` to the test executable list following the same pattern as existing tests (e.g., `test_virtual_list.cpp`).

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — `table.hpp` doesn't exist, `LayoutKind::Table` not defined.

- [ ] **Step 4: Add LayoutKind::Table**

In `include/prism/core/delegate.hpp`, change the LayoutKind enum at line 36:

```cpp
enum class LayoutKind : uint8_t {
    Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table
};
```

- [ ] **Step 5: Create table.hpp with TableSource and TableState**

Create `include/prism/core/table.hpp`:

```cpp
#pragma once

#include <prism/core/field.hpp>
#include <prism/core/types.hpp>
#include <prism/core/widget_node.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism {

// ── Type-erased table data source ──────────────────────────────

struct TableSource {
    std::function<size_t()> column_count;
    std::function<size_t()> row_count;
    std::function<std::string(size_t row, size_t col)> cell_text;
    std::function<std::string_view(size_t col)> header;
};

// ── Storage concepts ───────────────────────────────────────────

template <typename T>
concept ColumnStorage = requires(const T& t, size_t r, size_t c) {
    { t.column_count() } -> std::convertible_to<size_t>;
    { t.row_count() } -> std::convertible_to<size_t>;
    { t.cell_text(r, c) } -> std::convertible_to<std::string>;
    { t.header(c) } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept RowStorage = requires {
    requires is_list_v<std::remove_cvref_t<T>>;
};

// ── Table ephemeral state ──────────────────────────────────────

struct TableState {
    size_t column_count = 0;

    // Vertical scroll (body rows)
    DY scroll_y{0};
    Height viewport_h{0};
    Height row_height{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};

    // Horizontal scroll
    DX scroll_x{0};
    Width viewport_w{0};
    Width total_columns_w{0};

    // Row selection
    Field<std::optional<size_t>> selected_row{std::nullopt};

    // Data access (type-erased)
    TableSource source;

    // Row pool (reused from virtual list pattern)
    std::vector<WidgetNode> pool;

    // Header labels (override or from source)
    std::vector<std::string> header_overrides;

    size_t row_count() const {
        return source.row_count ? source.row_count() : 0;
    }

    std::string_view column_header(size_t col) const {
        if (col < header_overrides.size() && !header_overrides[col].empty())
            return header_overrides[col];
        return source.header ? source.header(col) : "";
    }
};

} // namespace prism
```

- [ ] **Step 6: Add TableState to EditState variant**

In `include/prism/core/delegate.hpp`, add to the EditState variant (around line 215):

```cpp
using EditState = std::variant<
    std::monostate,
    TextEditState,
    TextAreaEditState,
    DropdownEditState,
    ScrollState,
    std::shared_ptr<VirtualListState>,
    std::shared_ptr<TableState>,
    std::shared_ptr<void>
>;
```

- [ ] **Step 7: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS — both test cases green.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/table.hpp include/prism/core/delegate.hpp tests/test_table.cpp tests/meson.build
git commit -m "feat: add LayoutKind::Table, TableSource, TableState, and storage concepts"
```

---

### Task 2: ColumnStorage concept — type-erase into TableSource

**Files:**
- Modify: `tests/test_table.cpp`
- Modify: `include/prism/core/table.hpp`

- [ ] **Step 1: Write the failing test — ColumnStorage concept and wrap function**

Append to `tests/test_table.cpp`:

```cpp
struct TestColumns {
    std::vector<double> time = {0.0, 0.125, 0.25};
    std::vector<std::string> label = {"SW", "SW", "MS"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return time.size(); }
    std::string_view header(size_t col) const {
        static constexpr std::array<const char*, 2> names = {"Time", "Label"};
        return names[col];
    }
    std::string cell_text(size_t row, size_t col) const {
        if (col == 0) return std::to_string(time[row]);
        return label[row];
    }
};

static_assert(prism::ColumnStorage<TestColumns>);

TEST_CASE("wrap_column_storage produces valid TableSource") {
    TestColumns data;
    auto src = prism::wrap_column_storage(data);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 3);
    CHECK(src.header(0) == "Time");
    CHECK(src.header(1) == "Label");
    CHECK(src.cell_text(1, 1) == "SW");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — `wrap_column_storage` not defined.

- [ ] **Step 3: Implement wrap_column_storage**

Add to `include/prism/core/table.hpp`, inside namespace prism:

```cpp
template <ColumnStorage T>
TableSource wrap_column_storage(T& data) {
    return TableSource{
        .column_count = [&data] { return data.column_count(); },
        .row_count = [&data] { return data.row_count(); },
        .cell_text = [&data](size_t r, size_t c) -> std::string {
            return std::string(data.cell_text(r, c));
        },
        .header = [&data](size_t c) -> std::string_view {
            return data.header(c);
        },
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/table.hpp tests/test_table.cpp
git commit -m "feat: wrap_column_storage type-erases ColumnStorage into TableSource"
```

---

### Task 3: RowStorage concept — type-erase List<T> into TableSource

**Files:**
- Modify: `tests/test_table.cpp`
- Modify: `include/prism/core/table.hpp`

- [ ] **Step 1: Write the failing test — RowStorage concept and wrap function**

Append to `tests/test_table.cpp`:

```cpp
#include <prism/core/list.hpp>

struct TestRow {
    prism::Field<std::string> label{""};
    prism::Field<double> value{0.0};
};

TEST_CASE("wrap_row_storage produces valid TableSource") {
    prism::List<TestRow> rows;
    rows.push_back(TestRow{.label = {"Alpha"}, .value = {1.5}});
    rows.push_back(TestRow{.label = {"Beta"}, .value = {2.7}});

    auto src = prism::wrap_row_storage(rows);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 2);
    CHECK(src.header(0) == "label");
    CHECK(src.header(1) == "value");
    CHECK(src.cell_text(0, 0) == "Alpha");
    CHECK(src.cell_text(1, 1) == "2.700000");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — `wrap_row_storage` not defined.

- [ ] **Step 3: Implement wrap_row_storage**

This uses reflection (if available) or requires the row type to provide field access. Add to `include/prism/core/table.hpp`:

```cpp
#include <prism/core/list.hpp>
#include <prism/core/reflect.hpp>

namespace detail {

template <typename T>
std::string field_to_string(const Field<T>& f) {
    if constexpr (std::is_same_v<T, std::string>)
        return f.get();
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(f.get());
    else
        return "?";
}

} // namespace detail

template <typename T>
TableSource wrap_row_storage(List<T>& list) {
    return TableSource{
        .column_count = [] {
            size_t count = 0;
            for_each_field(T{}, [&](auto, auto&) { ++count; });
            return count;
        },
        .row_count = [&list] { return list.size(); },
        .cell_text = [&list](size_t r, size_t c) -> std::string {
            size_t idx = 0;
            std::string result;
            for_each_field(list[r], [&](auto, auto& field) {
                if (idx++ == c)
                    result = detail::field_to_string(field);
            });
            return result;
        },
        .header = [](size_t c) -> std::string_view {
            static std::vector<std::string> names = [] {
                std::vector<std::string> v;
                for_each_field(T{}, [&](std::string_view name, auto&) {
                    v.emplace_back(name);
                });
                return v;
            }();
            return names[c];
        },
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/table.hpp tests/test_table.cpp
git commit -m "feat: wrap_row_storage type-erases List<T> into TableSource via reflection"
```

---

### Task 4: ViewBuilder.table() overloads

**Files:**
- Modify: `tests/test_table.cpp`
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `include/prism/core/node.hpp`

- [ ] **Step 1: Write the failing test — vb.table() builds a table node**

Append to `tests/test_table.cpp`:

```cpp
#include <prism/core/widget_tree.hpp>

struct ColumnModel {
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<std::string> name = {"a", "b", "c"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 2> h = {"X", "Name"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        if (c == 0) return std::to_string(x[r]);
        return name[r];
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("ViewBuilder.table() with ColumnStorage creates Table node") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot();
    // Table node exists — at minimum we get geometry entries
    CHECK(snap.geometry.size() > 0);
}

struct RowModel {
    prism::List<TestRow> rows;

    RowModel() {
        rows.push_back(TestRow{.label = {"A"}, .value = {1.0}});
        rows.push_back(TestRow{.label = {"B"}, .value = {2.0}});
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(rows);
    }
};

TEST_CASE("ViewBuilder.table() with RowStorage creates Table node") {
    RowModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot();
    CHECK(snap.geometry.size() > 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — `vb.table()` not defined.

- [ ] **Step 3: Add table metadata fields to Node**

In `include/prism/core/node.hpp`, add alongside the existing `vlist_*` fields:

```cpp
// Table metadata
std::shared_ptr<TableState> table_state;
```

- [ ] **Step 4: Implement ViewBuilder.table() — ColumnStorage overload**

In `include/prism/core/widget_tree.hpp`, add to the ViewBuilder class (after the `list()` method):

```cpp
template <ColumnStorage T>
void table(T& data) {
    Node container;
    container.id = tree_.next_id_++;
    container.is_leaf = false;
    container.layout_kind = LayoutKind::Table;

    auto state = std::make_shared<TableState>();
    state->source = wrap_column_storage(data);
    state->column_count = data.column_count();
    container.table_state = state;

    current_parent().children.push_back(std::move(container));
}
```

- [ ] **Step 5: Implement ViewBuilder.table() — RowStorage overload**

In `include/prism/core/widget_tree.hpp`, add below the ColumnStorage overload:

```cpp
template <typename T>
    requires RowStorage<List<T>>
void table(List<T>& list) {
    Node container;
    container.id = tree_.next_id_++;
    container.is_leaf = false;
    container.layout_kind = LayoutKind::Table;

    auto state = std::make_shared<TableState>();
    state->source = wrap_row_storage(list);
    state->column_count = state->source.column_count();
    container.table_state = state;

    current_parent().children.push_back(std::move(container));
}
```

- [ ] **Step 6: Wire TableState into WidgetNode during build_widget_node**

In `include/prism/core/widget_tree.hpp`, find the `build_widget_node()` function where `LayoutKind::VirtualList` is handled. Add a parallel branch for `LayoutKind::Table`:

```cpp
if (src.layout_kind == LayoutKind::Table && src.table_state) {
    wn.edit_state = src.table_state;
    wn.layout_kind = LayoutKind::Table;
    wn.is_container = true;
    wn.focus_policy = FocusPolicy::tab_and_click;
}
```

- [ ] **Step 7: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS (or partial — layout may not yet produce geometry; adjust assertions if needed to just check no crash).

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/widget_tree.hpp include/prism/core/node.hpp tests/test_table.cpp
git commit -m "feat: ViewBuilder.table() overloads for ColumnStorage and RowStorage"
```

---

### Task 5: Table layout pass — measure, arrange, flatten

**Files:**
- Modify: `include/prism/core/layout.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — table layout produces correct geometry**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("Table layout: header + visible rows produce geometry") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot();

    // Should have: clip region + header draws + row draws + clip pop
    // At minimum, draw_lists should be non-empty
    CHECK(snap.draw_lists.size() > 0);

    // Check that table node has allocated rect
    bool found_table = false;
    for (auto& [id, rect] : snap.geometry) {
        if (rect.extent.w.raw() > 0 && rect.extent.h.raw() > 0)
            found_table = true;
    }
    CHECK(found_table);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — layout doesn't handle `LayoutKind::Table`.

- [ ] **Step 3: Add LayoutNode::Kind::Table**

In `include/prism/core/layout.hpp`, add to the Kind enum (around line 41):

```cpp
enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table } kind = Kind::Leaf;
```

Add table-specific fields to LayoutNode:

```cpp
// Table layout
DX table_scroll_x{0};
size_t table_column_count = 0;
float table_header_h = 0;
```

- [ ] **Step 4: Implement table materialization**

In `include/prism/core/widget_tree.hpp`, add `materialize_table()` alongside `materialize_virtual_list()`:

```cpp
void materialize_table(WidgetNode& node) {
    auto* sp = std::get_if<std::shared_ptr<TableState>>(&node.edit_state);
    if (!sp || !*sp) return;
    auto& ts = **sp;

    // Measure row height from a probe cell if not yet known
    if (ts.row_height.raw() <= 0.f) {
        ts.row_height = Height{24.f}; // font size 14 + padding
    }

    size_t total_rows = ts.row_count();
    auto [new_start, new_end] = compute_visible_range(
        ItemCount{total_rows}, ts.row_height, ts.scroll_y,
        ts.viewport_h, ts.overscan);

    // Unbind current children → pool
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        index_.erase(it->id);
        parent_map_.erase(it->id);
        std::erase(focus_order_, it->id);
        ts.pool.push_back(std::move(*it));
    }
    node.children.clear();

    // Bind visible rows
    size_t range_size = new_end.raw() - new_start.raw();
    node.children.reserve(range_size);
    for (size_t i = new_start.raw(); i < new_end.raw(); ++i) {
        WidgetNode wn;
        if (!ts.pool.empty()) {
            wn = std::move(ts.pool.back());
            ts.pool.pop_back();
        } else {
            wn.id = next_id_++;
        }
        wn.dirty = true;
        wn.draws.clear();

        // Record: draw all cells for this row
        size_t row_idx = i;
        bool selected = (ts.selected_row.get().has_value() &&
                         ts.selected_row.get().value() == row_idx);
        float col_w = ts.viewport_w.raw() > 0
            ? ts.viewport_w.raw() / static_cast<float>(ts.column_count)
            : 120.f;

        auto bg = (row_idx % 2 == 0)
            ? Color::rgba(30, 30, 50)
            : Color::rgba(26, 26, 46);
        if (selected)
            bg = Color::rgba(50, 50, 120);

        wn.draws.filled_rect(
            Rect{Point{X{0}, Y{0}},
                 Size{Width{col_w * ts.column_count}, ts.row_height}},
            bg);

        for (size_t c = 0; c < ts.column_count; ++c) {
            std::string txt = ts.source.cell_text(row_idx, c);
            float cx = static_cast<float>(c) * col_w;
            wn.draws.clip_push(
                Point{X{cx}, Y{0}},
                Size{Width{col_w}, ts.row_height});
            wn.draws.text(std::move(txt),
                Point{X{cx + 4.f}, Y{4.f}},
                14.f, Color::rgba(200, 200, 220));
            wn.draws.clip_pop();

            // Column separator
            if (c > 0) {
                wn.draws.filled_rect(
                    Rect{Point{X{cx}, Y{0}},
                         Size{Width{1.f}, ts.row_height}},
                    Color::rgba(50, 50, 70));
            }
        }

        parent_map_[wn.id] = node.id;
        node.children.push_back(std::move(wn));
    }

    for (auto& c : node.children)
        index_[c.id] = &c;

    ts.visible_start = new_start;
    ts.visible_end = new_end;
}
```

Call `materialize_table()` from `materialize_all_virtual_lists()` (or rename it to `materialize_all()`), adding a check for `LayoutKind::Table` alongside the existing `LayoutKind::VirtualList` check.

- [ ] **Step 5: Add Table layout measure/arrange**

In `include/prism/core/layout.hpp`, in `layout_measure()` add a case for `Kind::Table`:

```cpp
if (node.kind == LayoutNode::Kind::Table) {
    // Table wants to expand like a Spacer (fill available space)
    node.hint = SizeHint{.preferred = 200.f, .expand = true};
}
```

In `layout_arrange()` add a case for `Kind::Table`:

```cpp
if (node.kind == LayoutNode::Kind::Table) {
    node.allocated = available;
    float item_h = node.vlist_item_height;
    if (item_h <= 0.f) item_h = 24.f;
    float header_h = node.table_header_h > 0 ? node.table_header_h : item_h;
    float body_top = available.origin.y.raw() + header_h;
    float start_y = body_top
        + static_cast<float>(node.vlist_visible_start) * item_h;
    for (auto& child : node.children) {
        layout_arrange(child, {
            Point{available.origin.x, Y{start_y}},
            Size{available.extent.w, Height{item_h}}
        });
        start_y += item_h;
    }
    return;
}
```

- [ ] **Step 6: Add Table layout flatten — header draws, body clip, scrollbars**

In `include/prism/core/layout.hpp`, in `layout_flatten()`, add a case for `Kind::Table`. This handles:
1. Draw the header row (fixed, not clipped by vertical scroll)
2. Clip push for body region
3. Flatten visible row children with scroll offset
4. Clip pop
5. Vertical scrollbar overlay
6. Horizontal scrollbar overlay (if content wider than viewport)

```cpp
if (node.kind == LayoutNode::Kind::Table) {
    auto vp = node.allocated;
    float item_h = node.vlist_item_height > 0 ? node.vlist_item_height : 24.f;
    float header_h = node.table_header_h > 0 ? node.table_header_h : item_h;
    size_t col_count = node.table_column_count;
    float col_w = col_count > 0
        ? vp.extent.w.raw() / static_cast<float>(col_count)
        : vp.extent.w.raw();
    float total_col_w = col_w * col_count;

    snap.geometry.push_back({node.id, vp});

    // ── Header row (fixed at top) ──
    DrawList header_dl;
    header_dl.clip_push(vp.origin, Size{vp.extent.w, Height{header_h}});
    header_dl.filled_rect(
        Rect{vp.origin, Size{Width{total_col_w}, Height{header_h}}},
        Color::rgba(42, 42, 74));
    // Header separator
    header_dl.filled_rect(
        Rect{Point{vp.origin.x, vp.origin.y + DY{header_h - 2.f}},
             Size{Width{total_col_w}, Height{2.f}}},
        Color::rgba(74, 74, 106));

    // Header cell text is drawn by the caller (WidgetTree) via TableState
    // For now, we need to access the TableState — store header draws in overlay
    header_dl.clip_pop();
    snap.draw_lists.push_back(std::move(header_dl));
    snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

    // ── Body region (clipped, scrollable) ──
    Rect body_rect{
        Point{vp.origin.x, vp.origin.y + DY{header_h}},
        Size{vp.extent.w, Height{vp.extent.h.raw() - header_h}}};

    DrawList body_clip;
    body_clip.clip_push(body_rect.origin, body_rect.extent);
    snap.geometry.push_back({0, body_rect});
    snap.draw_lists.push_back(std::move(body_clip));
    snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

    // Flatten visible children with vertical scroll offset
    DY scroll_dy = node.scroll_offset;
    DY neg_scroll{-scroll_dy.raw()};
    DY header_offset{header_h};
    for (auto& child : node.children) {
        detail::offset_subtree_y(child, neg_scroll + header_offset);
        layout_flatten(child, snap);
        detail::offset_subtree_y(child, DY{scroll_dy.raw() - header_h});
    }

    DrawList body_clip_pop;
    body_clip_pop.clip_pop();
    snap.geometry.push_back({0, Rect{}});
    snap.draw_lists.push_back(std::move(body_clip_pop));
    snap.z_order.push_back(static_cast<uint16_t>(snap.geometry.size() - 1));

    // ── Vertical scrollbar ──
    size_t total_rows = node.scroll_content_h.raw() > 0
        ? static_cast<size_t>(node.scroll_content_h.raw() / item_h) : 0;
    if (node.scroll_content_h.raw() > body_rect.extent.h.raw()) {
        Height thumb_h = scrollbar::thumb_height(body_rect.extent.h, node.scroll_content_h);
        float max_scroll = node.scroll_content_h.raw() - body_rect.extent.h.raw();
        float thumb_y = max_scroll > 0
            ? scroll_dy.raw() * (body_rect.extent.h.raw() - thumb_h.raw()) / max_scroll
            : 0.f;
        snap.overlay.filled_rect(
            Rect{Point{body_rect.origin.x + DX{body_rect.extent.w.raw() - scrollbar::track_inset.raw()},
                       body_rect.origin.y + DY{thumb_y}},
                 Size{Width{scrollbar::track_width}, thumb_h}},
            Color::rgba(120, 120, 130, 160));
    }

    return;
}
```

- [ ] **Step 7: Wire layout node creation for Table**

In `include/prism/core/widget_tree.hpp`, in `to_layout_node()` (or equivalent), add the Table case alongside VirtualList:

```cpp
if (wn.layout_kind == LayoutKind::Table) {
    ln.kind = LayoutNode::Kind::Table;
    if (auto* sp = std::get_if<std::shared_ptr<TableState>>(&wn.edit_state); sp && *sp) {
        auto& ts = **sp;
        ln.vlist_item_height = ts.row_height.raw();
        ln.vlist_visible_start = ts.visible_start.raw();
        ln.scroll_offset = ts.scroll_y;
        ln.scroll_content_h = Height{ts.row_height.raw() * ts.row_count()};
        ln.table_column_count = ts.column_count;
        ln.table_header_h = ts.row_height.raw(); // header same height as rows
        ln.table_scroll_x = ts.scroll_x;
    }
}
```

- [ ] **Step 8: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add include/prism/core/layout.hpp include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: table layout pass — measure, arrange, flatten with header and body clip"
```

---

### Task 6: Header rendering via TableState

**Files:**
- Modify: `include/prism/core/layout.hpp` (or widget_tree.hpp)
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — header text appears in snapshot draw commands**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("Table header text appears in draw commands") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot();

    bool found_header_text = false;
    for (auto& dl : snap.draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "X" || tc->text == "Name")
                    found_header_text = true;
            }
        }
    }
    CHECK(found_header_text);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — header text not rendered.

- [ ] **Step 3: Render header cell text during flatten**

The challenge is that `layout_flatten` doesn't have access to `TableState` (it works on `LayoutNode`). Two options:

**Option A:** Pre-render header draws into the WidgetNode's `draws` or `overlay_draws` during materialization, then flatten picks them up.

Add header rendering in `materialize_table()`, storing draws on the WidgetNode itself:

```cpp
// At the end of materialize_table(), render header into node.overlay_draws
node.overlay_draws.clear();
float col_w = ts.viewport_w.raw() > 0
    ? ts.viewport_w.raw() / static_cast<float>(ts.column_count)
    : 120.f;

for (size_t c = 0; c < ts.column_count; ++c) {
    float cx = static_cast<float>(c) * col_w;
    auto hdr = ts.column_header(c);
    node.overlay_draws.text(std::string(hdr),
        Point{X{cx + 4.f}, Y{4.f}},
        14.f, Color::rgba(136, 136, 204));
}
```

Then in `layout_flatten` for Table, after drawing the header background, merge `node.overlay_draws` into the header DrawList (applying the header clip region and table origin offset).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/layout.hpp include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: render table header text via TableState during materialization"
```

---

### Task 7: Row selection — click and keyboard

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — click selects a row**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("Click on table row sets selected_row") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot(); // first frame
    tree.build_snapshot(); // stabilize

    // Find the table node
    auto table_id = tree.root().children[0].id; // assumes table is first child
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(
        &tree.root().children[0].edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Simulate click on row 1 area
    float row_h = ts.row_height.raw();
    float header_h = row_h; // header same height as row
    float click_y = header_h + row_h * 1.5f; // middle of row 1

    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch_input(table_id, prism::InputEvent{click});

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 1);
}

TEST_CASE("Click selected row deselects") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot();
    tree.build_snapshot();

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(
        &tree.root().children[0].edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    ts.selected_row.set(std::optional<size_t>{1});

    float row_h = ts.row_height.raw();
    float click_y = row_h + row_h * 1.5f;
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch_input(tree.root().children[0].id, prism::InputEvent{click});

    CHECK(ts.selected_row.get() == std::nullopt);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — no input handler for table.

- [ ] **Step 3: Implement table input handling**

In `include/prism/core/widget_tree.hpp`, wire the table node's `on_input` during `materialize_table()` or `build_widget_node()`:

```cpp
// In materialize_table(), after binding rows:
node.connections.clear();
node.connections.push_back(
    node.on_input.connect([&ts, &node](const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
            float header_h = ts.row_height.raw();
            float local_y = mb->position.y.raw();
            if (local_y < header_h) return; // click on header — ignore for now
            float body_y = local_y - header_h + ts.scroll_y.raw();
            size_t row = static_cast<size_t>(body_y / ts.row_height.raw());
            if (row < ts.row_count()) {
                auto current = ts.selected_row.get();
                if (current.has_value() && current.value() == row)
                    ts.selected_row.set(std::nullopt);
                else
                    ts.selected_row.set(row);
                node.dirty = true;
            }
        }
        if (auto* kp = std::get_if<KeyPress>(&ev)) {
            auto current = ts.selected_row.get();
            if (kp->key == keys::arrow_down) {
                size_t next = current.has_value() ? current.value() + 1 : 0;
                if (next < ts.row_count()) {
                    ts.selected_row.set(next);
                    node.dirty = true;
                }
            } else if (kp->key == keys::arrow_up) {
                if (current.has_value() && current.value() > 0) {
                    ts.selected_row.set(current.value() - 1);
                    node.dirty = true;
                }
            }
        }
    })
);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: table row selection via click and arrow keys"
```

---

### Task 8: Vertical scroll

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — mouse wheel scrolls table**

Append to `tests/test_table.cpp`:

```cpp
struct LargeColumnModel {
    std::vector<double> values;

    LargeColumnModel() : values(100) {
        for (size_t i = 0; i < 100; ++i) values[i] = static_cast<double>(i);
    }

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Value"; }
    std::string cell_text(size_t r, size_t) const { return std::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("Mouse wheel scrolls table vertically") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot();
    tree.build_snapshot();

    auto& table_node = tree.root().children[0];
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node.edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    CHECK(ts.scroll_y.raw() == 0.f);

    prism::MouseWheel wheel{.dy = 3.f};
    tree.dispatch_input(table_node.id, prism::InputEvent{wheel});

    CHECK(ts.scroll_y.raw() > 0.f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — no scroll handler.

- [ ] **Step 3: Add scroll handling to table input handler**

In the table input handler (from Task 7), add wheel handling:

```cpp
if (auto* mw = std::get_if<MouseWheel>(&ev)) {
    float scroll_speed = ts.row_height.raw() * 3.f;
    float max_scroll = std::max(0.f,
        ts.row_height.raw() * ts.row_count() - ts.viewport_h.raw());

    if (mw->shift_held) {
        // Horizontal scroll
        float dx = mw->dy * 40.f;
        float max_hscroll = std::max(0.f,
            ts.total_columns_w.raw() - ts.viewport_w.raw());
        ts.scroll_x = DX{std::clamp(ts.scroll_x.raw() + dx, 0.f, max_hscroll)};
    } else {
        // Vertical scroll
        float new_y = std::clamp(
            ts.scroll_y.raw() + mw->dy * scroll_speed,
            0.f, max_scroll);
        ts.scroll_y = DY{new_y};
    }
    node.dirty = true;
}

if (auto* kp = std::get_if<KeyPress>(&ev)) {
    float max_scroll = std::max(0.f,
        ts.row_height.raw() * ts.row_count() - ts.viewport_h.raw());

    if (kp->key == keys::page_down) {
        ts.scroll_y = DY{std::clamp(
            ts.scroll_y.raw() + ts.viewport_h.raw(), 0.f, max_scroll)};
        node.dirty = true;
    } else if (kp->key == keys::page_up) {
        ts.scroll_y = DY{std::clamp(
            ts.scroll_y.raw() - ts.viewport_h.raw(), 0.f, max_scroll)};
        node.dirty = true;
    }
    // ... existing arrow key handling ...
}
```

- [ ] **Step 4: Update viewport dimensions during layout**

In the layout arrange pass for Table, after allocating bounds, write back the viewport dimensions to the TableState. This requires passing the WidgetNode or TableState through to layout. Add to `materialize_table()` at the start:

```cpp
// Update viewport from canvas_bounds (set by previous frame's layout)
if (node.canvas_bounds.extent.w.raw() > 0) {
    ts.viewport_w = node.canvas_bounds.extent.w;
    ts.viewport_h = Height{node.canvas_bounds.extent.h.raw() - ts.row_height.raw()};
    ts.total_columns_w = Width{
        (ts.viewport_w.raw() / static_cast<float>(ts.column_count))
        * static_cast<float>(ts.column_count)};
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: table vertical and horizontal scroll via mouse wheel"
```

---

### Task 9: Selection scrolls into view + arrow key scroll follow

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — arrow down past viewport scrolls**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("Arrow key selection scrolls into view") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot();
    tree.build_snapshot();

    auto& table_node = tree.root().children[0];
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node.edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Select last visible row, then press down to go past viewport
    size_t visible_rows = static_cast<size_t>(ts.viewport_h.raw() / ts.row_height.raw());
    ts.selected_row.set(visible_rows - 1);

    prism::KeyPress down{.key = prism::keys::arrow_down};
    tree.dispatch_input(table_node.id, prism::InputEvent{down});

    CHECK(ts.selected_row.get().value() == visible_rows);
    CHECK(ts.scroll_y.raw() > 0.f); // scrolled to keep selection in view
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — scroll doesn't follow selection.

- [ ] **Step 3: Add scroll-into-view logic after selection change**

In the arrow key handler, after changing `selected_row`, add:

```cpp
// Scroll into view
if (auto sel = ts.selected_row.get(); sel.has_value()) {
    float row_top = static_cast<float>(sel.value()) * ts.row_height.raw();
    float row_bottom = row_top + ts.row_height.raw();
    float vp_top = ts.scroll_y.raw();
    float vp_bottom = vp_top + ts.viewport_h.raw();

    if (row_bottom > vp_bottom) {
        ts.scroll_y = DY{row_bottom - ts.viewport_h.raw()};
    } else if (row_top < vp_top) {
        ts.scroll_y = DY{row_top};
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: table selection auto-scrolls into view"
```

---

### Task 10: Dashboard example with table

**Files:**
- Modify: `examples/model_dashboard.cpp`

- [ ] **Step 1: Add a table to the existing dashboard**

Read the current `examples/model_dashboard.cpp` and add a table section. Add a column-oriented data struct and wire it into the dashboard's `view()`:

```cpp
struct SensorData {
    std::vector<double> time = {0.0, 0.125, 0.25, 0.375, 0.5};
    std::vector<double> density = {1.23e4, 1.19e4, 1.15e4, 9.87e3, 8.42e3};
    std::vector<std::string> region = {"Solar Wind", "Solar Wind", "Magneto", "Magneto", "Bow Shock"};

    size_t column_count() const { return 3; }
    size_t row_count() const { return time.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 3> h = {"Time", "Density", "Region"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        switch (c) {
            case 0: return std::to_string(time[r]);
            case 1: return std::to_string(density[r]);
            case 2: return region[r];
            default: return "";
        }
    }
};
```

Add `SensorData sensor_data;` as a member of the Dashboard struct, and add `vb.table(sensor_data);` in the `view()` method.

- [ ] **Step 2: Build and run**

Run: `meson compile -C builddir && ./builddir/examples/model_dashboard`
Expected: Table visible in dashboard with headers and scrollable rows.

- [ ] **Step 3: Commit**

```bash
git add examples/model_dashboard.cpp
git commit -m "feat: add sensor data table to dashboard example"
```

---

### Task 11: List<T> signal wiring for RowStorage tables

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — row insertion updates table**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("RowStorage table updates on List insert") {
    RowModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot();
    tree.build_snapshot();

    auto& table_node = tree.root().children[0];
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node.edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    size_t initial_rows = ts.row_count();
    model.rows.push_back(TestRow{.label = {"C"}, .value = {3.0}});

    // Table should be dirty after insert
    CHECK(table_node.dirty);
    CHECK(ts.row_count() == initial_rows + 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — no signal wiring for row storage tables.

- [ ] **Step 3: Wire List<T> signals in ViewBuilder.table() RowStorage overload**

In the `ViewBuilder::table(List<T>&)` method, add signal wiring on the Node (similar to `list()`):

```cpp
container.vlist_on_insert = [&list](size_t, std::function<void()> cb) -> Connection {
    return list.on_insert().connect(
        [cb = std::move(cb)](size_t, const auto&) { cb(); });
};
container.vlist_on_remove = [&list](size_t, std::function<void()> cb) -> Connection {
    return list.on_remove().connect(
        [cb = std::move(cb)](size_t) { cb(); });
};
container.vlist_on_update = [&list](size_t, std::function<void()> cb) -> Connection {
    return list.on_update().connect(
        [cb = std::move(cb)](size_t, const auto&) { cb(); });
};
```

Then in `build_widget_node` or `connect_dirty`, wire these signals to mark the table node dirty (same pattern as virtual list).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: wire List<T> signals for RowStorage tables"
```

---

### Task 12: depends_on for ColumnStorage tables

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the failing test — depends_on triggers re-render**

Append to `tests/test_table.cpp`:

```cpp
struct DynamicColumnModel {
    std::vector<double> values = {1.0, 2.0};
    prism::Field<bool> data_updated{false};

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Val"; }
    std::string cell_text(size_t r, size_t) const { return std::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).depends_on(data_updated);
    }
};

TEST_CASE("ColumnStorage table re-renders on depends_on trigger") {
    DynamicColumnModel model;
    prism::WidgetTree tree(model);
    tree.build_snapshot();
    tree.build_snapshot();

    auto& table_node = tree.root().children[0];
    model.values.push_back(3.0);
    model.data_updated.set(true);

    CHECK(table_node.dirty);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C builddir test_table -v`
Expected: FAIL — `table()` doesn't return a builder with `depends_on`.

- [ ] **Step 3: Return a TableBuilder from vb.table() with depends_on()**

Modify ViewBuilder to return a builder object:

```cpp
struct TableBuilder {
    Node& node;

    template <typename U>
    TableBuilder& depends_on(Field<U>& field) {
        node.dependencies.push_back([&field](std::function<void()> cb) -> Connection {
            return field.on_change().connect(
                [cb = std::move(cb)](const U&) { cb(); });
        });
        return *this;
    }

    TableBuilder& headers(std::vector<std::string> hdrs) {
        if (node.table_state)
            node.table_state->header_overrides = std::move(hdrs);
        return *this;
    }
};
```

Change `ViewBuilder::table()` return type from `void` to `TableBuilder`:

```cpp
template <ColumnStorage T>
TableBuilder table(T& data) {
    // ... existing code ...
    current_parent().children.push_back(std::move(container));
    return TableBuilder{current_parent().children.back()};
}
```

- [ ] **Step 4: Wire dependencies in connect_dirty for Table nodes**

In `connect_dirty()`, add handling for `LayoutKind::Table` nodes with dependencies (same pattern as Canvas):

```cpp
if (src.layout_kind == LayoutKind::Table) {
    for (auto& dep : src.dependencies) {
        wn.connections.push_back(dep([&wn] { wn.dirty = true; }));
    }
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/widget_tree.hpp tests/test_table.cpp
git commit -m "feat: depends_on and headers builder for table widget"
```

---

### Task 13: Header override test

**Files:**
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write the test — header override replaces reflection names**

Append to `tests/test_table.cpp`:

```cpp
struct OverrideModel {
    std::vector<double> x = {1.0};

    size_t column_count() const { return 1; }
    size_t row_count() const { return 1; }
    std::string_view header(size_t) const { return "X"; }
    std::string cell_text(size_t, size_t) const { return "1.0"; }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).headers({"Custom Header"});
    }
};

TEST_CASE("Header override replaces default") {
    OverrideModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot();
    snap = tree.build_snapshot();

    bool found_custom = false;
    for (auto& dl : snap.draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "Custom Header")
                    found_custom = true;
                CHECK(tc->text != "X"); // default should not appear
            }
        }
    }
    CHECK(found_custom);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson test -C builddir test_table -v`
Expected: PASS (if Task 6 and 12 are correctly implemented).

- [ ] **Step 3: Commit**

```bash
git add tests/test_table.cpp
git commit -m "test: verify header override in table widget"
```

---

### Task 14: Full integration test — end-to-end table workflow

**Files:**
- Modify: `tests/test_table.cpp`

- [ ] **Step 1: Write comprehensive integration test**

Append to `tests/test_table.cpp`:

```cpp
TEST_CASE("Full table workflow: render, scroll, select, observe") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);

    // Frame 1-2: stabilize
    tree.build_snapshot();
    auto snap = tree.build_snapshot();

    auto& table_node = tree.root().children[0];
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node.edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Verify initial state
    CHECK(ts.row_count() == 100);
    CHECK(ts.selected_row.get() == std::nullopt);
    CHECK(ts.scroll_y.raw() == 0.f);

    // Select row 5
    std::optional<size_t> observed_row;
    ts.selected_row.observe([&](const std::optional<size_t>& r) {
        observed_row = r;
    });

    ts.selected_row.set(std::optional<size_t>{5});
    CHECK(observed_row.has_value());
    CHECK(observed_row.value() == 5);

    // Scroll down
    prism::MouseWheel wheel{.dy = 5.f};
    tree.dispatch_input(table_node.id, prism::InputEvent{wheel});
    CHECK(ts.scroll_y.raw() > 0.f);

    // Re-render after scroll
    snap = tree.build_snapshot();
    CHECK(snap.draw_lists.size() > 0);
}
```

- [ ] **Step 2: Run all table tests**

Run: `meson test -C builddir test_table -v`
Expected: ALL PASS.

- [ ] **Step 3: Run full test suite**

Run: `meson test -C builddir -v`
Expected: ALL PASS — no regressions.

- [ ] **Step 4: Commit**

```bash
git add tests/test_table.cpp
git commit -m "test: full integration test for table widget workflow"
```
