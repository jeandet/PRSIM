# Inspector Field Annotations — Design Spec

**Date:** 2026-07-15
**Status:** Approved (pending spec review)
**Builds on:** [Live Object Inspector](2026-07-14-live-object-inspector-design.md)

## Problem

`Inspector<T>`/`FieldMirror<T>` (`include/prism/widgets/field_mirror.hpp`) auto-generates an
editable UI from any plain struct `T` by walking `nonstatic_data_members_of(^^T)` and giving every
member a `LeafSlot<M>` (name caption + editable value) or recursing into a nested `FieldMirror<M>`.
Every member is treated identically: same widget, same name (`identifier_of(m)`), same
editability, same flat position in the generated `vstack`.

For small structs this is fine. For a real settings/device-state struct it isn't:

- Some members are internal bookkeeping (cache versions, dirty flags) that shouldn't appear in the
  UI at all.
- Some names aren't good UI labels (`sample_rate` vs "Sample Rate (Hz)").
- Some members should be visible but not editable (device IDs, computed/derived-looking fields).
- A large struct with 15+ members renders as one undifferentiated list with no grouping.

This is explicitly **not** solved by PRISM's existing sentinel-type mechanism
(`doc/design/widgets-and-sentinels.md`), because `Inspector<T>` walks a *plain* struct's raw member
types — there's no `Field<T>`/`State<T>` distinction to hang a sentinel off, unlike the main
Component/WidgetTree path where `skip` already exists via `State<T>` vs `Field<T>`
(`widgets-and-sentinels.md`: "reflection emits a widget for `Field<T>` and skips `State<T>`").

## Scope

This feature is scoped to `Inspector<T>`/`FieldMirror<T>` only. It does not touch `reflect.hpp`,
`delegate.hpp`, or the main Component/`WidgetTree` reflection path (`widget_tree.hpp`), which
already has its own conventions (manual `vstack`/`hstack` placement, `State<T>` for skip) that are
out of scope for this change.

## Solution

### New file: `include/prism/core/fixed_string.hpp`

A structural compile-time string, needed because annotation values must be constants of structural
type, and `label`/`section` need to carry a string payload:

```cpp
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    consteval fixed_string(const char (&s)[N]) { std::ranges::copy(s, data); }
    constexpr std::string_view view() const { return {data, N - 1}; }
};
template <std::size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;
```

Generic utility, not inspector-specific — lives in `core/` so future reflection-driven features
(e.g. table columns) can reuse it without a new dependency on `widgets/field_mirror.hpp`.

### New annotations in `namespace prism::inspector` (`field_mirror.hpp`)

```cpp
constexpr inline struct {} skip{};
constexpr inline struct {} readonly{};

template <fixed_string S> struct label_t   { static constexpr auto value = S; };
template <fixed_string S> struct section_t { static constexpr auto value = S; };
template <std::size_t N> consteval auto label(const char (&s)[N])   { return label_t<fixed_string<N>(s)>{}; }
template <std::size_t N> consteval auto section(const char (&s)[N]) { return section_t<fixed_string<N>(s)>{}; }
```

Usage:

```cpp
struct Settings {
    [[=prism::inspector::section("Audio")]]
    float volume;

    [[=prism::inspector::label("Sample Rate (Hz)")]]
    int sample_rate;

    [[=prism::inspector::readonly]]
    std::string device_id;

    [[=prism::inspector::skip]]
    int internal_cache_version;
};
```

Both new files stay guarded by `#if __cpp_impl_reflection`, matching every other reflection-gated
file in the codebase.

### Detection

Each annotation is read via `std::meta::annotations_of(m)` inside `field_mirror_tuple_info<T>()`
(`field_mirror.hpp:39-45`), which already holds `std::meta::info m` per member before deciding its
slot type — this is the single integration point for all four annotations:

```cpp
template <typename Tag>
consteval bool has_annotation(std::meta::info m) {
    return std::ranges::any_of(std::meta::annotations_of(m),
        [](std::meta::info a) { return std::meta::type_of(a) == ^^Tag; });
}
```

`label`/`section` use the analogous "find and extract" form to pull the `fixed_string` payload out
of the matching annotation, rather than just detecting presence.

### Slot type changes (skip, readonly)

These two change *which slot type* a member gets, decided in `field_mirror_tuple_info<T>()`:

- **`skip`** → new `HiddenSlot<M>` (just `Field<M> value{}`, no name field). It still participates
  in `sync_from`/`build`, so the member round-trips unchanged on every rebuild — it's invisible,
  not dropped. `for_each_leaf` and `view()` skip `HiddenSlot`s.

  **Correctness invariant:** `Inspector<T>::push_local()` calls `mirror_.build()` to reconstruct
  the *entire* `T` and pushes it to `source_`. If a skipped member were omitted from the mirror
  tuple entirely, every local edit to any *other* field would silently reset the skipped field to
  `T{}`'s default — a data-loss bug. `HiddenSlot` avoids this by keeping the member wired into the
  round trip while excluding it only from enumeration/rendering.

- **`readonly`** → `LeafSlot<M, /*readonly=*/true>`, where the value field is `Field<Label<M>>`
  instead of `Field<M>`. No new widget: `Widget<Label<T>>` already exists
  (`include/prism/ui/delegate.hpp:440`) and is already used for the slot's name caption, so this
  reuses an existing rendering path rather than adding one.

### Display metadata changes (label, section)

These do **not** change slot type — they're pure display metadata, consistent with how the name is
already a runtime string set in `sync_from` today (`slot.name.set(...)`, `field_mirror.hpp:65`).

- **`label`**: a `static constexpr` array of `std::optional<std::string_view>` overrides, computed
  once via `define_static_array` alongside the existing `members` array (same pattern already used
  in `sync_from`/`build`/`for_each_leaf`). `sync_from` uses the override instead of
  `identifier_of(m)` when present.
- **`section`**: a parallel `static constexpr` array marking which members start a new section, and
  with what title. `view()` inserts a `Label<std::string>` header row before any member that starts
  a section. v1 is a flat header row in the existing `vstack` — no new widget, no collapse state.
  This is forward-compatible with a future collapsible group widget: the annotation and the
  section-boundary data don't change, only what `view()` does with a boundary would need to change.

### Non-goals

- No changes to the main Component/`WidgetTree` reflection path.
- No collapsible/expandable sections in this iteration.
- No validation/range annotations (e.g. `[[=prism::inspector::range(0,100)]]`) — that would encode
  rendering *parameters*, which is what sentinel types (`Slider<T, Options>`) already own on the
  main path; introducing a second mechanism for the same concern was explicitly rejected during
  design discussion.

## Testing

Per TDD: one failing-first test per annotation, using a small test struct behind
`Shared<TestStruct>` wrapped in `Inspector<TestStruct>`:

- **`skip`**: edit other fields, call `build()`, assert the skipped field's value is unchanged from
  what was originally set on `source_`. This is the test that would catch the data-loss bug
  described above if `HiddenSlot` wiring is wrong.
- **`readonly`**: assert the generated slot for that member is a `Field<Label<M>>`, not `Field<M>`,
  and that `handle_input` on it is a no-op (mirroring `Widget<Label<T>>::handle_input`, which is
  already empty).
- **`label`**: assert the name `Field<Label<std::string>>` shows the override string, not
  `identifier_of(m)`.
- **`section`**: assert a `Label<std::string>` header row is inserted immediately before the first
  member of the section, and nowhere else.

`skip`/`readonly` (slot-type-level, `FieldMirror`-internal) belong in `tests/test_field_mirror.cpp`.
`label`/`section` are exercised the same way, alongside `skip`/`readonly` — they only need
`FieldMirror`, not the full `Shared<T>` sync machinery in `tests/test_inspector.cpp`. No new test
file.
