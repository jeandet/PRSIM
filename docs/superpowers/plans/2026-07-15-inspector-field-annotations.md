# Inspector Field Annotations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add four C++26 annotations (`skip`, `readonly`, `label<"...">`, `section<"...">`) that `Inspector<T>`/`FieldMirror<T>` reads via `std::meta::annotations_of()` to customize how a plain struct's members render in the auto-generated live inspector, without touching the struct's declared member types.

**Architecture:** `field_mirror_tuple_info<T>()` (the existing consteval function that decides each member's slot type) gains an annotation-detection pass per member: `skip` routes to a new `HiddenSlot<M>` (round-trips but never renders), `readonly` adds a `bool` template parameter to the existing `LeafSlot<M>` that swaps which `ViewBuilder` call renders the value, and `label`/`section` are pure display-string metadata consulted at `sync_from()`/`view()` time. One new small, generic method (`ViewBuilder::widget_readonly`) is added to `widget_tree.hpp`, mirroring the existing `widget(Derived<T>&)` pattern, because rendering a non-interactive arbitrary-typed value has no existing hook reachable from `Inspector`-only code.

**Tech Stack:** C++26 reflection (GCC 16, `-freflection`), doctest, existing PRISM core/ui/app modules.

**Verified against the compiler:** every non-trivial reflection construct in this plan (annotation declaration syntax, `has_annotation`/`extract_string_annotation` helpers, `std::meta::substitute` with a mixed type+bool argument list, splicing a reflected type into a concept check) was compiled standalone against this machine's `g++ (GCC) 16.0.1` with `-std=c++26 -freflection` before being written into this plan. Two things changed from the original approved spec as a direct result:

1. **Annotation call syntax is NTTP-style, not function-call style.** `[[=prism::inspector::label("...")]]` (a `consteval` function taking `const char(&)[N]`) does not compile inside an annotation — GCC rejects the array-reference parameter as "not a constant expression" at that evaluation boundary. `[[=prism::inspector::label<"...">]]` (a variable template, NTTP-deduced from the string literal) compiles cleanly and is the syntax used throughout this plan.
2. **`readonly` needs one new `ViewBuilder` method in `widget_tree.hpp`.** The brainstorm's scope decision was "Inspector<T>/FieldMirror<T> only, nothing in widget_tree.hpp changes." That holds for `skip`/`label`/`section` (pure `FieldMirror` internals). `readonly` doesn't: rendering an arbitrary-typed value without input wiring requires `node_readonly_leaf<T>()`, which already exists in `widget_node.hpp` for exactly this purpose but is currently only reachable from `ViewBuilder` via the `Derived<T>&`/`Shared<T>&` overloads of `widget()` — neither fits a plain mutable mirror slot. The fix is a five-line additive overload, `widget_readonly(Field<T>&)`, following the exact existing pattern.

## Global Constraints

- C++26, GCC 16 `-freflection` (project already requires this; see `meson.build`).
- All new reflection code stays inside `#if __cpp_impl_reflection` guards, matching every other reflection-gated file in the codebase.
- TDD: failing test before implementation, for every task.
- Verify the full test suite passes (real pass/fail count, not a partial grep) before the final commit.
- No changes to `reflect.hpp`, `delegate.hpp`, or the main Component/`WidgetTree` reflection walk (`widget_tree.hpp:920-947`) — only the one additive `ViewBuilder` method noted above.

---

## File Structure

- **Create:** `include/prism/core/fixed_string.hpp` — generic structural compile-time string (NTTP payload for `label`/`section`). Not reflection-specific, no `#if` guard needed.
- **Create:** `tests/test_fixed_string.cpp` — unit tests for `fixed_string`.
- **Modify:** `include/prism/widgets/field_mirror.hpp` — annotation declarations/helpers, `HiddenSlot<M>`, `LeafSlot<M, bool>`, and every method that walks members (`field_mirror_tuple_info`, `sync_from`, `build`, `for_each_leaf`, `view`) gains annotation-awareness.
- **Modify:** `include/prism/app/widget_tree.hpp` — one new `ViewBuilder::widget_readonly(Field<T>&)` method.
- **Modify:** `tests/test_field_mirror.cpp` — one test per annotation, appended to the existing file (matches existing granularity: one test file per reflected component, not one file per annotation).

---

### Task 1: `fixed_string` — structural compile-time string

**Files:**
- Create: `include/prism/core/fixed_string.hpp`
- Test: `tests/test_fixed_string.cpp`

**Interfaces:**
- Produces: `prism::core::fixed_string<N>` — `struct` with `char data[N]`, a `consteval` constructor from `const char(&)[N]`, a deduction guide, and `constexpr std::string_view view() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_fixed_string.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/fixed_string.hpp>

TEST_CASE("fixed_string stores a string literal and exposes it as string_view") {
    constexpr prism::core::fixed_string fs{"Custom Name"};
    static_assert(fs.view() == "Custom Name");
    CHECK(fs.view() == "Custom Name");
}

TEST_CASE("fixed_string is usable as a template non-type parameter") {
    // If this compiles, NTTP deduction from a string literal works end to end.
    constexpr auto make = []<prism::core::fixed_string S>() { return S.view(); };
    CHECK(make.operator()<prism::core::fixed_string{"abc"}>() == "abc");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson setup builddir --reconfigure && ninja -C builddir test_fixed_string 2>&1 | tail -30`
Expected: FAIL — `prism/core/fixed_string.hpp: No such file or directory` (or a meson "unknown target" error; the test isn't wired into `meson.build` yet — that's expected at this step, fix in Step 3).

- [ ] **Step 3: Write minimal implementation**

Create `include/prism/core/fixed_string.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <string_view>

namespace prism::core {

// A structural compile-time string, usable as a template non-type parameter.
// Needed because C++26 annotations ([[=expr]]) must be constants of
// structural type, and prism::inspector::label/section carry a string.
template <std::size_t N>
struct fixed_string {
    char data[N]{};

    consteval fixed_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }

    constexpr std::string_view view() const { return {data, N - 1}; }
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace prism::core
```

Add the test target to `tests/meson.build`. It's a single data-table entry — find the `headless_tests` map (around line 4) and add a line matching the existing `'field_mirror' : files('test_field_mirror.cpp'),` entry:

```meson
  'field_mirror' : files('test_field_mirror.cpp'),
  'inspector' : files('test_inspector.cpp'),
  'fixed_string' : files('test_fixed_string.cpp'),
}
```

(Add the new line right after `'inspector'`, keeping the closing `}` of the `headless_tests` dict. The `foreach` loop below it already builds and registers every entry in the map — no other `meson.build` change is needed.)

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_fixed_string && ./builddir/test_fixed_string`
Expected: PASS, `[doctest] test cases: 2 | 2 passed | 0 failed`

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/fixed_string.hpp tests/test_fixed_string.cpp meson.build
git commit -m "feat: add fixed_string compile-time string for annotation payloads"
```

---

### Task 2: Annotation declarations and detection helpers

**Files:**
- Modify: `include/prism/widgets/field_mirror.hpp`
- Test: `tests/test_field_mirror.cpp`

**Interfaces:**
- Consumes: `prism::core::fixed_string<N>` (Task 1).
- Produces (all in `namespace prism::inspector`, inside `#if __cpp_impl_reflection`):
  - `constexpr inline struct {} skip{};`
  - `constexpr inline struct {} readonly{};`
  - `template <fixed_string S> struct label_t { static constexpr auto value = S; };`
  - `template <fixed_string S> struct section_t { static constexpr auto value = S; };`
  - `template <fixed_string S> constexpr inline label_t<S> label{};`
  - `template <fixed_string S> constexpr inline section_t<S> section{};`
  - `template <std::meta::info M, typename Tag> consteval bool has_annotation();`
  - `template <template <fixed_string> class Templ> consteval bool is_specialization_of(std::meta::info t);`
  - `template <std::meta::info M, template <fixed_string> class Templ> consteval std::string_view extract_string_annotation();`

This task adds pure infrastructure — no change to `field_mirror_tuple_info`/`sync_from`/etc. yet. The test is a compile-time-only check (a `static_assert`-driven `TEST_CASE`) proving the helpers correctly read annotations off a throwaway local struct, independent of `FieldMirror`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_field_mirror.cpp` (inside the existing `#if __cpp_impl_reflection` block, after the includes):

```cpp
struct AnnotationProbe {
    [[=prism::inspector::skip]] int a;
    [[=prism::inspector::readonly]] int b;
    [[=prism::inspector::label<"Custom Name">]] int c;
    [[=prism::inspector::section<"Audio">]] int d;
    int e;
};

TEST_CASE("annotation helpers detect skip/readonly and extract label/section text") {
    static_assert(prism::inspector::has_annotation<^^AnnotationProbe::a, decltype(prism::inspector::skip)>());
    static_assert(!prism::inspector::has_annotation<^^AnnotationProbe::e, decltype(prism::inspector::skip)>());

    static_assert(prism::inspector::has_annotation<^^AnnotationProbe::b, decltype(prism::inspector::readonly)>());
    static_assert(!prism::inspector::has_annotation<^^AnnotationProbe::e, decltype(prism::inspector::readonly)>());

    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::c, prism::inspector::label_t>() == "Custom Name");
    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::e, prism::inspector::label_t>().empty());

    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::d, prism::inspector::section_t>() == "Audio");
    static_assert(prism::inspector::extract_string_annotation<^^AnnotationProbe::e, prism::inspector::section_t>().empty());

    CHECK(true); // presence of this TEST_CASE proves the file compiled with the static_asserts above
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir test_field_mirror 2>&1 | tail -40`
Expected: FAIL to compile — `'skip' is not a member of 'prism::inspector'` (or equivalent) since none of the annotation symbols exist yet.

- [ ] **Step 3: Write minimal implementation**

In `include/prism/widgets/field_mirror.hpp`, add `#include <prism/core/fixed_string.hpp>` to the includes, and insert the following inside `namespace prism::inspector { ... }`, immediately after the `using namespace prism::ui;` line and before the existing `MirrorLeaf` concept:

```cpp
// --- Annotations ---------------------------------------------------------
// Attach to a member of a plain struct passed to Inspector<T>/FieldMirror<T>:
//
//   struct Settings {
//       [[=prism::inspector::skip]]                  int internal_version;
//       [[=prism::inspector::readonly]]               std::string device_id;
//       [[=prism::inspector::label<"Sample Rate">]]   int sample_rate;
//       [[=prism::inspector::section<"Audio">]]       float volume;
//   };

constexpr inline struct {} skip{};
constexpr inline struct {} readonly{};

template <fixed_string S> struct label_t   { static constexpr auto value = S; };
template <fixed_string S> struct section_t { static constexpr auto value = S; };
template <fixed_string S> constexpr inline label_t<S>   label{};
template <fixed_string S> constexpr inline section_t<S> section{};

template <std::meta::info M, typename Tag>
consteval bool has_annotation() {
    static constexpr auto annots = std::define_static_array(std::meta::annotations_of(M));
    for (auto a : annots) {
        if (std::meta::type_of(a) == ^^Tag) return true;
    }
    return false;
}

template <template <fixed_string> class Templ>
consteval bool is_specialization_of(std::meta::info t) {
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^Templ;
}

template <std::meta::info M, template <fixed_string> class Templ>
consteval std::string_view extract_string_annotation() {
    static constexpr auto annots = std::define_static_array(std::meta::annotations_of(M));
    template for (constexpr auto a : annots) {
        constexpr auto t = std::meta::type_of(a);
        if constexpr (is_specialization_of<Templ>(t)) {
            return [:t:]::value.view();
        }
    }
    return {};
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_field_mirror && ./builddir/test_field_mirror`
Expected: PASS, all existing `TEST_CASE`s plus the new one green.

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: add annotation detection helpers for FieldMirror"
```

---

### Task 3: `skip` — `HiddenSlot<M>`, wired through the whole mirror

**Files:**
- Modify: `include/prism/widgets/field_mirror.hpp`
- Test: `tests/test_field_mirror.cpp`

**Interfaces:**
- Consumes: `has_annotation<M, Tag>()` (Task 2).
- Produces: `template <typename M> struct HiddenSlot { Field<M> value{}; };`, `is_hidden_slot_v<T>`.

This is the structural task: `field_mirror_tuple_info`, `sync_from`, `for_each_leaf`, and `view` all need to become hidden-slot-aware simultaneously, since a skipped member changes which branch each of them takes. `build()` needs no change — `HiddenSlot<M>` and `LeafSlot<M>` both expose `.value`, so `build()`'s existing non-nested branch (`out.[:m:] = slot.value.get();`) already covers both without modification. This is also the task that removes the now-unused `MirrorSlot` alias (nothing outside this file references it — verified via `grep -rn "MirrorSlot\b"`).

**Correctness requirement:** a skipped member must still round-trip through `build()` unchanged when other fields are edited — it must never be silently reset to its default value. The test below exercises exactly this.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_field_mirror.cpp`:

```cpp
struct DeviceStateWithSkip {
    float voltage;
    [[=prism::inspector::skip]] int internal_version;
    bool enabled;
};

TEST_CASE("skip excludes a member from for_each_leaf but preserves it through build()") {
    prism::inspector::FieldMirror<DeviceStateWithSkip> mirror;
    DeviceStateWithSkip d{3.3f, 42, true};
    mirror.sync_from(d);

    // for_each_leaf must not visit the skipped member.
    int count = 0;
    mirror.for_each_leaf([&](auto&) { ++count; });
    CHECK(count == 2); // voltage, enabled -- not internal_version

    // Editing an unrelated field and rebuilding must not reset internal_version.
    std::get<0>(mirror.slots).value.set(9.9f);
    DeviceStateWithSkip rebuilt = mirror.build();
    CHECK(rebuilt.voltage == doctest::Approx(9.9f));
    CHECK(rebuilt.internal_version == 42); // preserved, not reset to 0
    CHECK(rebuilt.enabled == true);
}

TEST_CASE("skip removes the member's widgets from the generated tree") {
    prism::inspector::FieldMirror<DeviceStateWithSkip> mirror;
    mirror.sync_from(DeviceStateWithSkip{1.f, 7, false});
    prism::WidgetTree tree(mirror);
    // voltage (2: name+value) + enabled (2: name+value) = 4. internal_version: 0.
    CHECK(tree.leaf_count() == 4);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir test_field_mirror 2>&1 | tail -40`
Expected: FAIL — either a compile error (`[[=prism::inspector::skip]]` has no effect yet, so nothing is broken syntactically, but `for_each_leaf` visits all 3 members) or `count == 3` / `leaf_count() == 6`, not the expected 2/4.

- [ ] **Step 3: Write minimal implementation**

Replace the whole body of `include/prism/widgets/field_mirror.hpp` from the `MirrorLeaf` concept through the end of the `FieldMirror` struct (i.e. everything after the annotation helpers added in Task 2, before the closing `} // namespace prism::inspector`) with:

```cpp
// --- Slot shapes -----------------------------------------------------------

template <typename T>
concept MirrorLeaf = Numeric<T> || StringLike<T> || ScopedEnum<T>;

// One labeled row: a static name caption + the live editable value.
template <typename M>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};
};

// A member excluded from rendering ([[=prism::inspector::skip]]). Still
// round-trips through sync_from/build so unrelated edits don't reset it.
template <typename M>
struct HiddenSlot {
    Field<M> value{};
};

template <typename T> struct is_hidden_slot : std::false_type {};
template <typename M> struct is_hidden_slot<HiddenSlot<M>> : std::true_type {};
template <typename T> inline constexpr bool is_hidden_slot_v = is_hidden_slot<T>::value;

template <typename T>
concept NestedMirrorSlot = requires(T& t) { t.slots; };

template <typename T> struct FieldMirror;

template <typename T>
consteval std::meta::info field_mirror_tuple_info() {
    std::vector<std::meta::info> slot_types;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        constexpr auto mtype = std::meta::type_of(m);
        using M = typename [:mtype:];
        if constexpr (has_annotation<m, decltype(skip)>()) {
            slot_types.push_back(std::meta::substitute(^^HiddenSlot, {mtype}));
        } else if constexpr (MirrorLeaf<M>) {
            slot_types.push_back(std::meta::substitute(^^LeafSlot, {mtype}));
        } else {
            slot_types.push_back(std::meta::substitute(^^FieldMirror, {mtype}));
        }
    }
    return std::meta::substitute(^^std::tuple, slot_types);
}

template <typename T>
using FieldMirrorTuple = [: field_mirror_tuple_info<T>() :];

template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;

    void sync_from(const T& v) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.sync_from(v.[:m:]);
            } else if constexpr (is_hidden_slot_v<SlotT>) {
                slot.value.set(v.[:m:]);
            } else {
                slot.name.set(Label<std::string>{std::string(std::meta::identifier_of(m))});
                slot.value.set(v.[:m:]);
            }
        }
    }

    T build() const {
        T out{};
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            const auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                out.[:m:] = slot.build();
            } else {
                out.[:m:] = slot.value.get();
            }
        }
        return out;
    }

    template <typename Fn>
    void for_each_leaf(Fn&& fn) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.for_each_leaf(fn);
            } else if constexpr (is_hidden_slot_v<SlotT>) {
                // invisible -- no widget exists for it, so it can never receive a local edit.
            } else {
                fn(slot.value);
            }
        }
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            using SlotT = std::remove_cvref_t<decltype(std::get<i>(slots))>;
            if constexpr (!is_hidden_slot_v<SlotT>) {
                vb.component(std::get<i>(slots));
            }
        }
    }
};
```

Note: `LeafSlot<M>` no longer gets an explicit `view()` method in this task — it's still picked up by the generic component-reflection fallback in `build_node_tree` (`widget_tree.hpp:920-946`), identical to before. `view()` above changed only in *how* it iterates (`template for` over indices instead of `std::apply` over a fold expression) so it can skip hidden slots; the `vb.component(std::get<i>(slots))` call for each visible slot produces the exact same tree shape as the old `vb.vstack(s...)` did.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_field_mirror && ./builddir/test_field_mirror`
Expected: PASS, all `TEST_CASE`s green including the two new ones. Confirm the *existing* tests (`FieldMirror seeds leaf values...`, `for_each_leaf visits every leaf exactly once`, `is a WidgetTree component...`, etc.) still pass unchanged — this proves the rewrite is behavior-preserving for structs with no annotations.

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: add skip annotation via HiddenSlot, round-trips through build()"
```

---

### Task 4: `label` — override the name caption

**Files:**
- Modify: `include/prism/widgets/field_mirror.hpp`
- Test: `tests/test_field_mirror.cpp`

**Interfaces:**
- Consumes: `extract_string_annotation<M, label_t>()` (Task 2).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_field_mirror.cpp`:

```cpp
struct DeviceStateWithLabel {
    [[=prism::inspector::label<"Sample Rate (Hz)">]] int sample_rate;
    bool enabled;
};

TEST_CASE("label overrides the name caption instead of using identifier_of") {
    prism::inspector::FieldMirror<DeviceStateWithLabel> mirror;
    mirror.sync_from(DeviceStateWithLabel{44100, true});

    CHECK(std::get<0>(mirror.slots).name.get().value == "Sample Rate (Hz)");
    CHECK(std::get<1>(mirror.slots).name.get().value == "enabled"); // unannotated: falls back to identifier_of
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir test_field_mirror 2>&1 | tail -20`
Expected: FAIL — `CHECK(... == "Sample Rate (Hz)")` gets `"sample_rate"` instead (the `label` annotation is parsed but not yet consulted by `sync_from`).

- [ ] **Step 3: Write minimal implementation**

In `include/prism/widgets/field_mirror.hpp`, in `FieldMirror<T>::sync_from`, replace:

```cpp
            } else {
                slot.name.set(Label<std::string>{std::string(std::meta::identifier_of(m))});
                slot.value.set(v.[:m:]);
            }
```

with:

```cpp
            } else {
                constexpr auto override_label = extract_string_annotation<m, label_t>();
                if constexpr (!override_label.empty()) {
                    slot.name.set(Label<std::string>{std::string(override_label)});
                } else {
                    slot.name.set(Label<std::string>{std::string(std::meta::identifier_of(m))});
                }
                slot.value.set(v.[:m:]);
            }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_field_mirror && ./builddir/test_field_mirror`
Expected: PASS, all tests green.

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: add label annotation to override FieldMirror name captions"
```

---

### Task 5: `section` — header row before grouped members

**Files:**
- Modify: `include/prism/widgets/field_mirror.hpp`
- Test: `tests/test_field_mirror.cpp`

**Interfaces:**
- Consumes: `extract_string_annotation<M, section_t>()` (Task 2).
- Produces: `FieldMirror<T>::section_headers` (public member, `std::array<Field<Label<std::string>>, N>`), `field_mirror_member_count<T>()`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_field_mirror.cpp`:

```cpp
struct DeviceStateWithSection {
    float voltage;
    [[=prism::inspector::section<"Audio">]] float volume;
    bool enabled;
};

TEST_CASE("section stores a header title at the annotated member's index") {
    prism::inspector::FieldMirror<DeviceStateWithSection> mirror;
    mirror.sync_from(DeviceStateWithSection{1.f, 2.f, true});

    CHECK(mirror.section_headers[0].get().value.empty());
    CHECK(mirror.section_headers[1].get().value == "Audio");
    CHECK(mirror.section_headers[2].get().value.empty());
}

TEST_CASE("section inserts one extra header widget into the generated tree") {
    prism::inspector::FieldMirror<DeviceStateWithSection> mirror;
    mirror.sync_from(DeviceStateWithSection{1.f, 2.f, true});
    prism::WidgetTree tree(mirror);
    // voltage(2) + [header(1) + volume(2)] + enabled(2) = 7
    CHECK(tree.leaf_count() == 7);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir test_field_mirror 2>&1 | tail -20`
Expected: FAIL to compile — `FieldMirror<T>` has no member `section_headers` yet.

- [ ] **Step 3: Write minimal implementation**

In `include/prism/widgets/field_mirror.hpp`, add this helper directly above `field_mirror_tuple_info`:

```cpp
template <typename T>
consteval std::size_t field_mirror_member_count() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
}
```

Then modify the `FieldMirror<T>` struct: add the `section_headers` member, a constructor that populates it, and update `view()` to insert a header widget before any slot whose section title is non-empty.

Replace:

```cpp
template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;

    void sync_from(const T& v) {
```

with:

```cpp
template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;
    std::array<Field<Label<std::string>>, field_mirror_member_count<T>()> section_headers{};

    FieldMirror() {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            constexpr auto title = extract_string_annotation<m, section_t>();
            if constexpr (!title.empty()) {
                section_headers[i].set(Label<std::string>{std::string(title)});
            }
        }
    }

    void sync_from(const T& v) {
```

And replace the `view()` method body:

```cpp
    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            using SlotT = std::remove_cvref_t<decltype(std::get<i>(slots))>;
            if constexpr (!is_hidden_slot_v<SlotT>) {
                vb.component(std::get<i>(slots));
            }
        }
    }
```

with:

```cpp
    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            using SlotT = std::remove_cvref_t<decltype(std::get<i>(slots))>;
            if constexpr (!is_hidden_slot_v<SlotT>) {
                constexpr auto title = extract_string_annotation<m, section_t>();
                if constexpr (!title.empty()) {
                    vb.widget(section_headers[i]);
                }
                vb.component(std::get<i>(slots));
            }
        }
    }
```

Add `#include <array>` to the top-level includes of `field_mirror.hpp` if not already present via a transitive include (check first; add explicitly if the build fails without it).

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_field_mirror && ./builddir/test_field_mirror`
Expected: PASS, all tests green.

- [ ] **Step 5: Commit**

```bash
git add include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: add section annotation, renders a header row before grouped members"
```

---

### Task 6: `readonly` — non-interactive value rendering

**Files:**
- Modify: `include/prism/app/widget_tree.hpp` (new `ViewBuilder` method)
- Modify: `include/prism/widgets/field_mirror.hpp` (`LeafSlot<M, bool>`, `field_mirror_tuple_info` branch)
- Test: `tests/test_field_mirror.cpp`

**Interfaces:**
- Consumes: `node_readonly_leaf<T>(Observable&, WidgetId&)` (existing, `widget_node.hpp:160-194`); `has_annotation<M, Tag>()` (Task 2).
- Produces: `ViewBuilder::widget_readonly(Field<T>&)`; `LeafSlot<M, bool ReadOnly = false>` (now with an explicit `view()`).

**Test scoping note:** the original spec described testing this via "`handle_input` is a no-op." The codebase's existing `FieldMirror`/`Inspector` tests never drill into `WidgetNode`/`focus_policy`/`handle_input` — that machinery is only reachable after a `Node` is realized into a live `WidgetNode`, which none of the existing test infrastructure in `test_field_mirror.cpp` does. Building that scaffolding is out of scope for this feature. Instead, this task verifies the thing that's actually load-bearing: (a) the annotation routes to the correct slot *type* (`LeafSlot<M, true>`, statically asserted), and (b) that type's `view()` calls `widget_readonly` and not `widget` for the value — which is what determines whether `node_readonly_leaf` (no `wire`, `focus_policy::none`) or `node_leaf` (full input wiring) gets built. `node_readonly_leaf` itself is exercised by the existing `Derived<T>`/`Shared<T>` tests elsewhere in the suite, so its behavior isn't new-and-unverified — only its reachability from a plain `Field<T>` is new here.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_field_mirror.cpp`:

```cpp
struct DeviceStateWithReadonly {
    [[=prism::inspector::readonly]] std::string device_id;
    float voltage;
};

TEST_CASE("readonly routes the member to LeafSlot<M, true>") {
    using Tup = prism::inspector::FieldMirrorTuple<DeviceStateWithReadonly>;
    static_assert(std::is_same_v<std::tuple_element_t<0, Tup>,
                                  prism::inspector::LeafSlot<std::string, true>>);
    static_assert(std::is_same_v<std::tuple_element_t<1, Tup>,
                                  prism::inspector::LeafSlot<float, false>>);

    // Data still flows through a readonly slot exactly like any other leaf --
    // "readonly" only changes how it renders, not whether it holds a value.
    prism::inspector::FieldMirror<DeviceStateWithReadonly> mirror;
    mirror.sync_from(DeviceStateWithReadonly{"dev-42", 3.3f});
    CHECK(std::get<0>(mirror.slots).value.get() == "dev-42");
}

TEST_CASE("readonly still produces the same leaf count as an editable member") {
    prism::inspector::FieldMirror<DeviceStateWithReadonly> mirror;
    mirror.sync_from(DeviceStateWithReadonly{"dev-42", 3.3f});
    prism::WidgetTree tree(mirror);
    // device_id (name+value=2) + voltage (name+value=2) = 4
    CHECK(tree.leaf_count() == 4);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir test_field_mirror 2>&1 | tail -40`
Expected: FAIL to compile — `prism::inspector::LeafSlot` doesn't yet take a second template parameter, and `field_mirror_tuple_info` doesn't yet branch on `readonly`.

- [ ] **Step 3: Write minimal implementation**

First, in `include/prism/app/widget_tree.hpp`, add a new method immediately after the existing `widget(Field<T>&)` overload (the one at what is currently lines 80-84):

```cpp
        template <typename T>
        void widget(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(node_leaf(field, tree_.next_id_));
        }

        // Renders like widget(Field<T>&) but without focus/input wiring --
        // for prism::inspector's [[=prism::inspector::readonly]] annotation.
        template <typename T>
        void widget_readonly(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(node_readonly_leaf<T>(field, tree_.next_id_));
        }
```

Then, in `include/prism/widgets/field_mirror.hpp`, replace the `LeafSlot` definition:

```cpp
// One labeled row: a static name caption + the live editable value.
template <typename M>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};
};
```

with:

```cpp
// One labeled row: a static name caption + the live value. ReadOnly=true
// renders the value without focus/input wiring ([[=prism::inspector::readonly]]).
template <typename M, bool ReadOnly = false>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(name);
        if constexpr (ReadOnly) vb.widget_readonly(value);
        else vb.widget(value);
    }
};
```

Then, in `field_mirror_tuple_info`, replace:

```cpp
        } else if constexpr (MirrorLeaf<M>) {
            slot_types.push_back(std::meta::substitute(^^LeafSlot, {mtype}));
        } else {
            slot_types.push_back(std::meta::substitute(^^FieldMirror, {mtype}));
        }
```

with:

```cpp
        } else if constexpr (MirrorLeaf<M>) {
            constexpr bool ro = has_annotation<m, decltype(readonly)>();
            slot_types.push_back(std::meta::substitute(
                ^^LeafSlot, {mtype, std::meta::reflect_constant(ro)}));
        } else {
            static_assert(!has_annotation<m, decltype(readonly)>(),
                "[[=prism::inspector::readonly]] is only supported on leaf members "
                "(numeric, string-like, or scoped enum) -- not on nested structs");
            slot_types.push_back(std::meta::substitute(^^FieldMirror, {mtype}));
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C builddir test_field_mirror && ./builddir/test_field_mirror`
Expected: PASS, all tests green. Also re-run `test_inspector` (`ninja -C builddir test_inspector && ./builddir/test_inspector`) since `Inspector<T>` builds directly on `FieldMirror<T>` and this task touched `LeafSlot`'s shape — confirm no regression.

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp include/prism/widgets/field_mirror.hpp tests/test_field_mirror.cpp
git commit -m "feat: add readonly annotation via LeafSlot<M, bool> and ViewBuilder::widget_readonly"
```

---

### Task 7: Full suite verification and memory update

**Files:** none (verification + memory only)

- [ ] **Step 1: Run the complete test suite**

Run: `ninja -C builddir test 2>&1 | tail -60`
Expected: every test binary passes, 0 failures. Read the actual pass/fail counts printed by doctest for each binary — do not infer success from exit code alone.

- [ ] **Step 2: Update project memory**

Update `/home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/project-live-inspector.md` (or add a new memory file if that one doesn't cover this) to record: `Inspector<T>`/`FieldMirror<T>` now supports four field annotations (`skip`, `readonly`, `label<"...">`, `section<"...">`) via `std::meta::annotations_of()`; annotation call syntax is NTTP-style (`label<"text">`, not `label("text")`) because GCC 16 rejects a `consteval` factory function taking `const char(&)[N]` inside an annotation evaluation context. Add a pointer line to `MEMORY.md`.

- [ ] **Step 3: Final commit if memory changed**

```bash
git add /home/jeandet/.claude/projects/-var-home-jeandet-Documents-prog-PRSIM/memory/
git commit -m "docs: record Inspector<T> field annotations in project memory"
```
