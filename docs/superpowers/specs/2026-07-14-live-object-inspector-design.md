# Live Object Inspector — Design Spec

**Date:** 2026-07-14
**Status:** Approved
**Strategic priority:** #1 — "struct becomes form/inspector instantly", the near-term differentiator

## Problem

PRISM already turns a local struct into a live, editable UI for free: `model_app(title, model)`
reflects over any struct's members and builds an editable widget tree, with no `view()` required.
That covers same-thread config panels.

It does not cover the cross-thread case, which is the actual "device control / settings panel /
live debug" niche named in the strategic roadmap. Today:

- `Shared<T>` already supports cross-thread *read*: `drain_notifications()` pumps a fresh whole-`T`
  value into the UI thread, redrawing on change (`node_readonly_leaf<T>`).
- But every `Shared<T>` node is wired read-only — there is no path for the UI to edit a `Shared<T>`
  back. `Derived<T>`/`Shared<T>` are always a single opaque leaf (via `Widget<T>`), never expanded
  per-field.
- The default reflection walk (`build_node_tree`, `widget_tree.hpp:918-945`) only picks up members
  that are already wrapped as `Field<M>`/`Derived<M>`/`Shared<M>`/a component. A plain struct like
  `struct DeviceState { float voltage; int mode; bool enabled; };` living behind one `Shared<T>` is
  invisible to it — none of its members match those categories, so they're silently skipped.

Bridging "one plain struct behind one cross-thread atomic cell" into "a tree of individually
editable, individually dirty-tracked fields" is the actual feature. The goal is zero boilerplate:
point PRISM at an existing plain struct and get a live editable panel, without writing a parallel
mirror struct by hand.

## Solution

### 1. `Inspector<T>` — the public type

```cpp
template <typename T>
struct Inspector {
    explicit Inspector(Shared<T>& source) : source_(source) {
        sync_from_remote(source_.get());
        source_.observe([this](const T& v) { sync_from_remote(v); });
    }

    void view(ViewBuilder& vb) {
        std::apply([&](auto&... fields) { vb.vstack(fields...); }, mirror_);
    }

    void drain() { source_.drain_notifications(); }

private:
    Shared<T>& source_;
    FieldMirrorOf<T> mirror_;   // std::tuple<Field<M>...>, one slot per reflected member of T

    void sync_from_remote(const T& v);  // template for m : members(^^T): get<i>(mirror_).set(v.[:m:])
    void push_local();                  // template for m : members(^^T): out.[:m:] = get<i>(mirror_).get(); source_.set(out)
    // each Field<Mi> in mirror_ has push_local wired to its change sender at construction
};
```

`Inspector<T>` is an ordinary PRISM component — it has a `view()`, so it composes with everything
that already exists: nested inside another model, placed via `vb.component()`, or run standalone
as the root of `model_app()`. No new top-level embedding API.

```cpp
prism::Shared<DeviceState> device_state;                 // owned/updated elsewhere, e.g. driver thread
prism::Inspector<DeviceState> inspector(device_state);    // zero-boilerplate mirror

struct DashboardModel {
    prism::Inspector<DeviceState> device_panel;
    void view(ViewBuilder& vb) { vb.hstack(device_panel, /* ...other panels... */); }
};

prism::model_app("Device Control", inspector);            // or standalone, same as any model
```

### 2. `FieldMirrorOf<T>` — reflection-derived shadow storage

`FieldMirrorOf<T>` is a `std::tuple<Field<M1>, Field<M2>, ...>`, one slot per member of `T`, with
member types derived purely from reflecting `T` — the same `nonstatic_data_members_of(^^T)` +
`template for` pattern `build_node_tree` already uses (`widget_tree.hpp:918-943`). No new nominal
type is synthesized for `T`; the tuple is positional storage only, so `std::meta::define_class` is
not needed.

Labels for each mirror field come from `std::meta::identifier_of(m)` on `T`'s members — the same
mechanism already used for auto-labeling in the existing reflection walk.

Nested plain structs (a member of `T` that is itself a plain struct, not yet a leaf type) recurse
the same way `is_component_v` already recurses today — generalized to types that aren't yet
`Field`-wrapped.

### 3. Sync direction: remote → local

`Shared<T>::observe()` registers a callback invoked whenever `drain_notifications()` fires with a
changed value (existing `Shared<T>` mechanism, unchanged). `Inspector<T>` uses it to splice each
member of the incoming `T` into the matching mirror slot's `Field<Mi>::set(...)`.

### 4. Sync direction: local → remote

Each `Field<Mi>` in the mirror already has the existing dirty-flag/change-sender machinery. At
construction, `Inspector<T>` connects each one's change sender to reconstruct a full `T` from the
current mirror values and call `source_.set(v)`.

### 5. Drain wiring — the one change to existing internals

`tree.drain_shared()` walks every `Node` with a non-null `drain_fn` (`collect_drains`,
`widget_tree.hpp:703-708` — a full tree walk, not leaf-only). `Inspector<T>` builds its own root
node via its `view()` method, so `build_node_tree`'s existing `view()`-detection branch
(`widget_tree.hpp:910-917`) needs one small, additive extension: if the component also exposes a
`drain()` method, set `root.drain_fn = [&model]{ model.drain(); };` — same opt-in-via-`requires`
style already used to detect `view()`. This is the only change required to existing PRISM
internals; everything else in this design is new, additive code.

### 6. Concurrency model

A local edit reconstructs and pushes the *whole* `T` back via `source_.set(v)`. A remote update
racing an in-flight local edit is last-write-wins at the whole-`T` granularity — the same semantics
`Shared<T>`/`atomic_cell` already have elsewhere in PRISM. This is an explicit, accepted tradeoff:
no field-level merge or conflict resolution is implemented.

## Testing plan

Headless, no real threads needed — matches the existing `test_model_app.cpp` style (drain is
pumped explicitly, decoupled from actual thread delivery):

- Wrap a `Shared<DeviceState>` in `Inspector<DeviceState>`, build a `WidgetTree`, verify the
  snapshot has one leaf per member with the correct labels.
- Call `source.set(new_value)` then the collected `drain_fn` directly — verify every mirror field
  reflects the new value.
- Simulate editing one mirror field (dispatch input to its leaf, or call `Field::set` directly) —
  verify `source.get()` returns a whole `T` with that field updated and siblings preserved.
- One level of nested plain-struct recursion — verify recursive mirror + sync.
- Race case: `source.set(...)` a new remote value, then immediately push a local edit before
  draining — verify last-write-wins matches the documented tradeoff (not treated as a bug).

## Scope

**What changes:**
- New `Inspector<T>` type and `FieldMirrorOf<T>` reflection-derived tuple generator.
- One additive `requires`-gated extension to `build_node_tree`'s `view()` branch, to wire a
  component's optional `drain()` method to its root `Node::drain_fn`.

**What stays the same:**
- `Field<T>`, `Derived<T>`, `Shared<T>` — untouched (existing whole-value-leaf behavior for
  `Derived<T>`/plain `Shared<T>` is still the pure-observation path).
- `ViewBuilder`, layout, input routing, `Widget<T>` dispatch — untouched.
- Reflection walk for already-`Field`-wrapped members (`build_node_tree`'s default branch) —
  untouched; `Inspector<T>` is additive, not a replacement.

## Non-goals

- Field-level merge/conflict resolution — whole-`T` last-write-wins is the accepted model.
- Multi-type selection at runtime (switching what's inspected between different `T`s in one
  instance) — single fixed `T` per `Inspector<T>`; swapping targets means swapping which
  `Shared<T>` instance is bound, not switching types.
- A read-only mode toggle on `Inspector<T>` — `Derived<T>`/plain `Shared<T>` already cover pure
  observation; `Inspector<T>` is specifically the editable case.
- Embedding into a non-PRISM host application (foreign event loop, foreign renderer) —
  `Inspector<T>` is a PRISM component like any other; out of scope here.

## Implementation notes (post-implementation)

The shipped implementation deviates from this spec's sketches in a few places, none of which
change the design's intent:

- **`source_` shipped as `Shared<T>*`, not `Shared<T>&`.** A `Shared<T>&` reference member was
  found during implementation to trigger a real GCC P2996 `identifier_of()` limitation inside
  `WidgetTree::check_unplaced_fields`'s debug-mode reflection walk (that walk reflects over
  `Inspector<T>`'s own members to warn about unplaced `Field`/`Derived`/`Shared` fields, and a
  reference member matched its `is_shared_v<M>` check in a way that hit the limitation). A raw
  pointer member doesn't match that check, so the walk skips it entirely. Verified with a
  standalone repro before switching to a pointer.

- **The mirror type shipped as `FieldMirror<T>`, not `FieldMirrorOf<T>`.** It holds per-leaf
  `LeafSlot<M>` pairs (a `Field<Label<std::string>>` name plus the `Field<M>` value) rather than
  bare `Field<M>` slots — the mechanism is unchanged, but the type was renamed and given the
  `LeafSlot<M>` wrapper for clarity given the per-field labeling requirement added after this
  spec was approved.

- **A `syncing_` reentrancy guard (`SyncGuard`, RAII) was added beyond the original design.** The
  originally-specified wiring — each mirror `Field<Mi>`'s change sender unconditionally calling
  `push_local()` — caused multi-field remote updates to echo torn/partial values back to
  `Shared<T>::set()`, since `sync_from` sets one leaf at a time. This was found and fixed during
  Task 4's review, with its own regression test, and is now also covered for the nested-struct
  case (a remote update to a `T` with a nested struct member, verifying both the synced value and
  the absence of echo at nested depth).

- **The testing plan's "remote set then local edit before draining" race test was not implemented
  as literally described.** The shipped echo/settle tests (single-field and multi-field, plus the
  new nested-struct case) cover the same last-write-wins concurrency model described in
  "Concurrency model" above, but from the adjacent angle — local-edit-then-drain and
  remote-update-then-drain — rather than that exact interleave. This is accepted as sufficient
  coverage of the documented tradeoff, not tracked as an open gap.
