# WidgetTree Decomposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `include/prism/app/widget_tree.hpp` (2024 lines) into smaller, focused headers —
extracting the parts that have zero or minimal coupling to `WidgetTree`'s private instance state —
with **no behavior change**. This is Step 4 of `doc/review-2026-08-28.md`'s recommended order of
work (steps 1–3 shipped in the prior session, see `doc/handover-2026-08-28.md`).

**Architecture:** Three extractions, ordered easiest/lowest-risk first:
1. Stateless `WidgetNode`/`LayoutNode` accessor helpers + the static layout builder → new header.
2. Stateless dirty/traversal tree-walk helpers → new header (depends on file 1).
3. The `ViewBuilder` nested class (the 630-line builder DSL) → new header, using a
   forward-declare + friend + bottom-of-file-include pattern (verified to compile with this
   project's exact toolchain — see Task 3's rationale).

A fourth candidate (tree-construction + materialization: `build_widget_node`, `connect_dirty`,
`build_node_tree`, `materialize_tabs/virtual_list/table`) is **deliberately left in
`widget_tree.hpp`** — `materialize_tabs` calls directly into three tree-construction functions,
and all of them mutate `index_`/`parent_map_`/`focus_order_`/`next_id_`/`theme_` extensively.
Splitting it would need the same friend-class trick a second time for strictly less clean
separation. Not in scope for this plan; revisit only if a future task needs it.

**Tech Stack:** C++26 (GCC 16, `-std=c++26`, `cpp_std=c++26` in `meson.build`), Meson build,
doctest test framework.

**Spec:** `doc/review-2026-08-28.md` (§"Recommended order of work", step 4) and
`doc/handover-2026-08-28.md` ("What's left" section).

## Global Constraints

- **No behavior change.** Every task is pure code motion (plus the minimal glue a split
  requires: forward declarations, `friend` grants, `#include`s). No new features, no bug fixes
  bundled in.
- **Existing test suite is the regression harness** — there is no new behavior to TDD against.
  Baseline is `meson test -C builddir` → `Ok: 69, Fail: 0`. Every task must reproduce this exact
  count after its edits, unchanged. If the count differs, something behavioral leaked into the
  move — stop and investigate before committing.
- **Extracted functions must be declared `inline`, not `static`.** The originals are `static`
  *member* functions of a class defined inline in a header — that's implicitly ODR-safe across
  translation units. A `static` *free* function at namespace scope has internal linkage (a
  separate copy per TU) which is not what we want for shared helpers; a plain free function
  without `inline` risks ODR violations if ever taken by address across TUs. Use `inline`.
- **Follow the existing internal-helper convention**: `include/prism/app/event_routing.hpp` puts
  free helper functions in `namespace prism::app::detail` with `using namespace prism::core;` /
  `using namespace prism::ui;` at the top and an explicit, self-contained `#include` list. Match
  the shape (namespace + usings + explicit includes) for the two headers Tasks 1–2 create — but
  name their namespace `prism::app::widget_detail`, not plain `detail`: Task 1 originally used
  `detail` (matching `event_routing.hpp` literally) and it collided with the pre-existing
  `prism::ui::detail`, breaking an unrelated test file (see Task 1's Step 5 note below for the
  full mechanism). `event_routing.hpp`'s own `prism::app::detail` is untouched by this plan —
  it carries the same latent collision risk, but renaming it is out of scope here; a final
  whole-branch review flagged it as a candidate follow-up, not a blocker for this plan.
  `ViewBuilder` (Task 3) stays in `namespace prism::app` (not `detail`/`widget_detail`) since
  it's part of `WidgetTree`'s public surface today.
- **Commit after each task**, not at the end. Stage only the exact files each task touches.
- Build directory: `builddir` (already configured). Rebuild with `ninja -C builddir`, test with
  `meson test -C builddir`.

---

## Task 1: Extract stateless WidgetNode/LayoutNode accessors + `build_layout`

**Files:**
- Create: `include/prism/app/widget_tree_layout.hpp`
- Modify: `include/prism/app/widget_tree.hpp` (delete two blocks, add one `#include` + one
  `using namespace`)

**Interfaces:**
- Produces (in `namespace prism::app::widget_detail`): `ScrollState& ensure_scroll_state(WidgetNode&)`,
  `TableState* get_table_state(WidgetNode&)`, `VirtualListState* get_vlist_state(WidgetNode&)`,
  `TabsState* get_tabs_state_ptr(WidgetNode&)`, `struct ScrollView { DY& offset; Height
  viewport_h; Height content_h; uint8_t& show_ticks; ScrollEventPolicy event_policy; }`,
  `std::optional<ScrollView> get_scroll_view(WidgetNode&)`, `void build_layout(WidgetNode&,
  LayoutNode&)`. Task 2 consumes `get_vlist_state`/`get_table_state` from this header.

These functions are **fully static today** (verified by direct read of
`include/prism/app/widget_tree.hpp:1088-1127` and `:1912-2021`): none of them touch `this`,
`theme_`, `index_`, or any other `WidgetTree` instance member — they only take `WidgetNode&`/
`LayoutNode&` by reference. This is the lowest-risk extraction in the plan.

- [ ] **Step 1: Confirm the baseline**

Run: `meson test -C builddir -v 2>&1 | tail -5`
Expected: `Ok: 69` (or whatever the current count is — read the actual number, don't assume 69
if the repo has moved on; this is the number every later step must reproduce).

- [ ] **Step 2: Create the new header**

Read `include/prism/app/widget_tree.hpp:1088-1127` (the accessor cluster: `ensure_scroll_state`
through `get_scroll_view`, including the nested `ScrollView` struct) and `:1912-2021`
(`build_layout`) to copy their bodies verbatim — do not retype them by hand, use them exactly as
they exist in the file today. Then write:

```cpp
#pragma once

#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/delegate.hpp>

#include <any>
#include <memory>
#include <optional>

namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

inline ScrollState& ensure_scroll_state(WidgetNode& node) {
    return node.get_or_create<ScrollState>();
}

inline TableState* get_table_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TableState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline VirtualListState* get_vlist_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<VirtualListState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline TabsState* get_tabs_state_ptr(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TabsState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

struct ScrollView {
    DY& offset;
    Height viewport_h;
    Height content_h;
    uint8_t& show_ticks;
    ScrollEventPolicy event_policy;
};

inline std::optional<ScrollView> get_scroll_view(WidgetNode& node) {
    if (auto* ss = std::any_cast<ScrollState>(&node.edit_state))
        return ScrollView{ss->offset_y, ss->viewport_h, ss->content_h, ss->show_ticks, ss->event_policy};
    if (auto* vls = get_vlist_state(node)) {
        Height ch{static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
        return ScrollView{vls->scroll_offset, vls->viewport_h, ch, vls->show_ticks, vls->event_policy};
    }
    if (auto* ts = get_table_state(node)) {
        Height ch{static_cast<float>(ts->row_count()) * ts->row_height.raw()};
        return ScrollView{ts->scroll_y, ts->viewport_h, ch, ts->show_ticks, ts->event_policy};
    }
    return std::nullopt;
}

inline void build_layout(WidgetNode& node, LayoutNode& parent) {
    // ... copy the full body from widget_tree.hpp:1913-2020 verbatim, unchanged ...
}

} // namespace prism::app::widget_detail
```

(The `build_layout` body is ~108 lines of existing, already-working code — copy it exactly as it
reads in `widget_tree.hpp:1913-2020`, just drop the leading `static` keyword and prefix the
function with `inline` as shown.)

- [ ] **Step 3: Edit `widget_tree.hpp` — remove the two extracted blocks**

Delete lines `1912-2021` (the `static void build_layout(...)` function, replaced above) first
(delete the higher line range first so the lower one's line numbers don't shift out from under
you), then delete lines `1088-1127` (the accessor cluster + `ScrollView` struct).

- [ ] **Step 4: Wire up the new header**

At the top of `widget_tree.hpp`, add the include next to the other `prism/` includes:

```cpp
#include <prism/app/widget_tree_layout.hpp>
```

Right after the existing `using namespace prism::core; using namespace prism::ui;` lines near
the top of the file, add:

```cpp
using namespace prism::app::widget_detail;
```

This keeps every existing unqualified call site (`get_vlist_state(node)`, `build_layout(c,
container)`, etc.) compiling unchanged — they now resolve via unqualified lookup into
`prism::app::widget_detail` instead of as class-member calls.

- [ ] **Step 5: Rebuild and verify no regressions**

Run: `ninja -C builddir && meson test -C builddir`
Expected: same `Ok:` count as Step 1, `Fail: 0`. If it doesn't compile, the likely cause is a
missed call site still assuming these are member functions (e.g. written as
`WidgetTree::get_vlist_state(...)` somewhere) — grep for `WidgetTree::get_` and
`WidgetTree::build_layout` and drop the qualifier if you find any.

**Namespace name is load-bearing — don't rename it back to plain `detail`.** An earlier
attempt at this task used `namespace prism::app::detail`, which silently compiles for
`widget_tree.hpp` itself but breaks `tests/test_text_area.cpp` with "reference to `detail`
is ambiguous" against the pre-existing `prism::ui::detail` (`include/prism/ui/delegate.hpp`).
The collision isn't caused by anything in `widget_tree.hpp` — `test_text_area.cpp` opens
`namespace prism { using namespace app; using namespace ui; ... }`, and qualified lookup of
`prism::detail::wrap_lines` from there searches through both `app`'s and `ui`'s nominated
namespaces, so simply having a namespace *named* `prism::app::detail` anywhere is enough to
collide with `prism::ui::detail::wrap_lines`/`cursor_to_line_col`/`line_col_to_cursor`,
regardless of whether `widget_tree.hpp` uses `using namespace` or per-symbol `using`
declarations. `widget_detail` (as specified above) does not collide — verify this by actually
running the full suite (`meson test -C builddir`, not just `ninja`), since this failure only
shows up in `test_text_area.cpp`, a file this task never touches directly.

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/widget_tree_layout.hpp include/prism/app/widget_tree.hpp
git commit -m "$(cat <<'EOF'
refactor(app): extract stateless WidgetNode accessors + build_layout from WidgetTree

Pure code motion, no behavior change (doc/review-2026-08-28.md step 4).
These were already static, instance-state-free functions; moving them out
shrinks widget_tree.hpp toward the review's "split the frame pipeline out"
recommendation with zero coupling risk.
EOF
)"
```

---

## Task 2: Extract stateless dirty/traversal helpers

**Files:**
- Create: `include/prism/app/widget_tree_traversal.hpp`
- Modify: `include/prism/app/widget_tree.hpp`

**Interfaces:**
- Consumes: `prism::app::widget_detail::get_vlist_state`, `prism::app::widget_detail::get_table_state` (from
  Task 1's `widget_tree_layout.hpp`).
- Produces (in `namespace prism::app::widget_detail`): `std::size_t count_leaves(const WidgetNode&)`,
  `bool check_dirty(const WidgetNode&)`, `std::size_t count_dirty(const WidgetNode&)`, `void
  collect_viewport_heights(WidgetNode&, std::vector<std::pair<WidgetId, Height>>&)`, `void
  clear_dirty_impl(WidgetNode&)`, `void close_overlays_impl(WidgetNode&)`, `void
  collect_leaf_ids(const WidgetNode&, std::vector<WidgetId>&)`, `void refresh_dirty(WidgetNode&)`.

**Important — line numbers shifted.** Task 1 deleted 40 lines (1088-1127) before this range, so
the absolute line numbers below (from the pre-Task-1 file) will be off by roughly that much.
Before cutting, re-locate each function with
`grep -n "count_leaves\|check_dirty\|count_dirty\|collect_viewport_heights\|clear_dirty_impl\|close_overlays_impl\|collect_leaf_ids\|refresh_dirty" include/prism/app/widget_tree.hpp`
and cut from the current file, not from memorized line numbers.

Eight of the eleven functions in this section (`widget_tree.hpp:1410-1503` pre-Task-1) are
extractable; **`build_index`, `propagate_theme`, and `set_dirty` must stay** — they read/write
`index_`, `focus_order_`, `parent_map_`, or `theme_` (verified by direct read of
`widget_tree.hpp:1412-1503`).

- [ ] **Step 1: Confirm the baseline**

Run: `meson test -C builddir -v 2>&1 | tail -5`
Expected: same `Ok:` count as Task 1's Step 5 (no regressions carried over).

- [ ] **Step 2: Create the new header**

```cpp
#pragma once

#include <prism/app/widget_tree_layout.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

inline std::size_t count_leaves(const WidgetNode& node) {
    if (!node.is_container)
        return node.layout_kind == LayoutKind::Spacer ? 0 : 1;
    std::size_t n = 0;
    for (auto& c : node.children) n += count_leaves(c);
    return n;
}

inline bool check_dirty(const WidgetNode& node) {
    if (node.dirty) return true;
    for (auto& c : node.children)
        if (check_dirty(c)) return true;
    return false;
}

inline std::size_t count_dirty(const WidgetNode& node) {
    std::size_t n = node.dirty ? 1 : 0;
    for (auto& c : node.children) n += count_dirty(c);
    return n;
}

inline void collect_viewport_heights(WidgetNode& node, std::vector<std::pair<WidgetId, Height>>& out) {
    if (node.layout_kind == LayoutKind::VirtualList) {
        if (auto* vls = get_vlist_state(node)) out.push_back({node.id, vls->viewport_h});
    } else if (node.layout_kind == LayoutKind::Table) {
        if (auto* ts = get_table_state(node)) out.push_back({node.id, ts->viewport_h});
    }
    for (auto& c : node.children) collect_viewport_heights(c, out);
}

inline void clear_dirty_impl(WidgetNode& node) {
    node.dirty = false;
    for (auto& c : node.children) clear_dirty_impl(c);
}

inline void close_overlays_impl(WidgetNode& node) {
    if (!node.overlay_draws.empty()
        && node.layout_kind != LayoutKind::Table
        && node.layout_kind != LayoutKind::Tabs) {
        node.overlay_draws.clear();
        node.edit_state.reset();
        node.dirty = true;
    }
    for (auto& c : node.children) close_overlays_impl(c);
}

inline void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
    if (!node.is_container) {
        if (node.layout_kind != LayoutKind::Spacer)
            ids.push_back(node.id);
        return;
    }
    for (auto& c : node.children) collect_leaf_ids(c, ids);
}

inline void refresh_dirty(WidgetNode& node) {
    if (node.dirty && node.record)
        node.record(node);
    for (auto& c : node.children)
        refresh_dirty(c);
}

} // namespace prism::app::widget_detail
```

The original code carries a load-bearing comment on `collect_viewport_heights`
(`widget_tree.hpp`, right above its old location) explaining *why* it exists — copy that comment
along with the function, don't drop it:

```cpp
// VirtualList/Table viewport heights, keyed by widget id in a stable tree-order walk --
// used by build_snapshot() to detect whether a materialization pass actually needs
// redoing (see its comment), rather than re-running on any unrelated dirty flag.
```

- [ ] **Step 3: Edit `widget_tree.hpp` — remove the eight extracted functions**

Delete `count_leaves`, `check_dirty`, `count_dirty`, `collect_viewport_heights` (+ its comment),
`clear_dirty_impl`, `close_overlays_impl`, `collect_leaf_ids`, `refresh_dirty` from their current
locations (re-grepped per the note above). Leave `build_index`, `propagate_theme`, and
`set_dirty` exactly where they are, interleaved as they currently are — don't reorder the
survivors, just remove the eight functions around them.

- [ ] **Step 4: Wire up the new header**

Add `#include <prism/app/widget_tree_traversal.hpp>` next to the Task 1 include in
`widget_tree.hpp`. No new `using namespace` needed — Task 1 already added `using namespace
prism::app::widget_detail;`, which now also covers this header's contents.

- [ ] **Step 5: Rebuild and verify no regressions**

Run: `ninja -C builddir && meson test -C builddir`
Expected: same `Ok:` count as Step 1 of this task, `Fail: 0`.

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/widget_tree_traversal.hpp include/prism/app/widget_tree.hpp
git commit -m "$(cat <<'EOF'
refactor(app): extract stateless dirty/traversal tree-walk helpers from WidgetTree

Pure code motion, no behavior change (doc/review-2026-08-28.md step 4).
build_index/propagate_theme/set_dirty stay put -- they mutate WidgetTree's
own index_/focus_order_/theme_ and don't separate cleanly.
EOF
)"
```

---

## Task 3: Extract `ViewBuilder` to its own header

**Files:**
- Create: `include/prism/app/view_builder.hpp`
- Modify: `include/prism/app/widget_tree.hpp`

**Interfaces:**
- Consumes: `WidgetTree::next_id_` (private member), `WidgetTree::build_node_tree()` (private,
  templated method), `WidgetTree::field_names_` (private member, gated behind
  `PRISM_DEBUG_TOOLS_ENABLED` + `__cpp_impl_reflection`) — via `friend class app::ViewBuilder;`.
  Also calls `WidgetTree`'s **public** API: `begin_split_drag`/`update_split_drag`/
  `end_split_drag`, `scroll_row_into_view`, `set_focused`.
- Produces: `prism::app::ViewBuilder` (unchanged public interface — every `void view(WidgetTree::
  ViewBuilder& vb)` in the other 43 files that use the DSL keeps compiling unmodified, because
  `WidgetTree::ViewBuilder` stays a valid nested-name lookup via a `using` alias).

**Why this is safe** (verified, not assumed — see rationale below): `ViewBuilder` today is a
*nested* class of `WidgetTree`, and mutually calls into `WidgetTree` (`ViewBuilder`'s methods
call `WidgetTree::set_focused` etc.) while `WidgetTree::build_node_tree` constructs a `ViewBuilder`
by value. Naively splitting two mutually-dependent classes into separate top-level classes is a
classic C++ trap — each side's inline method bodies need the *other* type complete, which is
usually impossible to satisfy by ordering alone. **This was tested directly against this
project's actual compiler** (`g++ (GCC) 16.0.1`, `-std=c++26`) with a minimal reproduction
mirroring the exact structure (forward-declared `ViewBuilder`, a `using ViewBuilder =
app::ViewBuilder;` alias plus `friend class app::ViewBuilder;` inside `WidgetTree`, a templated
`WidgetTree::build(Model&)` constructing `ViewBuilder vb{*this};` by value, and `ViewBuilder`'s
own inline methods calling a private `WidgetTree` member and a private templated method) — it
compiled cleanly. This works because `build_node_tree` is itself a *template*: GCC 16 defers the
completeness check on the non-dependent local `ViewBuilder vb{...}` to instantiation time (which
only happens downstream, once both classes are fully defined), rather than at the point
`build_node_tree`'s body is parsed.

**One non-obvious gotcha the repro also caught**: the `friend` declaration must use the
fully-qualified name, `friend class app::ViewBuilder;`, **not** `friend class ViewBuilder;` — the
latter collides with the `using ViewBuilder = app::ViewBuilder;` alias declared in the same
class ("using typedef-name … after 'class'" — a real compile error, not a style nit).

- [ ] **Step 1: Confirm the baseline**

Run: `meson test -C builddir -v 2>&1 | tail -5`
Expected: same `Ok:` count as Task 2's Step 5.

- [ ] **Step 2: Create the new header with ViewBuilder's body moved verbatim**

Read `include/prism/app/widget_tree.hpp:36-664` (the entire `class ViewBuilder { ... };` body,
from Task 1/2's edits this range is untouched and still at these exact lines — verify with
`sed -n '36p;664p' include/prism/app/widget_tree.hpp` showing `    class ViewBuilder {` and
`    };` respectively before proceeding). Copy that block verbatim — every line, including its
internal comments, `DependsOnMixin`, `TableBuilder`, `CanvasHandle`, and all the `vstack`/
`hstack`/`spacer`/`handle`/`scroll`/`canvas`/`list`/`tree`/`table`/`tabs`/`tab`/`finalize`
overloads — into:

```cpp
#pragma once

#include <prism/delegates/dropdown_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/list.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/state.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/tree.hpp>
#include <prism/ui/widget_node.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#endif

#include <algorithm>
#include <set>
#include <type_traits>
#include <vector>

namespace prism::app {
class WidgetTree;

using namespace prism::core;
using namespace prism::ui;

class ViewBuilder {
    // ... paste widget_tree.hpp:37-663 here verbatim (the body between the class's
    // opening and closing brace), completely unchanged ...
};

} // namespace prism::app
```

Do not rewrite, "clean up", or reformat anything while moving it — this task is a pure cut/paste,
and any incidental change makes the diff harder to trust as behavior-preserving.

- [ ] **Step 3: Edit `widget_tree.hpp` — replace the nested class with a forward declaration**

Delete lines `36-664` (the entire nested `ViewBuilder` definition). Immediately before `class
WidgetTree {` (currently line 34, may have shifted from Tasks 1-2's edits — re-locate with
`grep -n "^class WidgetTree"`), insert:

```cpp
class ViewBuilder;

```

Immediately after the `public:` label that used to open onto `class ViewBuilder { ... }` (now
the first thing after `public:`), insert:

```cpp
    using ViewBuilder = app::ViewBuilder;
    friend class app::ViewBuilder;

```

(Fully-qualified `app::ViewBuilder` in the `friend` declaration — see the gotcha above.)

- [ ] **Step 3.5: Fix `build_node_tree`'s direct non-dependent calls on `vb`**

**This step was added after a real implementer BLOCKED on it — read this carefully, it is not
optional.** The forward-declare/friend/bottom-include mechanism (Steps 2-4) defers
`ViewBuilder`'s completeness-check to template instantiation time *only* for call expressions
that are themselves dependent on `build_node_tree`'s `Model` template parameter (like
`model.view(vb)`, where `vb`'s static type doesn't matter to the call but `model`'s does). It
does **not** cover a direct, non-dependent member-function call on `vb` itself — and
`build_node_tree` has exactly two of those, immediately after `model.view(vb);`:

```cpp
#if __cpp_impl_reflection
            check_unplaced_fields(model, vb.placed());
#endif
            vb.finalize();
```

`vb.placed()` and `vb.finalize()` are non-dependent calls on a non-dependent local object, so
GCC requires `ViewBuilder` complete *at the point `build_node_tree`'s body is parsed* to resolve
them — which the split cannot provide (verified with an isolated, from-scratch repro:
`ViewBuilder vb{*this}; vb.finalize();` inside the templated method fails with `'vb' has
incomplete type [-Wtemplate-body]`, even though the identical mechanism without that direct
call compiles cleanly).

**Fix:** route both calls through a generic lambda, invoked immediately, so the calls become
dependent on the lambda's own (deduced) template parameter instead of on the fixed `ViewBuilder`
type — deferring the lookup to instantiation time exactly like `model.view(vb)` already relies
on. This was verified against an isolated repro mirroring this exact shape (a private member
template called from inside the lambda, feeding a member of the deduced-type argument) before
being added to this plan.

Change:

```cpp
            ViewBuilder vb{*this, root};
            model.view(vb);
#if __cpp_impl_reflection
            check_unplaced_fields(model, vb.placed());
#endif
            vb.finalize();
```

to:

```cpp
            ViewBuilder vb{*this, root};
            model.view(vb);
            // vb.placed()/vb.finalize() are non-dependent calls on a non-dependent local
            // object; called directly here they'd require ViewBuilder complete at this point
            // in the file, which the forward-declared split (Steps 2-4) can't provide. Routing
            // them through a generic lambda makes the call dependent on the lambda's own
            // deduced template parameter, deferring the completeness check to instantiation
            // time -- the same mechanism model.view(vb) above already relies on.
            [&](auto& built) {
#if __cpp_impl_reflection
                check_unplaced_fields(model, built.placed());
#endif
                built.finalize();
            }(vb);
```

Locate the exact current text with
`grep -n "ViewBuilder vb{\*this, root}" include/prism/app/widget_tree.hpp` — there is exactly
one match (a second, unrelated `ViewBuilder vb{tree, target};` exists inside `ViewBuilder`'s own
`tab()` lazy-content-builder lambda, at what was originally line ~558 — that one is *inside* the
636-line block Step 3 deletes, moves to `view_builder.hpp` verbatim as part of it, and needs no
change: it's self-referential construction inside `ViewBuilder`'s own already-complete class
body once moved, not the same completeness problem).

- [ ] **Step 4: Wire up the new header**

At the very bottom of `widget_tree.hpp`, immediately after the closing `};` of `class WidgetTree`
(currently line 2022, re-locate if shifted), add:

```cpp

#include <prism/app/view_builder.hpp>
```

This must be **after** `WidgetTree`'s closing brace, not with the other includes at the top of
the file — the whole mechanism depends on `WidgetTree` already being a complete type by the time
`view_builder.hpp`'s `class ViewBuilder { ... }` body (which calls `WidgetTree::set_focused` and
friends) is parsed.

- [ ] **Step 5: Rebuild and verify no regressions**

Run: `ninja -C builddir && meson test -C builddir`
Expected: same `Ok:` count as Step 1, `Fail: 0`.

If this fails to compile, the most likely causes, in order of likelihood:
1. `'vb' has incomplete type [-Wtemplate-body]` (or the same on `built`) — Step 3.5 wasn't
   applied, or another direct non-dependent call on `vb` exists somewhere this plan didn't
   catch. Grep `widget_tree.hpp` for `vb\.` outside the lambda Step 3.5 introduces; anything
   found needs the same generic-lambda treatment.
2. A stray unqualified `friend class ViewBuilder;` left over from a hand-edit instead of
   `friend class app::ViewBuilder;` (see the gotcha above — the error message names the
   `using`-alias collision explicitly).
3. A missed transitive include — `view_builder.hpp`'s include list above is a superset copy of
   `widget_tree.hpp`'s original includes; if something's still missing, the compiler error will
   name the missing symbol (e.g. `Field`, `Connection`, `node_leaf`) and you can trace it to its
   header via `grep -rn "struct Field\|class Field" include/prism/`.

Do **not** try to fix a completeness/incomplete-type error by moving code back inline or
weakening the split (e.g. making `ViewBuilder` take `WidgetTree*` instead of `WidgetTree&`,
or template-parameterizing it) — the mechanism is verified to work as specified; a compile
failure at this step means a transcription mistake, not a fundamental limitation.

- [ ] **Step 6: Commit**

```bash
git add include/prism/app/view_builder.hpp include/prism/app/widget_tree.hpp
git commit -m "$(cat <<'EOF'
refactor(app): extract ViewBuilder out of WidgetTree into its own header

Pure code motion, no behavior change (doc/review-2026-08-28.md step 4).
ViewBuilder keeps its WidgetTree::ViewBuilder nested-name identity via a
using-alias + friend grant, so all 43 existing view() implementations
compile unchanged. This is the single biggest cut in the decomposition --
630 of widget_tree.hpp's original 2024 lines were the builder DSL.
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage**: `doc/review-2026-08-28.md` step 4 says "Splitting out virtualization and the
  snapshot builder is also the prerequisite for an explicit scene compiler." This plan
  deliberately does *not* extract virtualization/materialization (see Architecture section) —
  that's a scope decision, not an oversight: the coupling analysis showed it doesn't separate
  cleanly, and forcing it would trade one 2024-line file for two files each depending on the
  other's internals, which isn't a readability win. If a future scene-compiler task needs that
  split specifically, it should be scoped and planned separately once there's a concrete
  consumer driving the boundary.
- **Placeholder scan**: no TBD/TODO markers; the one abbreviated code block (`build_layout`'s
  108-line body in Task 1, `ViewBuilder`'s 630-line body in Task 3) points at an exact
  file:line-range of already-working code to copy verbatim rather than hand-transcribing it —
  this is the standard "cite the source of truth" pattern for pure code-motion tasks, not a
  placeholder for undesigned behavior.
- **Type/name consistency**: `prism::app::widget_detail::get_vlist_state`/`get_table_state` (Task 1) are
  the exact names Task 2's `collect_viewport_heights` calls; `WidgetTree::ViewBuilder` (Task 3's
  alias) is the exact name all 43 external `view()` signatures already use — verified by grep,
  not assumed.
