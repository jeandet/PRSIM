# Table Reflection Tiers + List/Depends-On Ergonomics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Table a plain-struct row tier and an auto-detected struct-of-vectors (SOA) tier, add `List<T>::replace_all` and a variadic `depends_on`, then migrate `examples/model_system_monitor.cpp` to use all four — eliminating the `ProcessRow` twin struct, the manual row-diff loop, and the repeated 5-call `depends_on` chains.

**Architecture:** Two small new headers under `include/prism/core/` (`reflect_annotations.hpp`, `reflect_leaf.hpp`) get extracted from existing, already-shipped code (`field_mirror.hpp`'s annotation block, `tree.hpp`'s inline leaf classification) so Table's new reflection tiers can use the same skip/label annotation vocabulary and leaf classification without depending on the Inspector widget machinery. `table.hpp`'s `wrap_row_storage` grows a second branch for plain structs; a new `SoaStorage` concept plus `wrap_soa_columns` gives Table a third `ViewBuilder::table(...)` overload for structs of vectors. `core/list.hpp` gets one new method; `widget_tree.hpp`'s `DependsOnMixin` gets one new overload.

**Tech Stack:** C++26 (experimental reflection, gated by `#if __cpp_impl_reflection`), doctest, Meson.

**Spec:** `docs/superpowers/specs/2026-07-23-table-reflection-tiers-design.md`

## Global Constraints

- All reflection-based code stays inside `#if __cpp_impl_reflection` guards, matching every existing reflection-gated file in this codebase.
- Tasks 1 and 2 are refactors: the existing test suites they touch (`prism:field_mirror`, `prism:tree_reflect`) must pass **unchanged** — no test assertions in those files are edited, only added to.
- New/changed headers follow the codebase's existing convention: every file under `include/prism/core/` uses `namespace prism::core`; every file under `include/prism/ui/` uses `namespace prism::ui` with `using namespace prism::core;` already present at the top (confirmed in both `tree.hpp:23-24` and `table.hpp:20-21`) — this is why code in Tasks 2, 5, and 6 can reference `LeafType`, `skip`, `label_t`, etc. unqualified with no extra `using` needed.
- Build: `ninja -C builddir`. Run a single test binary: `meson test -C builddir <name> -v` (e.g. `meson test -C builddir field_mirror -v`). All existing test binaries referenced below are already registered in `tests/meson.build` — no meson.build edits are needed anywhere in this plan (every task only adds `TEST_CASE`s to already-registered files).
- Commit after each task, following this repo's commit message style (short imperative summary, no body needed for mechanical steps).

**Note on one deliberate deviation from the spec doc:** the spec (`docs/superpowers/specs/2026-07-23-table-reflection-tiers-design.md`) names the new shared namespace `prism::reflect`. While digging into the exact files for this plan, every other file under `include/prism/core/` uses `namespace prism::core` (not a topic-specific sub-namespace) — `traits.hpp`, `list.hpp`, `reflect.hpp`, `fixed_string.hpp` all do. Task 1 and Task 2 below put the new code in `prism::core` instead, to match that established convention. Nothing about the approved design's behavior changes — only the namespace name. Flagging this so it's not a silent surprise; the spec doc's namespace name is stale after this plan runs.

---

### Task 1: Extract annotation helpers into `prism::core::reflect_annotations`

**Files:**
- Create: `include/prism/core/reflect_annotations.hpp`
- Modify: `include/prism/widgets/field_mirror.hpp:1-63`
- Modify: `include/prism/core/fixed_string.hpp:9-10`
- Test: `tests/test_field_mirror.cpp` (existing tests must still pass unchanged; one new test added)

**Interfaces:**
- Produces: `prism::core::skip`, `prism::core::readonly` (anonymous-struct-typed constants), `prism::core::label_t<S>`/`prism::core::section_t<S>` (class templates), `prism::core::label<S>`/`prism::core::section<S>` (variable templates), `prism::core::has_annotation<M, Tag>()`, `prism::core::extract_string_annotation<M, Templ>()` (consteval function templates, `M` is a `std::meta::info`). Consumed by Task 5 and Task 6.
- Consumes: nothing new — this is a pure relocation of code that already exists in `field_mirror.hpp:31-63`.

- [ ] **Step 1: Confirm the baseline passes before touching anything**

Run: `ninja -C builddir && meson test -C builddir field_mirror -v`
Expected: all `TEST_CASE`s in `tests/test_field_mirror.cpp` PASS (this is the regression baseline Task 1 must not break).

- [ ] **Step 2: Create `include/prism/core/reflect_annotations.hpp`**

```cpp
#pragma once

#include <prism/core/fixed_string.hpp>

#if __cpp_impl_reflection
#include <meta>

namespace prism::core {

// --- Annotations ---------------------------------------------------------
// Attach to a member of a plain reflected struct:
//
//   struct Settings {
//       [[=prism::core::skip]]                  int internal_version;
//       [[=prism::core::readonly]]              std::string device_id;
//       [[=prism::core::label<"Sample Rate">]]  int sample_rate;
//       [[=prism::core::section<"Audio">]]      float volume;
//   };
//
// prism::inspector still exposes these under their original names
// (prism::inspector::skip, etc.) via the `using namespace prism::core;`
// already present in field_mirror.hpp -- no call site there changes.

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

} // namespace prism::core

#endif // __cpp_impl_reflection
```

- [ ] **Step 3: Remove the relocated block from `field_mirror.hpp` and include the new header**

In `include/prism/widgets/field_mirror.hpp`, replace lines 1-63 (everything from the top through the closing brace of `extract_string_annotation`, i.e. up to and including the blank line before `// --- Slot shapes`) with:

```cpp
#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/fixed_string.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/ui/delegate.hpp>

#include <array>
#include <tuple>
#include <type_traits>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#include <ranges>

namespace prism::inspector {
using namespace prism::core;
using namespace prism::ui;

// skip/readonly/label/section/has_annotation/extract_string_annotation now live in
// prism::core::reflect_annotations.hpp; the `using namespace prism::core;` above is
// what still makes prism::inspector::skip etc. resolve for every existing call site.

// --- Slot shapes -----------------------------------------------------------
```

The file from `template <typename T> concept MirrorLeaf = ...` (originally line 68) onward is unchanged.

- [ ] **Step 4: Update the stale comment in `fixed_string.hpp`**

In `include/prism/core/fixed_string.hpp`, change:

```cpp
// Needed because C++26 annotations ([[=expr]]) must be constants of
// structural type, and prism::inspector::label/section carry a string.
```

to:

```cpp
// Needed because C++26 annotations ([[=expr]]) must be constants of
// structural type, and prism::core::label/section (see
// reflect_annotations.hpp) carry a string.
```

- [ ] **Step 5: Rebuild and confirm the existing suite still passes unchanged**

Run: `ninja -C builddir && meson test -C builddir field_mirror -v`
Expected: identical PASS result to Step 1 — same test count, all green. This proves the relocation is behavior-preserving.

- [ ] **Step 6: Add one small test proving the relocation, not just the re-export**

Append to `tests/test_field_mirror.cpp`, inside the existing `#if __cpp_impl_reflection` block (anywhere after the existing includes, e.g. right after the `AnnotationProbe`/`has_annotation` test block):

```cpp
struct CoreDirectProbe {
    [[=prism::core::skip]] int a;
    [[=prism::core::label<"Direct">]] int b;
};

TEST_CASE("annotations are directly usable via prism::core, not only prism::inspector") {
    static_assert(prism::core::has_annotation<^^CoreDirectProbe::a, decltype(prism::core::skip)>());
    static_assert(prism::core::extract_string_annotation<^^CoreDirectProbe::b,
                                                           prism::core::label_t>() == "Direct");
    CHECK(true);
}
```

- [ ] **Step 7: Run the test, confirm it passes alongside the untouched suite**

Run: `ninja -C builddir && meson test -C builddir field_mirror -v`
Expected: PASS, one more test case than Step 1's baseline.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/reflect_annotations.hpp include/prism/widgets/field_mirror.hpp include/prism/core/fixed_string.hpp tests/test_field_mirror.cpp
git commit -m "refactor: extract Inspector annotation helpers into prism::core::reflect_annotations"
```

---

### Task 2: Extract leaf classification into `prism::core::reflect_leaf`, refactor Tree to use it

**Files:**
- Create: `include/prism/core/reflect_leaf.hpp`
- Modify: `include/prism/ui/tree.hpp:9-21` (include), `include/prism/ui/tree.hpp:366-373` (classification branches)
- Test: `tests/test_tree_reflect.cpp` (existing tests must still pass unchanged); new test file section for `LeafType`/`format_leaf_value` directly

**Interfaces:**
- Produces: `prism::core::LeafType<T>` (concept: `std::is_arithmetic_v<T> || std::is_same_v<T, std::string> || std::is_enum_v<T>`), `prism::core::format_leaf_value(v)` (function template, `LeafType<T>`-constrained, returns `std::string`). Consumed by Task 5 and Task 6.
- Consumes: nothing new — extracted verbatim from `tree.hpp:366-373`'s existing three `if constexpr` branches.

- [ ] **Step 1: Confirm the baseline passes**

Run: `ninja -C builddir && meson test -C builddir tree_reflect -v`
Expected: all existing `TEST_CASE`s in `tests/test_tree_reflect.cpp` PASS.

- [ ] **Step 2: Create `include/prism/core/reflect_leaf.hpp`**

```cpp
#pragma once

#include <fmt/format.h>
#include <string>
#include <type_traits>
#include <utility>

namespace prism::core {

// A "leaf" is any reflected member representable as one formatted value --
// never a tree row or table column of its own. Matches the classification
// tree.hpp already used inline (both scoped and unscoped enums) -- this is
// deliberately NOT prism::inspector::MirrorLeaf, whose ScopedEnum excludes
// plain `enum`. That narrower rule is Inspector-specific and stays as-is;
// this concept exists so Tree's existing behavior can be reused verbatim by
// Table's new reflection tiers without silently narrowing enum support.
template <typename T>
concept LeafType = std::is_arithmetic_v<T> || std::is_same_v<T, std::string>
                    || std::is_enum_v<T>;

template <LeafType T>
std::string format_leaf_value(const T& v) {
    if constexpr (std::is_same_v<T, std::string>) return v;
    else if constexpr (std::is_enum_v<T>) return fmt::to_string(std::to_underlying(v));
    else return fmt::to_string(v);
}

} // namespace prism::core
```

- [ ] **Step 3: Add the include to `tree.hpp`**

In `include/prism/ui/tree.hpp`, add to the include list (near the other `<prism/core/...>` includes at the top, e.g. right after line 4 `#include <prism/core/list.hpp>`):

```cpp
#include <prism/core/reflect_leaf.hpp>
```

- [ ] **Step 4: Replace the three inline branches with the shared helper**

In `include/prism/ui/tree.hpp`, inside `populate_struct_tree_cache` (around lines 366-373), replace:

```cpp
        } else if constexpr (std::is_arithmetic_v<M>) {
            entry.attributes.emplace_back(std::string(member_name), fmt::to_string(member));
        } else if constexpr (std::is_same_v<M, std::string>) {
            entry.attributes.emplace_back(std::string(member_name), member);
        } else if constexpr (std::is_enum_v<M>) {
            entry.attributes.emplace_back(std::string(member_name),
                                           fmt::to_string(std::to_underlying(member)));
        }
```

with:

```cpp
        } else if constexpr (LeafType<M>) {
            entry.attributes.emplace_back(std::string(member_name), format_leaf_value(member));
        }
```

- [ ] **Step 5: Rebuild and confirm the existing suite still passes unchanged**

Run: `ninja -C builddir && meson test -C builddir tree_reflect -v`
Expected: identical PASS result to Step 1 — this is a behavior-preserving extraction, not a behavior change.

- [ ] **Step 6: Add a direct unit test for `LeafType`/`format_leaf_value`, including the unscoped-enum case Tree's own test file doesn't cover**

Append to `tests/test_tree_reflect.cpp`, inside the existing `#if __cpp_impl_reflection` block:

```cpp
namespace {
enum UnscopedColor { Red, Green }; // plain enum, NOT enum class
}

TEST_CASE("LeafType/format_leaf_value: arithmetic, string, scoped and unscoped enum") {
    static_assert(prism::LeafType<int>);
    static_assert(prism::LeafType<double>);
    static_assert(prism::LeafType<std::string>);
    static_assert(prism::LeafType<Color>);           // enum class, defined earlier in this file
    static_assert(prism::LeafType<UnscopedColor>);   // plain enum -- excluded by MirrorLeaf,
                                                       // included here (matches Tree's prior behavior)
    static_assert(!prism::LeafType<std::vector<int>>);

    CHECK(prism::format_leaf_value(42) == "42");
    CHECK(prism::format_leaf_value(std::string("hi")) == "hi");
    CHECK(prism::format_leaf_value(Color::Green) == "1");
    CHECK(prism::format_leaf_value(UnscopedColor::Green) == "1");
}
```

(`Color` is the `enum class Color { Red, Green };` already declared earlier in this file for the existing `"wrap_struct_tree collects enum members..."` test — no new declaration needed for it.)

- [ ] **Step 7: Run the full file, confirm everything passes**

Run: `ninja -C builddir && meson test -C builddir tree_reflect -v`
Expected: PASS, one more test case than Step 1's baseline.

- [ ] **Step 8: Commit**

```bash
git add include/prism/core/reflect_leaf.hpp include/prism/ui/tree.hpp tests/test_tree_reflect.cpp
git commit -m "refactor: extract Tree's leaf classification into prism::core::reflect_leaf"
```

---

### Task 3: `List<T>::replace_all`

**Files:**
- Modify: `include/prism/core/list.hpp:24-27` (add method after `set`)
- Test: `tests/test_list.cpp`

**Interfaces:**
- Produces: `void List<T>::replace_all(std::ranges::range auto&& new_values)`. Consumed by Task 7.
- Consumes: `List<T>`'s existing `size()`, `erase(size_t)`, `set(size_t, T)`, `push_back(T)` — all already present at `list.hpp:14-27`, unchanged.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_list.cpp`:

```cpp
TEST_CASE("List::replace_all shrinks: overlapping indices update, extra rows removed") {
    prism::List<int> list;
    list.push_back(1);
    list.push_back(2);
    list.push_back(3);

    std::vector<size_t> updated_indices;
    std::vector<size_t> removed_indices;
    auto conn_u = list.on_update().connect([&](size_t i, const int&) { updated_indices.push_back(i); });
    auto conn_r = list.on_remove().connect([&](size_t i) { removed_indices.push_back(i); });

    list.replace_all(std::vector<int>{10, 20});

    CHECK(list.size() == 2);
    CHECK(list[0] == 10);
    CHECK(list[1] == 20);
    CHECK(updated_indices == std::vector<size_t>{0, 1});
    CHECK(removed_indices == std::vector<size_t>{2}); // only the size delta
}

TEST_CASE("List::replace_all grows: overlapping indices update, extra rows inserted") {
    prism::List<int> list;
    list.push_back(1);

    std::vector<size_t> updated_indices;
    std::vector<size_t> inserted_indices;
    auto conn_u = list.on_update().connect([&](size_t i, const int&) { updated_indices.push_back(i); });
    auto conn_i = list.on_insert().connect([&](size_t i, const int&) { inserted_indices.push_back(i); });

    list.replace_all(std::vector<int>{100, 200, 300});

    CHECK(list.size() == 3);
    CHECK(list[0] == 100);
    CHECK(list[1] == 200);
    CHECK(list[2] == 300);
    CHECK(updated_indices == std::vector<size_t>{0});
    CHECK(inserted_indices == std::vector<size_t>{1, 2});
}

TEST_CASE("List::replace_all with same size only updates, never inserts or removes") {
    prism::List<std::string> list;
    list.push_back("a");
    list.push_back("b");

    int inserts = 0, removes = 0, updates = 0;
    auto conn_i = list.on_insert().connect([&](size_t, const std::string&) { ++inserts; });
    auto conn_r = list.on_remove().connect([&](size_t) { ++removes; });
    auto conn_u = list.on_update().connect([&](size_t, const std::string&) { ++updates; });

    list.replace_all(std::vector<std::string>{"x", "y"});

    CHECK(list[0] == "x");
    CHECK(list[1] == "y");
    CHECK(inserts == 0);
    CHECK(removes == 0);
    CHECK(updates == 2);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ninja -C builddir && meson test -C builddir list -v`
Expected: FAIL to compile — `'class prism::core::List<int>' has no member named 'replace_all'`.

- [ ] **Step 3: Implement `replace_all`**

In `include/prism/core/list.hpp`, add `#include <ranges>` to the includes (alongside the existing `<vector>`), then add the method right after `set` (after line 27):

```cpp
    void replace_all(std::ranges::range auto&& new_values) {
        size_t n = std::ranges::size(new_values);
        while (items_.size() > n) erase(items_.size() - 1);
        size_t i = 0;
        for (auto&& v : new_values) {
            if (i < items_.size()) set(i, std::forward<decltype(v)>(v));
            else push_back(std::forward<decltype(v)>(v));
            ++i;
        }
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C builddir && meson test -C builddir list -v`
Expected: PASS, all three new cases plus the pre-existing ones.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/list.hpp tests/test_list.cpp
git commit -m "feat(list): add List<T>::replace_all for index-preserving bulk replace"
```

---

### Task 4: Variadic `depends_on`

**Files:**
- Modify: `include/prism/app/widget_tree.hpp:63-79` (`DependsOnMixin`)
- Test: `tests/test_view.cpp`

**Interfaces:**
- Produces: `Self& DependsOnMixin<Self>::depends_on(auto&... obs)` — a second overload alongside the existing single-argument one. Consumed by Task 7 (plot canvases) and available to Task 6's SOA table example.
- Consumes: `DependsOnMixin<Self>::depends_on(Observable&)` (the existing single-arg overload, `widget_tree.hpp:67-78`) — unchanged, the new overload folds into it.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_view.cpp` (right after the existing `"canvas depends_on supports multiple fields"` test, which used two chained `.depends_on()` calls):

```cpp
TEST_CASE("canvas depends_on accepts multiple fields in one variadic call") {
    struct VariadicDepCanvas {
        prism::Field<int> x{0};
        prism::Field<int> y{0};
        prism::Field<int> z{0};

        void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
            dl.filled_rect(bounds, prism::Color::rgba(0, 0, 0));
        }

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.canvas(*this).depends_on(x, y, z);
        }
    };

    VariadicDepCanvas model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 1);

    tree.clear_dirty();
    model.x.set(1);
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    model.y.set(2);
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    model.z.set(3);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir && meson test -C builddir view -v`
Expected: FAIL to compile — `depends_on(x, y, z)` doesn't match the single-argument `depends_on(Observable&)`.

- [ ] **Step 3: Add the variadic overload**

In `include/prism/app/widget_tree.hpp`, inside `struct DependsOnMixin` (right after the existing single-arg `depends_on`, i.e. after line 78's closing brace, before line 79's closing brace of the struct):

```cpp
            Self& depends_on(auto&... obs) {
                (depends_on(obs), ...);
                return static_cast<Self&>(*this);
            }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C builddir && meson test -C builddir view -v`
Expected: PASS, including the pre-existing `"canvas depends_on supports multiple fields"` chained-call test (unaffected — the single-arg overload it uses is untouched).

Also rerun the Table suite, since `TableBuilder` shares the same mixin:

Run: `meson test -C builddir table -v`
Expected: PASS, no change (this task adds a capability, doesn't remove one).

- [ ] **Step 5: Commit**

```bash
git add include/prism/app/widget_tree.hpp tests/test_view.cpp
git commit -m "feat(widget_tree): add variadic depends_on overload"
```

---

### Task 5: Table plain-struct row tier

**Files:**
- Modify: `include/prism/ui/table.hpp:1-116` (includes, `wrap_row_storage`)
- Test: `tests/test_table.cpp`

**Interfaces:**
- Produces: `wrap_row_storage<L>(L&)` now handles two shapes of `L::value_type` (unchanged signature/name) — `Field`-wrapped (existing behavior, untouched) and plain-leaf (new). Consumed by Task 7.
- Consumes: `prism::core::LeafType<T>` / `prism::core::format_leaf_value(v)` (Task 2), `prism::core::skip` / `prism::core::label_t` / `prism::core::label<S>` / `prism::core::has_annotation<M, Tag>()` / `prism::core::extract_string_annotation<M, Templ>()` (Task 1) — all resolve unqualified inside `table.hpp` via its existing `using namespace prism::core;` (`table.hpp:21`).

- [ ] **Step 1: Write the failing test**

Append to `tests/test_table.cpp`, inside the existing `#if __cpp_impl_reflection` block (after the `"RowStorage table updates on List remove"` test, before the closing `#endif`):

```cpp
struct PlainRow {
    int id = 0;
    [[=prism::core::skip]] int hidden = 0;
    std::string name;
    [[=prism::core::label<"Score %">]] float score = 0.f;
};

TEST_CASE("wrap_row_storage supports plain (non-Field) struct rows") {
    prism::List<PlainRow> rows;
    rows.push_back(PlainRow{.id = 1, .hidden = 99, .name = "Alpha", .score = 1.5f});
    rows.push_back(PlainRow{.id = 2, .hidden = 42, .name = "Beta", .score = 2.5f});

    auto src = prism::wrap_row_storage(rows);
    CHECK(src.column_count() == 3);   // hidden excluded via skip
    CHECK(src.row_count() == 2);
    CHECK(src.header(0) == "id");
    CHECK(src.header(1) == "name");
    CHECK(src.header(2) == "Score %"); // label override
    CHECK(src.cell_text(0, 0) == "1");
    CHECK(src.cell_text(0, 1) == "Alpha");
    CHECK(src.cell_text(1, 2) == fmt::to_string(2.5f));
}

struct PlainRowModel {
    prism::List<PlainRow> rows;
    PlainRowModel() {
        rows.push_back(PlainRow{.id = 1, .hidden = 0, .name = "A", .score = 1.f});
    }
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(rows);
    }
};

TEST_CASE("ViewBuilder.table() with a plain-struct List still creates a Table node") {
    PlainRowModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    bool found_header = false;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl.commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd))
                if (tc->text == "Score %") found_header = true;
    CHECK(found_header);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C builddir && meson test -C builddir table -v`
Expected: FAIL — `wrap_row_storage` currently only emits columns for `is_field_v` members; `PlainRow` has none, so `column_count()` returns 0 and the header/cell assertions fail.

- [ ] **Step 3: Add the includes and the plain-struct branch to `wrap_row_storage`**

In `include/prism/ui/table.hpp`, add two includes near the top (alongside the existing `#include <prism/core/traits.hpp>`):

```cpp
#include <prism/core/reflect_annotations.hpp>
#include <prism/core/reflect_leaf.hpp>
```

Replace the entire reflection-gated block (from `namespace detail { ... field_to_string ... }` through the end of `wrap_row_storage`, i.e. lines 62-114) with:

```cpp
namespace detail {
template <typename T>
std::string field_to_string(const Field<T>& f) {
    if constexpr (std::is_same_v<T, std::string>)
        return f.get();
    else if constexpr (std::is_arithmetic_v<T>)
        return fmt::to_string(f.get());
    else
        return "?";
}

template <typename Row>
consteval bool has_any_field_member() {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));
    bool found = false;
    template for (constexpr auto m : members) {
        using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
        if constexpr (is_field_v<M>) found = true;
    }
    return found;
}
} // namespace detail

template <RowStorage L>
TableSource wrap_row_storage(L& list) {
    using Row = typename std::remove_cvref_t<L>::value_type;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));
    static constexpr bool any_field_member = detail::has_any_field_member<Row>();

    // Build header names at static-init time
    static const auto headers = [] {
        std::vector<std::string> h;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (any_field_member) {
                if constexpr (is_field_v<M>) {
                    h.emplace_back(std::meta::identifier_of(m));
                }
            } else if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                constexpr auto override_label = extract_string_annotation<m, label_t>();
                if constexpr (!override_label.empty())
                    h.emplace_back(override_label);
                else
                    h.emplace_back(std::meta::identifier_of(m));
            }
        }
        return h;
    }();

    return TableSource{
        .column_count = [&list] { return headers.size(); },
        .row_count = [&list] { return list.size(); },
        .cell_text = [&list](size_t row, size_t col) -> std::string {
            size_t idx = 0;
            std::string result;
            const auto& r = list[row];
            template for (constexpr auto m : members) {
                auto& member = r.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (any_field_member) {
                    if constexpr (is_field_v<M>) {
                        if (idx == col)
                            result = detail::field_to_string(member);
                        ++idx;
                    }
                } else if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                    if (idx == col)
                        result = format_leaf_value(member);
                    ++idx;
                }
            }
            return result;
        },
        .header = [](size_t col) -> std::string_view {
            return headers[col];
        },
    };
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C builddir && meson test -C builddir table -v`
Expected: PASS — both new tests, plus every pre-existing `TEST_CASE` in the file (in particular `"wrap_row_storage produces valid TableSource"` using the `Field`-wrapped `TestRow`, which must still pass unchanged since `any_field_member` is `true` for that type and takes the untouched code path).

- [ ] **Step 5: Commit**

```bash
git add include/prism/ui/table.hpp tests/test_table.cpp
git commit -m "feat(table): support plain-struct rows via LeafType classification and skip/label annotations"
```

---

### Task 6: Table SOA columnar tier (auto-detected)

**Files:**
- Modify: `include/prism/ui/table.hpp` (new `SoaStorage` concept, `is_soa_struct_v`, `wrap_soa_columns`)
- Modify: `include/prism/app/widget_tree.hpp:445-475` (third `table()` overload)
- Test: `tests/test_table.cpp`

**Interfaces:**
- Produces: `prism::SoaStorage<T>` concept, `prism::is_soa_struct_v<T>`, `prism::wrap_soa_columns<T>(T&) -> TableSource`, and a third `ViewBuilder::table(T&)` overload constrained by `SoaStorage<T>`. Not consumed by Task 7 (the example has no column-major data — this tier ships with its own tests only, per the spec's explicit non-goal for the example).
- Consumes: `prism::core::LeafType<T>` / `format_leaf_value` (Task 2), `prism::core::skip` / `label_t` / `has_annotation` / `extract_string_annotation` (Task 1), the existing `ColumnStorage` concept and `TableSource` struct (`table.hpp:24-37`, unchanged), `is_list_v` (`core/traits.hpp:43`, unchanged).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_table.cpp`, inside the `#if __cpp_impl_reflection` block, after Task 5's tests:

```cpp
struct SoaColumns {
    std::vector<int> id = {1, 2, 3};
    std::vector<std::string> name = {"a", "b", "c"};
    [[=prism::core::label<"Score %">]] std::vector<float> score = {1.5f, 2.5f, 3.5f};
    [[=prism::core::skip]] std::vector<int> hidden = {9, 9, 9};
    prism::Field<int> revision{0}; // not a vector -- silently excluded, doesn't disqualify the shape
};

static_assert(prism::SoaStorage<SoaColumns>);
static_assert(!prism::SoaStorage<prism::List<int>>);
static_assert(!prism::SoaStorage<PlainRow>); // PlainRow (Task 5) has no vector members at all

TEST_CASE("wrap_soa_columns produces valid TableSource from struct-of-vectors") {
    SoaColumns cols;
    auto src = prism::wrap_soa_columns(cols);
    CHECK(src.column_count() == 3); // hidden excluded via skip, revision isn't a vector column
    CHECK(src.row_count() == 3);
    CHECK(src.header(0) == "id");
    CHECK(src.header(1) == "name");
    CHECK(src.header(2) == "Score %");
    CHECK(src.cell_text(1, 1) == "b");
    CHECK(src.cell_text(2, 2) == fmt::to_string(3.5f));
}

struct SoaTableModel {
    SoaColumns cols;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(cols);
    }
};

TEST_CASE("ViewBuilder.table() auto-detects a struct-of-vectors and creates a Table node") {
    SoaTableModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children)
        if (c.layout_kind == prism::LayoutKind::Table) table_node = &c;
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    CHECK((*sp)->row_count() == 3);
}

TEST_CASE("SOA table re-renders on depends_on trigger") {
    struct RevisionModel {
        SoaColumns cols;
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.table(cols).depends_on(cols.revision);
        }
    };
    RevisionModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.cols.revision.set(1);
    CHECK(tree.any_dirty());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ninja -C builddir && meson test -C builddir table -v`
Expected: FAIL to compile — `prism::SoaStorage`, `prism::wrap_soa_columns`, and the `vb.table(cols)` overload for a non-`ColumnStorage`, non-`List` struct don't exist yet.

- [ ] **Step 3: Add `SoaStorage`/`wrap_soa_columns` to `table.hpp`**

In `include/prism/ui/table.hpp`, add right after the closing `#endif // __cpp_impl_reflection` of `wrap_row_storage` from Task 5 — i.e. a second `#if __cpp_impl_reflection` block (or extend the existing one; either is fine, put it right before that `#endif` so it's inside the same reflection-gated region):

```cpp
namespace detail {
template <typename T>
struct IsLeafVector : std::false_type {};
template <typename X>
struct IsLeafVector<std::vector<X>> : std::bool_constant<LeafType<X>> {};

template <typename T>
consteval bool has_any_soa_column() {
    if constexpr (!std::is_class_v<T>) {
        return false;
    } else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (!has_annotation<m, decltype(skip)>() && IsLeafVector<M>::value) {
                found = true;
            }
        }
        return found;
    }
}
} // namespace detail

template <typename T>
inline constexpr bool is_soa_struct_v = detail::has_any_soa_column<std::remove_cvref_t<T>>();

template <typename T>
concept SoaStorage = !is_list_v<std::remove_cvref_t<T>>
                      && !ColumnStorage<T>
                      && is_soa_struct_v<T>;

template <SoaStorage T>
TableSource wrap_soa_columns(T& data) {
    using Row = std::remove_cvref_t<T>;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));

    static const auto headers = [] {
        std::vector<std::string> h;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (!has_annotation<m, decltype(skip)>() && detail::IsLeafVector<M>::value) {
                constexpr auto override_label = extract_string_annotation<m, label_t>();
                if constexpr (!override_label.empty())
                    h.emplace_back(override_label);
                else
                    h.emplace_back(std::meta::identifier_of(m));
            }
        }
        return h;
    }();

    return TableSource{
        .column_count = [] { return headers.size(); },
        .row_count = [&data] {
            size_t n = 0;
            bool found = false;
            template for (constexpr auto m : members) {
                auto& member = data.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (!has_annotation<m, decltype(skip)>() && detail::IsLeafVector<M>::value) {
                    if (!found) { n = member.size(); found = true; }
                }
            }
            return n;
        },
        .cell_text = [&data](size_t row, size_t col) -> std::string {
            size_t idx = 0;
            std::string result;
            template for (constexpr auto m : members) {
                auto& member = data.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (!has_annotation<m, decltype(skip)>() && detail::IsLeafVector<M>::value) {
                    if (idx == col)
                        result = format_leaf_value(member[row]);
                    ++idx;
                }
            }
            return result;
        },
        .header = [](size_t col) -> std::string_view {
            return headers[col];
        },
    };
}
```

- [ ] **Step 4: Add the third `ViewBuilder::table` overload**

In `include/prism/app/widget_tree.hpp`, inside the `#if __cpp_impl_reflection` block that already contains the `List<T>` overload (right after it, before its closing `#endif` at line 475):

```cpp
        template <typename T>
            requires SoaStorage<T>
        TableBuilder table(T& data) {
            Node container;
            container.id = tree_.next_id_++;
            container.is_leaf = false;
            container.layout_kind = LayoutKind::Table;

            auto state = std::make_shared<TableState>();
            state->source = wrap_soa_columns(data);
            state->column_count = state->source.column_count();
            container.table_state = state;

            current_parent().children.push_back(std::move(container));
            return TableBuilder{current_parent().children.back(), placed_};
        }
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `ninja -C builddir && meson test -C builddir table -v`
Expected: PASS — all four new tests, plus every pre-existing test in the file (in particular, the `ColumnStorage`-based tests must be unaffected since `SoaStorage<T>` explicitly excludes anything already satisfying `ColumnStorage<T>`).

- [ ] **Step 6: Commit**

```bash
git add include/prism/ui/table.hpp include/prism/app/widget_tree.hpp tests/test_table.cpp
git commit -m "feat(table): add auto-detected SOA (struct-of-vectors) column tier"
```

---

### Task 7: Migrate `examples/model_system_monitor.cpp`

**Files:**
- Modify: `examples/proc_metrics.hpp:69-78` (`ProcessInfo` annotations)
- Modify: `examples/model_system_monitor.cpp:20-141` (remove `ProcessRow`, retype `table_rows`, simplify `ingest_processes`, collapse `depends_on` chains, drop `.headers()`)
- No new automated test — this is an example, verified by manual build + run (system monitor has no existing automated test suite; do not add one, matches the rest of the file).

**Interfaces:**
- Consumes: `List<T>::replace_all` (Task 3), variadic `depends_on` (Task 4), Table's plain-struct row tier (Task 5). Does **not** use the SOA tier (Task 6) — the example has no column-major data, per the spec's explicit scope note.

- [ ] **Step 1: Annotate `ProcessInfo`**

In `examples/proc_metrics.hpp`, replace:

```cpp
struct ProcessInfo {
    int pid = 0;
    int ppid = 0;
    std::string name;
    char state = '?';
    float cpu_percent = 0.f;
    float mem_percent = 0.f;
    long rss_kb = 0;
    long total_jiffies = 0;
};
```

with:

```cpp
struct ProcessInfo {
    int pid = 0;
    [[=prism::core::skip]] int ppid = 0;
    std::string name;
    [[=prism::core::skip]] char state = '?';
    [[=prism::core::label<"CPU %">]] float cpu_percent = 0.f;
    [[=prism::core::label<"Mem %">]] float mem_percent = 0.f;
    [[=prism::core::skip]] long rss_kb = 0;
    [[=prism::core::skip]] long total_jiffies = 0;
};
```

Add `#include <prism/core/reflect_annotations.hpp>` to `proc_metrics.hpp`'s includes (it currently only includes `<prism/core/shared.hpp>`).

- [ ] **Step 2: Delete `ProcessRow`, retype `table_rows`**

In `examples/model_system_monitor.cpp`, delete the `ProcessRow` struct (lines 20-25):

```cpp
struct ProcessRow {
    prism::Field<int> pid{0};
    prism::Field<std::string> name{""};
    prism::Field<float> cpu_percent{0.f};
    prism::Field<float> mem_percent{0.f};
};
```

Change the `table_rows` member declaration (line 43) from:

```cpp
    prism::List<ProcessRow> table_rows;
```

to:

```cpp
    prism::List<ProcessInfo> table_rows;
```

- [ ] **Step 3: Simplify `ingest_processes`**

Replace `ingest_processes` (lines 70-85):

```cpp
    void ingest_processes(const std::vector<ProcessInfo>& processes) {
        auto sorted = sort_by(processes, sort_key.get());

        while (table_rows.size() > sorted.size())
            table_rows.erase(table_rows.size() - 1);
        for (size_t i = 0; i < sorted.size(); ++i) {
            ProcessRow row{.pid = {sorted[i].pid}, .name = {sorted[i].name},
                           .cpu_percent = {sorted[i].cpu_percent},
                           .mem_percent = {sorted[i].mem_percent}};
            if (i < table_rows.size()) table_rows.set(i, std::move(row));
            else table_rows.push_back(std::move(row));
        }

        tree_source.update(processes);
        tree_ctrl.refresh();
    }
```

with:

```cpp
    void ingest_processes(const std::vector<ProcessInfo>& processes) {
        table_rows.replace_all(sort_by(processes, sort_key.get()));
        tree_source.update(processes);
        tree_ctrl.refresh();
    }
```

- [ ] **Step 4: Collapse the three `depends_on` chains**

In `view()` (lines 112-141), replace each of the three plot blocks. E.g. the CPU one:

```cpp
            vb.canvas(cpu_plot)
                .depends_on(cpu_plot.x_range).depends_on(cpu_plot.y_range)
                .depends_on(cpu_plot.view).depends_on(cpu_plot.cursor)
                .depends_on(cpu_plot.revision)
                .min_size(prism::Height{120});
```

becomes:

```cpp
            vb.canvas(cpu_plot)
                .depends_on(cpu_plot.x_range, cpu_plot.y_range, cpu_plot.view,
                            cpu_plot.cursor, cpu_plot.revision)
                .min_size(prism::Height{120});
```

Apply the same collapse to the `mem_plot` and `net_plot` blocks.

- [ ] **Step 5: Drop the now-redundant `.headers()` call**

In the `"Table"` tab (line 133), change:

```cpp
                tvb.table(table_rows).headers({"PID", "Name", "CPU %", "Mem %"});
```

to:

```cpp
                tvb.table(table_rows);
```

(Header text is now `pid | name | CPU % | Mem %` via the default `identifier_of` for `pid`/`name` and the `label` annotations from Step 1 for the two percent columns — matching what `.headers()` used to hardcode, except `pid`/`name` are lowercase since no override is applied. This is an intentional, visible cosmetic difference from before; if exact `"PID"`/`"Name"` capitalization is wanted, add `[[=prism::core::label<"PID">]]` / `[[=prism::core::label<"Name">]]` to `ProcessInfo` in Step 1 instead of relying on the default.)

- [ ] **Step 6: Rebuild and manually run the example**

Run: `ninja -C builddir`
Expected: builds cleanly, no errors.

Run the built binary directly (it's `executable('model_system_monitor', 'model_system_monitor.cpp', ...)` in `examples/meson.build:17`):

```bash
./builddir/examples/model_system_monitor /tmp/smoke.svg   # headless screenshot-mode smoke check
./builddir/examples/model_system_monitor                  # live windowed mode
```

Confirm:
- The table shows 4 columns (`pid`/`name`/`CPU %`/`Mem %`), sorted correctly, still responds to the sort dropdown.
- The three plots (CPU/mem/net) still redraw as data streams in.
- The tree tab still populates from the same `processes` vector.
- The heartbeat canvas still pulses (unaffected by this migration, but confirms the app isn't stalled).

- [ ] **Step 7: Run the full test suite one more time**

Run: `meson test -C builddir`
Expected: every test PASSES (full-suite confirmation after all 7 tasks, not just the files touched by this task).

- [ ] **Step 8: Commit**

```bash
git add examples/proc_metrics.hpp examples/model_system_monitor.cpp
git commit -m "refactor(examples): migrate system monitor to plain-struct table rows and replace_all"
```

---

## Self-Review Notes

- **Spec coverage:** Piece 1 (shared infra) → Tasks 1+2. Piece 2 (row tier) → Task 5. Piece 3 (`replace_all`) → Task 3. Piece 4 (variadic `depends_on`) → Task 4. Piece 5 (SOA tier) → Task 6. Piece 6 (example migration) → Task 7. All six pieces covered.
- **Namespace deviation:** documented up top in Global Constraints — plan uses `prism::core` instead of the spec doc's `prism::reflect`, matching the rest of `include/prism/core/`.
- **Type/signature consistency check:** `wrap_row_storage<L>(L&)` (Task 5) keeps the exact name/signature used by the existing `RowStorage` overload in `widget_tree.hpp:448`, no changes needed there. `wrap_soa_columns<T>(T&)` (Task 6) is called both directly in tests and from the new `widget_tree.hpp` overload (Task 6, Step 4) with matching signature. `List<T>::replace_all` (Task 3) is called from Task 7 with a `std::vector<ProcessInfo>` argument (via `sort_by(...)`, which already returns `std::vector<ProcessInfo>` per the existing `proc_metrics.hpp` — matches `std::ranges::range auto&&`).
