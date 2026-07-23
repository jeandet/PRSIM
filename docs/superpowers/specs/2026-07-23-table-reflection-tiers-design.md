# Table Reflection Tiers + Shared List/Depends-On Ergonomics — Design

## Context

Building the system monitor example (`examples/model_system_monitor.cpp`,
`docs/superpowers/specs/2026-07-22-system-monitor-example-design.md`) surfaced
recurring boilerplate that isn't specific to that example — it's missing
framework primitives:

- `ProcessRow` (`model_system_monitor.cpp:20-25`) exists only to give
  `ProcessInfo` `Field<>`-wrapped members, because Table's row tier
  (`wrap_row_storage`, `table.hpp:74-114`) only accepts `Field`-wrapped struct
  members.
- `ingest_processes` (`model_system_monitor.cpp:70-85`) hand-diffs
  `table_rows` against a freshly sorted vector every poll, because `List<T>`
  has no bulk-replace operation (`core/list.hpp:10-45`: only `push_back`,
  `erase`, `set`).
- The three plot canvases each repeat an identical 5-call `.depends_on()`
  chain (`model_system_monitor.cpp:114-128`), because `depends_on` only takes
  one argument per call (`DependsOnMixin`, `widget_tree.hpp:67-78`).
- Column-major data has no reflection-based tier the way Tree already has one
  for structs (`wrap_struct_tree`, `tree.hpp:382-404`) — only a hand-written
  `ColumnStorage` adapter tier (`wrap_column_storage`, `table.hpp:39-51`).

This spec covers six pieces that close those gaps. All are additive except
the two explicitly-called-out refactors (Tree's leaf classification, and
Inspector's annotation location), both of which are behavior-preserving
extractions verified by re-running their existing test suites unchanged.

## Scope

**In scope:**
1. Shared reflection infrastructure: `reflect_annotations.hpp` +
   `reflect_leaf.hpp` (new, `include/prism/core/`).
2. Table plain-struct row tier (extends `wrap_row_storage`).
3. `List<T>::replace_all`.
4. Variadic `depends_on`.
5. SOA columnar tier, auto-detected (`SoaStorage` concept + third `table()`
   overload).
6. Migrating `examples/model_system_monitor.cpp` to use all of the above.

**Out of scope:**
- A generic flat-parent-id → tree helper (noted as a real gap while
  investigating `FlatProcessTreeSource`, but not part of this design —
  no concrete second use case yet).
- Any change to `TreeController`/`FlatProcessTreeSource`.
- Renaming `prism::inspector`'s public annotation names — they keep working
  unchanged via aliases (see Piece 1).
- Inline cell editing for either new Table tier (both are read/display-only,
  matching the existing Field-tier's actual usage today).

## Piece 1 — Shared reflection infrastructure

Two small headers extracted from existing code, both under
`include/prism/core/`, namespace `prism::reflect`:

### `reflect_annotations.hpp`

Relocated from `field_mirror.hpp:31-63` (currently in `prism::inspector`):
the `skip`/`readonly` tag values, `label_t<S>`/`section_t<S>` +
`label<S>`/`section<S>` variable templates, and the `has_annotation<M, Tag>`/
`extract_string_annotation<M, Templ>` consteval helpers. Nothing about their
implementation is Inspector-specific — they're generic "read a C++26
annotation off a reflected member" utilities that Table's new tiers also
need, and pulling in the annotation vocabulary shouldn't require depending on
`widgets/field_mirror.hpp` (which pulls in the full `FieldMirror<T>`/
`LeafSlot`/`HiddenSlot` machinery Table has no use for).

`field_mirror.hpp` keeps `prism::inspector::skip`/`readonly`/`label`/
`section` working unchanged via forwarding `using` declarations:

```cpp
namespace prism::inspector {
using prism::reflect::skip;
using prism::reflect::readonly;
template <fixed_string S> using label = prism::reflect::label<S>;
template <fixed_string S> using section = prism::reflect::section<S>;
}
```

No existing Inspector call site or test changes. Table's new code uses
`prism::reflect::skip` / `prism::reflect::label<"...">` directly — e.g.:

```cpp
struct ProcessInfo {
    int pid = 0;
    [[=prism::reflect::skip]] int ppid = 0;
    std::string name;
    [[=prism::reflect::skip]] char state = '?';
    [[=prism::reflect::label<"CPU %">]] float cpu_percent = 0.f;
    [[=prism::reflect::label<"Mem %">]] float mem_percent = 0.f;
    [[=prism::reflect::skip]] long rss_kb = 0;
    [[=prism::reflect::skip]] long total_jiffies = 0;
};
```

### `reflect_leaf.hpp`

A `LeafType<T>` concept plus `format_leaf_value(v)`, extracted from the
three `if constexpr` branches inline in `tree.hpp:366-373`:

```cpp
template <typename T>
concept LeafType = std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
                    || std::is_enum_v<T>;

template <LeafType T>
std::string format_leaf_value(const T& v) {
    if constexpr (std::is_same_v<T, std::string>) return v;
    else if constexpr (std::is_enum_v<T>) return fmt::to_string(std::to_underlying(v));
    else return fmt::to_string(v);
}
```

**Deliberately not reusing `prism::inspector::MirrorLeaf`** (`Numeric ||
StringLike || ScopedEnum`, `field_mirror.hpp:67-68`): `ScopedEnum` is
`std::is_scoped_enum_v`, which excludes plain `enum` — narrower than
`tree.hpp`'s existing `std::is_enum_v`, which accepts both. Reusing
`MirrorLeaf` verbatim would silently stop unscoped-enum members from
appearing as Tree attributes. `LeafType` is a new concept matching
`tree.hpp`'s current behavior exactly; `MirrorLeaf` is untouched and stays
Inspector-only. `tree.hpp:335-378` is refactored to call `LeafType`/
`format_leaf_value` instead of its inline branches — behavior-preserving,
verified by Tree's existing test suite passing unchanged.

Table's new row and SOA tiers (Pieces 2 and 5) use `LeafType`/
`format_leaf_value` the same way Tree does.

## Piece 2 — Table plain-struct row tier

`wrap_row_storage<L>` (`table.hpp:74-114`) gains an `if constexpr` branch on
whether `Row = L::value_type` has any `is_field_v` member:

- **Has `Field` members (today's behavior, unchanged):** existing
  Field-column path, unconditionally includes every `Field` member as a
  column — zero regression risk, no annotation support added to this path.
- **No `Field` members (new):** walk `Row`'s members via
  `nonstatic_data_members_of`; for each member `m`:
  - `has_annotation<m, decltype(prism::reflect::skip)>()` → excluded from
    columns.
  - Not `LeafType<M>` (i.e. class/vector/pointer/optional, mirroring Tree's
    own child-vs-attribute split) → excluded from columns, no annotation
    needed.
  - Otherwise → one column. Header defaults to
    `std::meta::identifier_of(m)`; `extract_string_annotation<m, label_t>()`
    overrides it if present. Cell text is `format_leaf_value(row.[:m:])`.

`.headers({...})` (`table.hpp` builder, unchanged) still overrides the whole
header list at once if the caller wants full control instead of per-field
`label` annotations.

Call site is unchanged either way — `tvb.table(table_rows)` — because the
branch is resolved inside `wrap_row_storage` on `Row`'s shape, not via a
different `ViewBuilder::table` overload.

## Piece 3 — `List<T>::replace_all`

Added to `core/list.hpp` next to `push_back`/`erase`/`set`:

```cpp
void replace_all(std::ranges::range auto&& new_values) {
    size_t n = std::ranges::size(new_values);
    while (size() > n) erase(size() - 1);
    size_t i = 0;
    for (auto&& v : new_values) {
        if (i < size()) set(i, std::forward<decltype(v)>(v));
        else push_back(std::forward<decltype(v)>(v));
        ++i;
    }
}
```

Index-preserving: overlapping indices fire `on_update` (via `set`), only the
size delta fires `on_insert`/`on_remove` — replicates
`model_system_monitor.cpp:73-81`'s existing manual loop exactly, just as a
named, reusable operation. `push_back`/`erase`/`set` are unchanged.

## Piece 4 — Variadic `depends_on`

`DependsOnMixin<Self>` (`widget_tree.hpp:67-78`) gains a fold-expression
overload, mirroring `ViewBuilder::vstack`/`hstack`'s existing
`(item(args), ...)` pattern (`widget_tree.hpp:140,143`):

```cpp
Self& depends_on(auto&... obs) {
    (depends_on(obs), ...);
    return static_cast<Self&>(*this);
}
```

Applies to both `CanvasHandle` and `TableBuilder` (both inherit
`DependsOnMixin`, `widget_tree.hpp:81-92`) — the SOA table tier's
`.depends_on(revision)` and any existing chained call site benefit
identically. The single-argument version is unchanged; the pack overload
expands into repeated calls to it.

## Piece 5 — SOA columnar tier (auto-detected)

A struct whose members are each a collection of leaves is recognized
automatically — no wrapper function name at the call site, extending the
same "shape decides the tier" principle as Piece 2:

```cpp
template <typename T>
concept SoaStorage = !is_list_v<T> && !ColumnStorage<T> && is_soa_struct_v<T>;
```

`is_soa_struct_v<T>` is a `consteval` helper using the same
`nonstatic_data_members_of` walk: true iff at least one non-`skip`-annotated
member `m` has a nested `value_type` and `LeafType<typename M::value_type>`
holds (i.e. `m` is a `vector`-like container of leaves — not a bare leaf
itself, not a nested class/pointer); other non-`skip`-annotated members are
silently excluded from the column set, matching the row tier's own
philosophy. `column_count()` counts only those qualifying columns;
`row_count()` is the first column's `size()` (mismatched column sizes across
members is a caller-side invariant violation, asserted in debug builds, not
a validated/thrown runtime error — this is internal state the caller
constructs, not a system boundary); `header(c)`/`cell_text(r, c)` reuse the
same annotation and formatting rules as Piece 2.

The three `ViewBuilder::table(...)` overloads are mutually exclusive by
construction (`ColumnStorage<T>` hand-written / `RowStorage<List<T>>` list-
of-structs / `SoaStorage<T>` struct-of-collections), so a given `T` matches
exactly one and the call site never changes shape-to-shape:

```cpp
prism::List<ProcessInfo> table_rows;      // AoS -> row tier (Piece 2)
tvb.table(table_rows);

struct ProcessColumns {
    std::vector<int> pid;
    std::vector<std::string> name;
    [[=prism::reflect::label<"CPU %">]] std::vector<float> cpu_percent;
    prism::Field<int> revision{0};        // caller bumps after refilling above
};
ProcessColumns cols;                       // SoA -> columnar tier (Piece 5), auto-detected
tvb.table(cols).depends_on(cols.revision);
```

The reflection walk that builds the column adapter (internally calls the
existing `wrap_column_storage`, `table.hpp:39-51`) is a free function in
`table.hpp` — independently unit-testable, not required to be called by
name at a real call site (same relationship `wrap_row_storage` already has
to the `List<T>` overload).

This tier is independent of Piece 2/3 — a caller picks row-major (`List<T>`)
or column-major (a struct of vectors) per use case; the system monitor
example does not use this tier (it has no column-major data), so it isn't
exercised by Piece 6, only by its own unit tests.

## Piece 6 — Example migration (`examples/model_system_monitor.cpp`)

- `ProcessRow` (:20-25) deleted.
- `table_rows` becomes `prism::List<ProcessInfo>` (was `List<ProcessRow>`).
- `ProcessInfo` (`proc_metrics.hpp:69-78`) annotated as shown in Piece 1.
- `ingest_processes` (:70-85) collapses to:
  ```cpp
  void ingest_processes(const std::vector<ProcessInfo>& processes) {
      table_rows.replace_all(sort_by(processes, sort_key.get()));
      tree_source.update(processes);
      tree_ctrl.refresh();
  }
  ```
- The three plot `.depends_on()` chains (:114-128) each collapse to one call:
  ```cpp
  vb.canvas(cpu_plot)
      .depends_on(cpu_plot.x_range, cpu_plot.y_range, cpu_plot.view,
                  cpu_plot.cursor, cpu_plot.revision)
      .min_size(prism::Height{120});
  ```
- `.headers({...})` call (:133) removed — auto-derived identifiers plus the
  two `label` annotations already produce `pid | name | CPU % | Mem %`.

## Testing

- `reflect_leaf.hpp`: `LeafType`/`format_leaf_value` unit tests (arithmetic,
  string, both scoped and unscoped enum).
- Tree regression: existing Tree test suite re-run unchanged after the
  `tree.hpp` refactor — no new tests required, behavior-preserving.
- Inspector regression: existing Inspector/FieldMirror test suite re-run
  unchanged after the `reflect_annotations.hpp` extraction.
- `wrap_row_storage` plain-struct path: column count/order, `skip`
  exclusion, `label` override, default-identifier header, non-`LeafType`
  member exclusion.
- `List<T>::replace_all`: signal-count assertions — shrink/grow/no-op-size
  cases fire the right mix of `on_update` vs `on_insert`/`on_remove`.
- `depends_on` variadic: multi-field dirty trigger fires the canvas/table
  redraw once per dependency change, same as chained single-arg calls.
- SOA tier: column count/order, `skip`/`label`, cell text correctness,
  `SoaStorage` concept correctly rejects `List<T>` and hand-written
  `ColumnStorage` types (mutual-exclusivity check).
- Example: manual rebuild + run to confirm visual/behavioral parity with
  today's output (table columns, sort, plot redraw, heartbeat).
