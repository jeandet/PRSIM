# Table Widget Design

## Overview

A read-only data table widget for PRISM, implemented as `LayoutKind::Table`. Supports two storage models — column-oriented (struct of vectors) and row-oriented (`List<RowStruct>`) — dispatched at compile time via concepts and unified through a lightweight type-erased `TableSource`. Features row selection, vertical + horizontal scroll, row recycling, and fixed headers.

## Storage Concepts

Two concepts define the data contract:

```cpp
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
```

**Column-oriented** — the natural shape for scientific data streams. Each column is a contiguous vector. The user implements `column_count()`, `row_count()`, `cell_text(row, col)`, and `header(col)` on their struct.

**Row-oriented** — `List<RowStruct>` where each row is a struct with `Field<T>` members. Column count and headers derived via reflection (or explicit `view()`). Leverages existing `List<T>` insert/remove/update signals.

## Type Erasure

Both storage models converge into a single runtime interface consumed by the layout engine:

```cpp
struct TableSource {
    std::function<size_t()> column_count;
    std::function<size_t()> row_count;
    std::function<std::string(size_t row, size_t col)> cell_text;
    std::function<std::string_view(size_t col)> header;
};
```

`TableSource` holds function pointers into the user's data — no copies. The ViewBuilder constructs it differently for each concept:

- `ColumnStorage`: wraps the user's methods directly.
- `RowStorage`: generates accessors via reflection that project each row's fields into columns.

## User-Facing API

### Column-oriented

```cpp
struct TelemetryTable {
    std::vector<double> time;
    std::vector<double> density;
    std::vector<std::string> labels;

    size_t column_count() const { return 3; }
    size_t row_count() const { return time.size(); }
    std::string_view header(size_t col) const {
        constexpr std::array names = {"Time", "Density", "Label"};
        return names[col];
    }
    std::string cell_text(size_t row, size_t col) const {
        switch (col) {
            case 0: return std::to_string(time[row]);
            case 1: return std::to_string(density[row]);
            case 2: return labels[row];
        }
    }
};

// In view():
vb.table(telemetry).depends_on(data_updated);
```

### Row-oriented

```cpp
struct Measurement {
    prism::Field<std::string> label{""};
    prism::Field<double> value{0.0};
    prism::Field<int> quality{0};
};

prism::List<Measurement> measurements;

// In view():
vb.table(measurements);
```

### Header override

Reflection provides default column headers from member names. Users can override:

```cpp
vb.table(measurements).headers({"Name", "Reading", "QF"});
```

## Layout Structure

`LayoutKind::Table` produces this node tree:

```
TableNode
├── HeaderRow          (fixed top, scrolls horizontally with body)
│   └── Cell[0..N]
├── ScrollRegion       (vertical + horizontal scroll)
│   └── Row[i]         (recycled from pool, only visible + overscan)
│       └── Cell[0..N]
└── HScrollbar         (visible only when content overflows horizontally)
```

### Layout rules

- **Row height**: uniform, computed once from font size + cell padding.
- **Column widths**: equal division of available width (v1). Future: user-specified, auto-fit.
- **Header row**: fixed at top of table bounds. Scrolls horizontally in sync with the body. Never scrolls vertically.
- **Vertical scroll**: reuses existing `ScrollState` and overlay scrollbar from scroll area infrastructure.
- **Horizontal scroll**: new `ScrollState` for the X axis, shared between header and body. Horizontal scrollbar rendered at the bottom when total column width exceeds viewport width.
- **Row recycling**: reuses the virtual list's pool mechanism. Only visible rows + configurable overscan are materialized as WidgetNodes.

### Rendering

Each cell renders as `DrawList::text()` within a clipped rect (one `clip_push`/`clip_pop` per cell to truncate overflow). Visual features:

- Alternating row backgrounds for readability.
- Selected row: highlight fill + left accent border.
- Header row: distinct background, bold text, bottom separator line.
- Cell separators: subtle vertical lines between columns.

## Row Selection

```cpp
// Owned by the table node, observable by the user
prism::Field<std::optional<size_t>> selected_row;
```

Accessible after `vb.table()` for cross-widget wiring:

```cpp
auto& tbl = vb.table(telemetry);
tbl.selected_row.observe([&](const std::optional<size_t>& idx) {
    if (idx) highlight_point_on_plot(*idx);
});
```

- Click row → select. Click selected row → deselect.
- Up/Down arrows → move selection (clamped to row count).
- Selection change auto-scrolls the selected row into view.
- Future: `Field<std::set<size_t>>` for multi-select.

## Input Handling

### Selection
- Mouse click on row body → set `selected_row`.
- Up/Down arrow keys → move selection when table is focused.

### Scrolling
- Mouse wheel over table body → vertical scroll.
- Shift + mouse wheel → horizontal scroll.
- PageUp/PageDown → jump by viewport height.

### Focus
- `FocusPolicy::tab_and_click` on the table node.
- Tab into table → restore previous selection or select first row.
- Arrow keys only active when table has focus.

### Not in v1
- No drag interaction.
- No cell-level interaction or text selection.
- No column resize/sort/reorder.

## Data Change Notification

### Row-oriented (`List<T>`)

Wires into existing `List<T>` signals — same as virtual list:

- `on_insert(index, value)` → insert row, update pool.
- `on_remove(index)` → remove row, return to pool.
- `on_update(index, value)` → re-render affected row if visible.

Selection is adjusted automatically: if a row before the selection is removed, the selection index shifts down.

### Column-oriented (`ColumnStorage`)

Uses `depends_on` — same mechanism as canvas:

```cpp
vb.table(telemetry).depends_on(data_updated);
```

When a dependency fires, the table re-queries `row_count()` and re-renders the visible range. The user signals data changes by mutating a `Field<T>`.

## Future Extension Points

These are explicitly out of scope for v1 but the design accommodates them:

- **Cell editing**: `TableSource` gains `set_cell_text(row, col, value)`. Cells become mini-widgets via delegate dispatch.
- **Column resize**: column widths move from equal-division to `std::vector<Width>`, drag handle on header borders.
- **Column sort**: click header → sort indicator, user provides comparator or default lexicographic.
- **Multi-select**: `Field<std::set<size_t>>` replaces `Field<std::optional<size_t>>`.
- **Frozen columns**: first N columns fixed during horizontal scroll.
- **Custom cell rendering**: per-column delegate override for colored cells, progress bars, etc.

## Files Affected

- `include/prism/core/table.hpp` — new: `TableSource`, concepts, table state
- `include/prism/core/table_delegate.hpp` — new: table rendering (header, rows, cells, scrollbars)
- `include/prism/core/widget_tree.hpp` — add `vb.table()` overloads
- `include/prism/core/widget_node.hpp` — add `TableSource` to node state, `LayoutKind::Table`
- `include/prism/core/layout.hpp` — table layout pass (column widths, row positioning, scroll regions)
- `tests/test_table.cpp` — new: concept checks, layout, selection, scroll, both storage models
- `examples/model_dashboard.cpp` — add table to existing dashboard demo
