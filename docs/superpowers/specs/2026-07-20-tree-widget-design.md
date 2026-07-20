# Tree Widget Design

## Overview

A generic, lazily-loaded tree widget for PRISM, covering three use cases with one engine: a
filesystem/directory browser, a JSON-like viewer, and "instantly visualize this recursive C++
struct" (the last one generalizes the other two, and is the flagship "instant UI from structs"
capability for this widget).

Built entirely on the existing `List<T>` / `LayoutKind::VirtualList` machinery — no new
`LayoutKind`, no changes to `layout.hpp` / `widget_node.hpp`. This directly generalizes the
already-shipped debug tree inspector (`prism::debug::flatten_tree` / `TreeInspectorModel` /
`TreeInspectorController`, see `include/prism/widgets/debug/tree_inspector.hpp`), which validated
this exact shape (flatten a hierarchy into `List<Row>`, render via `vb.list()`) in production. The
one thing that pattern doesn't do is *lazy* loading — it always flattens an already-fully-in-memory
`WidgetTree`. This widget's core addition is making the flatten step itself lazy, so a directory
with a million entries costs one `has_children()` call per collapsed folder, not a full recursive
walk.

**Rejected alternative:** a dedicated `LayoutKind::Tree` (mirroring how `Table` got its own layout
kind). `Table` needed that for a fixed header row synced to horizontal scroll and a cell grid — a
tree has neither; it's structurally just a vertically-scrolling list of variable-indent rows, which
`VirtualList` already does. A new `LayoutKind` would touch three core engine files for no
functional gain over what's actually been asked for.

## Data Contract

```cpp
using TreeNodeId = uint64_t;  // opaque handle, source assigns + interprets (mirrors WidgetId's own alias)

struct TreeSource {
    std::function<size_t()>                                        root_count;
    std::function<TreeNodeId(size_t index)>                        root_at;
    std::function<size_t(TreeNodeId)>                              child_count;
    std::function<TreeNodeId(TreeNodeId, size_t idx)>               child_at;
    std::function<std::string(TreeNodeId)>                         label;
    std::function<bool(TreeNodeId)>                                has_children;
    std::function<std::optional<std::string>(TreeNodeId)>          icon;        // optional
    std::function<std::vector<std::pair<std::string,std::string>>(TreeNodeId)> attributes; // optional
};
```

Same type-erasure shape as `TableSource`: a struct of `std::function`s that the engine's internal
code (layout/render/input, `TreeModel`, `TreeController`) is the only thing that ever touches
directly. `has_children` is deliberately separate from `child_count() > 0` — for a lazy source,
knowing a node is expandable must not require fetching its children first (that's exactly the
readdir cost being avoided). `icon` and `attributes` are optional (a source may leave them
unset / default-empty).

**Why a struct of `std::function`s instead of templating the widget on the source type or using a
virtual interface:** `WidgetNode` / `WidgetTree` need to store a tree widget generically alongside
every other widget kind in one non-templated node storage — type erasure has to happen somewhere.
A struct of `std::function`s lets construction stay generic/concept-gated (see the three tiers
below) while avoiding a heap-allocated polymorphic object with its own ownership story; each
function can just capture a reference into the caller's existing data by lambda, no allocation or
lifetime management beyond what `std::function` itself does. This mirrors `Table`'s own precedent
exactly: `wrap_column_storage` / `wrap_row_storage` are templated, concept-gated constructors that
both produce the same non-templated `TableSource`.

## Lazy Flattening

```cpp
std::vector<TreeRow> visible_rows(const TreeSource& source, const std::set<TreeNodeId>& expanded);
```

Walks only expanded branches: roots are always visible; a node's children are fetched via
`child_count`/`child_at` only if that node's id is in `expanded`. A collapsed node with a million
children costs exactly one `has_children()` call. `TreeRow` carries id, label, icon, depth,
`has_children`, `expanded`, plus the usual hover/selected flags — structurally the same shape as
the debug inspector's `NodeRow`.

## User-Facing API — Three Tiers

**Tier 1 — `TreeSource` directly.** Most callers never touch this, same as `TableSource` today.

**Tier 2 — hand-written adapter, no `std::function` in sight:**

```cpp
struct FileTreeSource {
    size_t root_count() const;
    TreeNodeId root_at(size_t i) const;
    size_t child_count(TreeNodeId id) const;
    TreeNodeId child_at(TreeNodeId id, size_t i) const;
    std::string label(TreeNodeId id) const;
    bool has_children(TreeNodeId id) const;
    std::optional<std::string> icon(TreeNodeId id) const;        // optional method
    std::vector<std::pair<std::string,std::string>> attributes(TreeNodeId id) const; // optional method
};

vb.tree(my_file_source);
```

A `TreeStorage` concept checks for the required methods (mirrors `ColumnStorage`); `vb.tree()`
calls `wrap_tree_storage()` internally to build the type-erased `TreeSource`. This is what the
shipped filesystem example looks like: `std::filesystem`-backed, `TreeNodeId` is a hash of the
path, `has_children` is `is_directory()`, children fetched via `directory_iterator` only when a
folder is expanded.

**Tier 3 — generic reflection over a recursive C++ struct**, zero methods written:

```cpp
struct MyNode {
    int value;
    MyNode* A;
    MyNode* B;
};

vb.tree(root_node);
```

Reflection walks every non-static data member (see "Why this deviates from `is_field_v`" below) and
classifies each by type:
- **Pointer / `std::optional<X>` / smart-pointer to a reflectable class `X`** → a child slot.
  Non-null descends into `X`; null → not shown at all (no phantom row).
- **Nested class/struct member directly** (not behind a pointer) → always present, always a child
  slot, recursed into.
- **Everything else** (int, string, enum, bool, ...) → not a tree row. Collected into that node's
  `attributes` list instead (see Detail Panel below), formatted the same way Table's
  `field_to_string` already does.

Row label for a child node = the member name that referenced it (`"A"`, `"B"`) — same idiom the
debug tree inspector already uses for field names (`WidgetTree::field_names_`). A root with no
referencing member is labeled with its own type's identifier (same idiom `build_node_tree` already
uses for the model root).

**Scope boundary:** this classification covers single-valued members only — a raw pointer,
`std::optional<X>`, smart-pointer to `X`, or a directly nested `X`. A **container of children**
(e.g. `std::vector<Child> children;`), the common shape for N-ary trees, is explicitly **out of
scope for v1** (see Non-Goals) — only the fixed-named-pointer/nested-member shape actually
discussed is supported. Extending the walk to containers-of-composites is a natural, additive
follow-up, not a redesign, but it isn't being built speculatively now.

**Why this deviates from the `is_field_v` / `Field<T>` gate used everywhere else in PRISM's
reflection-driven UI** (Table's row-oriented mode, `Inspector<T>`, dropdown/checkbox auto-wiring):
those features gate on `Field<T>` because they generate *reactive, editable* UI and need to know
what's observable. Tier 3 is **read-only structural visualization** of a struct the caller may not
even control (an externally-provided tree type) — it never needs to know what's editable, only
"what are this node's children," so the gate's rationale doesn't apply. This is a deliberate,
scoped exception, not an inconsistency: no other reflection-driven feature in PRISM is affected by
it, and it doesn't change what `is_field_v` means anywhere else.

**Documented non-goal, not guarded against:** this walk trusts the data forms a genuine tree. A
cyclic pointer structure (accidental or intentional) will recurse until it hits a real leaf or
blows the stack. No cycle detection in v1.

## Icons

`icon(id)` returns a glyph string from the bundled **Nerd Font** (JetBrains Mono Nerd Font, already
bundled specifically because it ships icon glyphs) — rendered via the existing `dl.text()` call, no
new rendering primitive or image/asset pipeline. A Tier 2 adapter returns whatever glyph fits its
own logic (directory vs. file, node type, file extension). **Tier 3's reflection path has no icons
in v1** — a later extension could reuse the existing `Inspector<T>` C++26 annotation mechanism
(`[[=prism::inspector::...]]` precedent) to tag icons declaratively on the reflected path, but that
new annotation surface isn't being built speculatively now.

## Selection & Detail Panel

Single selection only (`Field<std::optional<TreeNodeId>> selected`), matching how `Table` shipped
v1 (`Field<std::optional<size_t>> selected_row`, multi-select explicitly deferred there too).

A detail panel is always shown alongside the tree (mirrors the debug inspector's `hstack` layout:
tree list + side pane) — generalizes `Widget<std::optional<NodeRow>>`'s "dump the selected thing as
text lines" pattern from a fixed set of known fields to an open-ended `attributes` key/value list.
Shows "no selection" when nothing is picked. Tier 2 adapters that don't implement `attributes` just
show an empty list — no special-casing needed.

## Input Handling

- **Click a row** → select it and, if it has children, toggle expand/collapse in the same action —
  matches the debug tree inspector's existing `on_row_clicked` behavior. (Splitting "click the
  arrow" vs. "click the label" into separate behaviors would need sub-row hit-testing; not worth it
  for v1.)
- **Up/Down** → move `selected` to the previous/next currently-visible row, auto-scrolling via the
  already-shipped `WidgetTree::scroll_row_into_view`.
- **Right** → if selected node is collapsed and has children, expand it (selection stays). If
  already expanded, move selection to its first visible child.
- **Left** → if selected node is expanded, collapse it. If already collapsed (or a leaf), move
  selection to its parent.
- **Enter/Space** → same effect as clicking the selected row.
- **Focus:** `FocusPolicy::tab_and_click`, arrow keys active only while focused — same rule as
  `Table`.

## Non-Goals (v1)

- Multi-select.
- Drag-and-drop reordering/reparenting.
- In-place rename/edit.
- Tooltips — deferred. The overlay system (`SceneSnapshot::overlay`, already used for `Dropdown`
  popups) was explicitly designed to be reused for tooltips, so adding an optional
  `tooltip(TreeNodeId) -> optional<string>` accessor later is additive, not an architecture change.
- Cycle detection on Tier 3's pointer-following walk (documented risk above).
- Icon annotations on the Tier 3 reflected path.
- Container-of-children members (e.g. `std::vector<Child>`) on the Tier 3 reflected path — only
  single-valued pointer/optional/nested-struct members are walked in v1.
- Header row / horizontal scroll (not applicable — a tree isn't a grid).
- A dedicated `LayoutKind::Tree` (rejected approach — see Overview).

## Testing Strategy

- **The lazy-loading guarantee itself** (highest priority): a source whose `child_at`/`child_count`
  would fail the test if invoked for a collapsed node — proves a collapsed subtree is never
  queried beyond `has_children()`. This is the concrete proof of the reason lazy loading was chosen
  over eager in the first place.
- Row rendering (depth/indent/icon/label) from a small fixture tree.
- Click → select + expand/collapse toggle, and detail-panel/attributes population.
- Keyboard nav (Up/Down/Left/Right/Enter) sequences producing the expected selection/expand-state.
- Tier 3 reflection: composite vs. value-member classification, non-null pointer descent, null →
  no child, member-name labeling, attributes derived from primitive fields.

## Files Affected

- `include/prism/widgets/tree.hpp` — new: `TreeSource`, `TreeNodeId`, `TreeStorage` concept +
  `wrap_tree_storage` / `wrap_struct_tree` (reflection-gated, `#if __cpp_impl_reflection`, same
  structure as `table.hpp`), lazy `visible_rows()`, `TreeRow`, `Widget<TreeRow>`, the generic
  attributes-list detail widget, `TreeModel`, `TreeController`.
- `include/prism/app/widget_tree.hpp` — add `vb.tree(source)` overloads (mirrors existing
  `vb.table()` / `vb.list()`).
- `examples/model_tree_browser.cpp` — new: filesystem `TreeSource` (Tier 2, `std::filesystem`, no
  new dependency) and a reflected recursive-struct example (Tier 3).
- `tests/test_tree.cpp` — new.
