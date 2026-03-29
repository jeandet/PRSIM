# Strong Coordinate Types — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all raw float coordinates with zero-cost strong types enforcing axis separation, affine algebra, and position/offset/dimension distinctions at compile time.

**Architecture:** A `Scalar<Tag>` template with trait-based algebra encodes all rules. Composite types (`Point`, `Offset`, `Size`, `Rect`) are composed from per-direction scalars. Migration is bottom-up: types.hpp → draw_list.hpp → commands → layout/hit_test/input → delegates → backend.

**Tech Stack:** C++26, doctest, Meson, concepts, `<=>`, `constexpr`

**Spec:** `docs/superpowers/specs/2026-03-29-strong-types-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `include/prism/core/types.hpp` | Create | Scalar<Tag>, tags, traits, operators, composites, literals |
| `tests/test_types.cpp` | Create | Exhaustive scalar/composite algebra + compile-time safety tests |
| `tests/meson.build` | Modify | Add test_types to headless_tests |
| `include/prism/core/draw_list.hpp` | Modify | Replace local type defs with `#include types.hpp`, update DrawList + commands |
| `include/prism/core/pixel_buffer.hpp` | Modify | Update `fill_rect` to use composed Rect |
| `include/prism/core/input_event.hpp` | Modify | Mouse coords use strong types, localize_mouse uses Offset |
| `include/prism/core/layout.hpp` | Modify | SizeHint + layout_arrange + translate_draw_list use strong types |
| `include/prism/core/scene_snapshot.hpp` | Modify | No changes needed (uses Rect transitively) |
| `include/prism/core/hit_test.hpp` | Modify | No changes needed (uses Point/Rect transitively) |
| `include/prism/core/delegate.hpp` | Modify | Widget constants + record/handle_input use strong types |
| `include/prism/core/widget_tree.hpp` | Modify | detail:: helpers + all delegate method bodies use strong types |
| `include/prism/core/app.hpp` | Modify | Frame class uses strong types |
| `src/backends/software_backend.cpp` | Modify | `.raw()` extraction at SDL boundary |
| `include/prism/core/software_renderer.hpp` | Modify | Minimal (passes Rect to fill_rect) |
| All test files | Modify | Construct strong types instead of bare floats |

---

### Task 1: Create types.hpp — Scalar template, tags, traits, and operators

**Files:**
- Create: `include/prism/core/types.hpp`
- Create: `tests/test_types.cpp`
- Modify: `tests/meson.build`

- [ ] **Step 1: Write failing tests for scalar algebra**

Create `tests/test_types.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>

using namespace prism;
using namespace prism::literals;

// === Scalar construction and raw() ===

TEST_CASE("Scalar construction and raw()") {
    constexpr X x{10.f};
    static_assert(x.raw() == 10.f);
    CHECK(x.raw() == 10.f);
}

// === Same-tag comparison ===

TEST_CASE("Scalar same-tag comparison") {
    CHECK(X{5.f} == X{5.f});
    CHECK(X{3.f} < X{5.f});
    CHECK(X{5.f} > X{3.f});
    CHECK(X{5.f} != X{3.f});
}

// === Position - Position = Offset ===

TEST_CASE("X - X = DX") {
    constexpr auto result = X{10.f} - X{3.f};
    static_assert(std::same_as<decltype(result), const DX>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

TEST_CASE("Y - Y = DY") {
    auto result = Y{20.f} - Y{5.f};
    static_assert(std::same_as<decltype(result), DY>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

// === Position + Offset = Position ===

TEST_CASE("X + DX = X") {
    constexpr auto result = X{10.f} + DX{5.f};
    static_assert(std::same_as<decltype(result), const X>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("X - DX = X") {
    auto result = X{10.f} - DX{3.f};
    static_assert(std::same_as<decltype(result), X>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

TEST_CASE("Y + DY = Y") {
    auto result = Y{10.f} + DY{5.f};
    static_assert(std::same_as<decltype(result), Y>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

// === Offset + Offset = Offset ===

TEST_CASE("DX + DX = DX") {
    auto result = DX{3.f} + DX{7.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(10.f));
}

TEST_CASE("DX - DX = DX") {
    auto result = DX{10.f} - DX{3.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(7.f));
}

// === Offset scaling ===

TEST_CASE("DX * float = DX") {
    auto result = DX{5.f} * 3.f;
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("float * DX = DX") {
    auto result = 3.f * DX{5.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(15.f));
}

TEST_CASE("DX / float = DX") {
    auto result = DX{15.f} / 3.f;
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(5.f));
}

TEST_CASE("-DX = DX") {
    auto result = -DX{5.f};
    static_assert(std::same_as<decltype(result), DX>);
    CHECK(result.raw() == doctest::Approx(-5.f));
}

// === Dimension algebra ===

TEST_CASE("Width + Width = Width") {
    auto result = Width{10.f} + Width{20.f};
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(30.f));
}

TEST_CASE("Width - Width = Width") {
    auto result = Width{30.f} - Width{10.f};
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(20.f));
}

TEST_CASE("Width * float = Width") {
    auto result = Width{10.f} * 2.f;
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(20.f));
}

TEST_CASE("Width / float = Width") {
    auto result = Width{20.f} / 2.f;
    static_assert(std::same_as<decltype(result), Width>);
    CHECK(result.raw() == doctest::Approx(10.f));
}

TEST_CASE("Height * float = Height") {
    auto result = Height{10.f} * 1.4f;
    static_assert(std::same_as<decltype(result), Height>);
    CHECK(result.raw() == doctest::Approx(14.f));
}

// === Compile-time safety: forbidden operations ===

TEST_CASE("Forbidden operations do not compile") {
    // X + X (adding positions)
    static_assert(!requires { X{1} + X{2}; });
    // X * float (scaling a position)
    static_assert(!requires { X{1} * 2.f; });
    // X + Width (mixing position and dimension)
    static_assert(!requires { X{1} + Width{2}; });
    // Width + DX (mixing dimension and offset)
    static_assert(!requires { Width{1} + DX{2}; });
    // X + Y (mixing axes)
    static_assert(!requires { X{1} + Y{2}; });
    // DX + DY (mixing axes)
    static_assert(!requires { DX{1} + DY{2}; });
    // X == Y (cross-tag comparison)
    static_assert(!requires { X{1} == Y{1}; });
    // -X (negating a position)
    static_assert(!requires { -X{1}; });
    // implicit conversion from float
    static_assert(!std::convertible_to<float, X>);
}

// === User-defined literals ===

TEST_CASE("User-defined literals") {
    auto x = 10.0_x;
    static_assert(std::same_as<decltype(x), X>);
    CHECK(x.raw() == doctest::Approx(10.f));

    auto w = 100.0_w;
    static_assert(std::same_as<decltype(w), Width>);
    CHECK(w.raw() == doctest::Approx(100.f));
}

// === Composite types ===

TEST_CASE("Point - Point = Offset") {
    Point a{X{10.f}, Y{20.f}};
    Point b{X{3.f}, Y{5.f}};
    auto result = a - b;
    static_assert(std::same_as<decltype(result), Offset>);
    CHECK(result.dx.raw() == doctest::Approx(7.f));
    CHECK(result.dy.raw() == doctest::Approx(15.f));
}

TEST_CASE("Point + Offset = Point") {
    Point p{X{10.f}, Y{20.f}};
    Offset o{DX{5.f}, DY{3.f}};
    auto result = p + o;
    static_assert(std::same_as<decltype(result), Point>);
    CHECK(result.x.raw() == doctest::Approx(15.f));
    CHECK(result.y.raw() == doctest::Approx(23.f));
}

TEST_CASE("Point - Offset = Point") {
    Point p{X{10.f}, Y{20.f}};
    Offset o{DX{3.f}, DY{5.f}};
    auto result = p - o;
    static_assert(std::same_as<decltype(result), Point>);
    CHECK(result.x.raw() == doctest::Approx(7.f));
    CHECK(result.y.raw() == doctest::Approx(15.f));
}

TEST_CASE("Offset + Offset = Offset") {
    Offset a{DX{1.f}, DY{2.f}};
    Offset b{DX{3.f}, DY{4.f}};
    auto result = a + b;
    static_assert(std::same_as<decltype(result), Offset>);
    CHECK(result.dx.raw() == doctest::Approx(4.f));
    CHECK(result.dy.raw() == doctest::Approx(6.f));
}

TEST_CASE("Offset * float") {
    Offset o{DX{2.f}, DY{3.f}};
    auto result = o * 2.f;
    CHECK(result.dx.raw() == doctest::Approx(4.f));
    CHECK(result.dy.raw() == doctest::Approx(6.f));
}

TEST_CASE("Size + Size = Size") {
    Size a{Width{10.f}, Height{20.f}};
    Size b{Width{5.f}, Height{3.f}};
    auto result = a + b;
    static_assert(std::same_as<decltype(result), Size>);
    CHECK(result.w.raw() == doctest::Approx(15.f));
    CHECK(result.h.raw() == doctest::Approx(23.f));
}

TEST_CASE("Size * float") {
    Size s{Width{10.f}, Height{20.f}};
    auto result = s * 2.f;
    CHECK(result.w.raw() == doctest::Approx(20.f));
    CHECK(result.h.raw() == doctest::Approx(40.f));
}

TEST_CASE("Rect contains Point") {
    Rect r{Point{X{10.f}, Y{20.f}}, Size{Width{100.f}, Height{50.f}}};
    CHECK(r.contains(Point{X{50.f}, Y{40.f}}));
    CHECK_FALSE(r.contains(Point{X{5.f}, Y{40.f}}));
    CHECK_FALSE(r.contains(Point{X{50.f}, Y{75.f}}));
}

TEST_CASE("Rect center") {
    Rect r{Point{X{10.f}, Y{20.f}}, Size{Width{100.f}, Height{50.f}}};
    auto c = r.center();
    CHECK(c.x.raw() == doctest::Approx(60.f));
    CHECK(c.y.raw() == doctest::Approx(45.f));
}

TEST_CASE("Rect from_corners") {
    auto r = Rect::from_corners(Point{X{10.f}, Y{20.f}}, Point{X{110.f}, Y{70.f}});
    CHECK(r.origin.x.raw() == doctest::Approx(10.f));
    CHECK(r.origin.y.raw() == doctest::Approx(20.f));
    CHECK(r.extent.w.raw() == doctest::Approx(100.f));
    CHECK(r.extent.h.raw() == doctest::Approx(50.f));
}

TEST_CASE("Offset length") {
    Offset o{DX{3.f}, DY{4.f}};
    CHECK(o.length() == doctest::Approx(5.f));
}

TEST_CASE("Composite equality") {
    Point a{X{1.f}, Y{2.f}};
    Point b{X{1.f}, Y{2.f}};
    Point c{X{1.f}, Y{3.f}};
    CHECK(a == b);
    CHECK(a != c);

    Size s1{Width{10.f}, Height{20.f}};
    Size s2{Width{10.f}, Height{20.f}};
    CHECK(s1 == s2);
}
```

- [ ] **Step 2: Add test to meson.build**

In `tests/meson.build`, add to the `headless_tests` dict:

```
  'types' : files('test_types.cpp'),
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson test -C builddir types --verbose`
Expected: Compilation error — `types.hpp` doesn't exist yet.

- [ ] **Step 4: Implement types.hpp**

Create `include/prism/core/types.hpp`:

```cpp
#pragma once

#include <cmath>
#include <compare>
#include <concepts>

namespace prism {

// --- Scalar template ---

template <typename Tag>
struct Scalar {
    constexpr explicit Scalar(float v) : v_(v) {}
    [[nodiscard]] constexpr float raw() const { return v_; }
    constexpr auto operator<=>(const Scalar&) const = default;
private:
    float v_;
};

// --- Tags ---

struct XTag {};
struct YTag {};
struct DXTag {};
struct DYTag {};
struct WidthTag {};
struct HeightTag {};

// --- Aliases ---

using X = Scalar<XTag>;
using Y = Scalar<YTag>;
using DX = Scalar<DXTag>;
using DY = Scalar<DYTag>;
using Width = Scalar<WidthTag>;
using Height = Scalar<HeightTag>;

// --- Algebra traits ---

template <typename LTag, typename RTag>
struct AddResult;  // no default — SFINAE fallback

template <typename LTag, typename RTag>
struct SubResult;

template <typename Tag>
struct ScaleResult;

// Convenience alias
template <typename LTag, typename RTag>
using add_result_t = typename AddResult<LTag, RTag>::type;

template <typename LTag, typename RTag>
using sub_result_t = typename SubResult<LTag, RTag>::type;

template <typename Tag>
using scale_result_t = typename ScaleResult<Tag>::type;

// Concepts for constraining operators
template <typename L, typename R>
concept Addable = requires { typename AddResult<L, R>::type; };

template <typename L, typename R>
concept Subtractable = requires { typename SubResult<L, R>::type; };

template <typename T>
concept Scalable = requires { typename ScaleResult<T>::type; };

// --- Position rules: X ---
template <> struct SubResult<XTag, XTag>   { using type = DXTag; };   // X - X = DX
template <> struct AddResult<XTag, DXTag>  { using type = XTag; };    // X + DX = X
template <> struct SubResult<XTag, DXTag>  { using type = XTag; };    // X - DX = X

// --- Position rules: Y ---
template <> struct SubResult<YTag, YTag>   { using type = DYTag; };   // Y - Y = DY
template <> struct AddResult<YTag, DYTag>  { using type = YTag; };    // Y + DY = Y
template <> struct SubResult<YTag, DYTag>  { using type = YTag; };    // Y - DY = Y

// --- Offset rules: DX ---
template <> struct AddResult<DXTag, DXTag> { using type = DXTag; };   // DX + DX = DX
template <> struct SubResult<DXTag, DXTag> { using type = DXTag; };   // DX - DX = DX
template <> struct ScaleResult<DXTag>      { using type = DXTag; };   // DX * float = DX

// --- Offset rules: DY ---
template <> struct AddResult<DYTag, DYTag> { using type = DYTag; };
template <> struct SubResult<DYTag, DYTag> { using type = DYTag; };
template <> struct ScaleResult<DYTag>      { using type = DYTag; };

// --- Dimension rules: Width ---
template <> struct AddResult<WidthTag, WidthTag> { using type = WidthTag; };
template <> struct SubResult<WidthTag, WidthTag> { using type = WidthTag; };
template <> struct ScaleResult<WidthTag>         { using type = WidthTag; };

// --- Dimension rules: Height ---
template <> struct AddResult<HeightTag, HeightTag> { using type = HeightTag; };
template <> struct SubResult<HeightTag, HeightTag> { using type = HeightTag; };
template <> struct ScaleResult<HeightTag>          { using type = HeightTag; };

// --- Scalar operators ---

template <typename L, typename R>
    requires Addable<L, R>
constexpr Scalar<add_result_t<L, R>> operator+(Scalar<L> a, Scalar<R> b) {
    return Scalar<add_result_t<L, R>>{a.raw() + b.raw()};
}

template <typename L, typename R>
    requires Subtractable<L, R>
constexpr Scalar<sub_result_t<L, R>> operator-(Scalar<L> a, Scalar<R> b) {
    return Scalar<sub_result_t<L, R>>{a.raw() - b.raw()};
}

template <typename Tag>
    requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator*(Scalar<Tag> a, float s) {
    return Scalar<scale_result_t<Tag>>{a.raw() * s};
}

template <typename Tag>
    requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator*(float s, Scalar<Tag> a) {
    return Scalar<scale_result_t<Tag>>{s * a.raw()};
}

template <typename Tag>
    requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator/(Scalar<Tag> a, float s) {
    return Scalar<scale_result_t<Tag>>{a.raw() / s};
}

template <typename Tag>
    requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator-(Scalar<Tag> a) {
    return Scalar<scale_result_t<Tag>>{-a.raw()};
}

// --- Composite types ---

struct Offset {
    DX dx;
    DY dy;
    constexpr auto operator<=>(const Offset&) const = default;
    [[nodiscard]] float length() const { return std::sqrt(dx.raw() * dx.raw() + dy.raw() * dy.raw()); }
};

struct Point {
    X x;
    Y y;
    constexpr auto operator<=>(const Point&) const = default;
};

struct Size {
    Width w;
    Height h;
    constexpr auto operator<=>(const Size&) const = default;
};

struct Rect {
    Point origin;
    Size extent;
    constexpr auto operator<=>(const Rect&) const = default;

    [[nodiscard]] constexpr Point center() const {
        return {X{origin.x.raw() + extent.w.raw() / 2.f},
                Y{origin.y.raw() + extent.h.raw() / 2.f}};
    }

    [[nodiscard]] constexpr bool contains(Point p) const {
        return p.x.raw() >= origin.x.raw()
            && p.x.raw() <  origin.x.raw() + extent.w.raw()
            && p.y.raw() >= origin.y.raw()
            && p.y.raw() <  origin.y.raw() + extent.h.raw();
    }

    [[nodiscard]] static constexpr Rect from_corners(Point top_left, Point bottom_right) {
        return {top_left,
                Size{Width{(bottom_right.x - top_left.x).raw()},
                     Height{(bottom_right.y - top_left.y).raw()}}};
    }
};

// --- Composite operators ---

// Point - Point = Offset
constexpr Offset operator-(Point a, Point b) {
    return {a.x - b.x, a.y - b.y};
}

// Point + Offset = Point
constexpr Point operator+(Point p, Offset o) {
    return {p.x + o.dx, p.y + o.dy};
}

// Point - Offset = Point
constexpr Point operator-(Point p, Offset o) {
    return {p.x - o.dx, p.y - o.dy};
}

// Offset + Offset = Offset
constexpr Offset operator+(Offset a, Offset b) {
    return {a.dx + b.dx, a.dy + b.dy};
}

// Offset - Offset = Offset
constexpr Offset operator-(Offset a, Offset b) {
    return {a.dx - b.dx, a.dy - b.dy};
}

// Offset * float
constexpr Offset operator*(Offset o, float s) {
    return {o.dx * s, o.dy * s};
}

constexpr Offset operator*(float s, Offset o) {
    return {s * o.dx, s * o.dy};
}

// Offset / float
constexpr Offset operator/(Offset o, float s) {
    return {o.dx / s, o.dy / s};
}

// -Offset
constexpr Offset operator-(Offset o) {
    return {-o.dx, -o.dy};
}

// Size + Size = Size
constexpr Size operator+(Size a, Size b) {
    return {a.w + b.w, a.h + b.h};
}

// Size - Size = Size
constexpr Size operator-(Size a, Size b) {
    return {a.w - b.w, a.h - b.h};
}

// Size * float
constexpr Size operator*(Size s, float f) {
    return {s.w * f, s.h * f};
}

constexpr Size operator*(float f, Size s) {
    return {f * s.w, f * s.h};
}

// Size / float
constexpr Size operator/(Size s, float f) {
    return {s.w / f, s.h / f};
}

// --- User-defined literals ---

namespace literals {

constexpr X operator""_x(long double v) { return X{static_cast<float>(v)}; }
constexpr Y operator""_y(long double v) { return Y{static_cast<float>(v)}; }
constexpr DX operator""_dx(long double v) { return DX{static_cast<float>(v)}; }
constexpr DY operator""_dy(long double v) { return DY{static_cast<float>(v)}; }
constexpr Width operator""_w(long double v) { return Width{static_cast<float>(v)}; }
constexpr Height operator""_h(long double v) { return Height{static_cast<float>(v)}; }

} // namespace literals

} // namespace prism
```

- [ ] **Step 5: Run tests**

Run: `meson test -C builddir types --verbose`
Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/types.hpp tests/test_types.cpp tests/meson.build
git commit -m "feat: add strong coordinate types with Scalar<Tag> algebra

Zero-cost Scalar<Tag> template with per-direction tags (X, Y, DX, DY,
Width, Height). Trait-based algebra enforces affine rules at compile time.
Composites: Point, Offset, Size, Rect. User-defined literals."
```

---

### Task 2: Migrate draw_list.hpp — replace local types with types.hpp

**Files:**
- Modify: `include/prism/core/draw_list.hpp`
- Modify: `tests/test_draw_list.cpp`

- [ ] **Step 1: Update draw_list.hpp**

Replace the local type definitions and update DrawList to use the new types.

Remove these structs from draw_list.hpp (lines 21-37):
```cpp
struct Point { float x, y; };
struct Size { float w, h; };
struct Rect { float x, y, w, h; ... };
```

Add at the top (after existing includes):
```cpp
#include <prism/core/types.hpp>
```

Update `ClipPush` to use composed Rect:
```cpp
struct ClipPush {
    Rect rect;  // Rect is now {Point origin; Size extent;}
};
```

Update `DrawList` methods — the key changes are accessing `r.origin.x` instead of `r.x` and `r.extent.w` instead of `r.w`:

```cpp
struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(FilledRect{
            Rect{Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c});
    }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        auto o = current_offset();
        commands.emplace_back(RectOutline{
            Rect{Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c, thickness});
    }

    void text(std::string s, Point origin, float size, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(
            TextCmd{std::move(s), Point{origin.x + o.dx, origin.y + o.dy}, size, c});
    }

    void clip_push(Point origin, Size extent)
    {
        auto o = current_offset();
        DX abs_dx = o.dx + DX{origin.x.raw()};
        DY abs_dy = o.dy + DY{origin.y.raw()};
        origin_stack_.push_back(Offset{abs_dx, abs_dy});
        commands.emplace_back(ClipPush{
            Rect{Point{X{abs_dx.raw()}, Y{abs_dy.raw()}}, extent}});
    }

    void clip_pop()
    {
        if (!origin_stack_.empty()) origin_stack_.pop_back();
        commands.emplace_back(ClipPop{});
    }

    void clear()
    {
        commands.clear();
        origin_stack_.clear();
    }

    [[nodiscard]] bool empty() const { return commands.empty(); }
    [[nodiscard]] std::size_t size() const { return commands.size(); }

    [[nodiscard]] Rect bounding_box() const {
        if (commands.empty())
            return Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        auto expand = [&](Rect r) {
            min_x = std::min(min_x, r.origin.x.raw());
            min_y = std::min(min_y, r.origin.y.raw());
            max_x = std::max(max_x, r.origin.x.raw() + r.extent.w.raw());
            max_y = std::max(max_y, r.origin.y.raw() + r.extent.h.raw());
        };
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.rect; })
                    expand(c.rect);
                else if constexpr (requires { c.origin; })
                    expand(Rect{c.origin, Size{Width{0}, Height{c.size}}});
            }, cmd);
        }
        return Rect{Point{X{min_x}, Y{min_y}},
                     Size{Width{max_x - min_x}, Height{max_y - min_y}}};
    }

  private:
    std::vector<Offset> origin_stack_;

    [[nodiscard]] Offset current_offset() const
    {
        return origin_stack_.empty() ? Offset{DX{0.f}, DY{0.f}} : origin_stack_.back();
    }
};
```

Note: `origin_stack_` now stores `Offset` instead of `Point` — an accumulated offset is semantically an Offset, not a Point.

- [ ] **Step 2: Update test_draw_list.cpp**

Update all tests to construct types using the new API. Key pattern: `{x, y, w, h}` becomes `Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}`. For brevity, define a helper at the top of the test file:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/draw_list.hpp>

using namespace prism;

// Helper to reduce verbosity in tests
constexpr Rect make_rect(float x, float y, float w, float h) {
    return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}};
}

TEST_CASE("DrawList basic commands")
{
    DrawList dl;
    CHECK(dl.empty());

    dl.filled_rect(make_rect(0, 0, 100, 50), Color::rgba(255, 0, 0));
    dl.text("hello", Point{X{50}, Y{25}}, 14.0f, Color::rgba(0, 0, 0));
    dl.rect_outline(make_rect(0, 0, 100, 50), Color::rgba(0, 0, 0), 2.0f);

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<FilledRect>(dl.commands[0]));
    CHECK(std::holds_alternative<TextCmd>(dl.commands[1]));
    CHECK(std::holds_alternative<RectOutline>(dl.commands[2]));
}

TEST_CASE("DrawList clip push/pop")
{
    DrawList dl;
    dl.clip_push(Point{X{10.f}, Y{10.f}}, Size{Width{80.f}, Height{40.f}});
    dl.filled_rect(make_rect(10, 10, 60, 20), Color::rgba(0, 255, 0));
    dl.clip_pop();

    CHECK(dl.size() == 3);
    CHECK(std::holds_alternative<ClipPush>(dl.commands[0]));
    CHECK(std::holds_alternative<ClipPop>(dl.commands[2]));
}

TEST_CASE("DrawList clear")
{
    DrawList dl;
    dl.filled_rect(make_rect(0, 0, 10, 10), Color::rgba(0, 0, 0));
    dl.clear();
    CHECK(dl.empty());
}

TEST_CASE("bounding_box of empty draw list is zero rect") {
    DrawList dl;
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 0);
    CHECK(bb.origin.y.raw() == 0);
    CHECK(bb.extent.w.raw() == 0);
    CHECK(bb.extent.h.raw() == 0);
}

TEST_CASE("bounding_box encompasses all commands") {
    DrawList dl;
    dl.filled_rect(make_rect(10, 20, 100, 50), Color::rgba(255, 0, 0));
    dl.filled_rect(make_rect(50, 0, 30, 200), Color::rgba(0, 255, 0));
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 10);
    CHECK(bb.origin.y.raw() == 0);
    CHECK(bb.extent.w.raw() == 100);
    CHECK(bb.extent.h.raw() == 200);
}

TEST_CASE("bounding_box handles rect_outline") {
    DrawList dl;
    dl.rect_outline(make_rect(5, 5, 90, 40), Color::rgba(0, 0, 0), 2.f);
    auto bb = dl.bounding_box();
    CHECK(bb.origin.x.raw() == 5);
    CHECK(bb.origin.y.raw() == 5);
    CHECK(bb.extent.w.raw() == 90);
    CHECK(bb.extent.h.raw() == 40);
}

TEST_CASE("clip_push offsets filled_rect coordinates") {
    DrawList dl;
    dl.clip_push(Point{X{10.f}, Y{20.f}}, Size{Width{80.f}, Height{40.f}});
    dl.filled_rect(make_rect(0, 0, 30, 15), Color::rgba(255, 0, 0));
    dl.clip_pop();

    auto& clip = std::get<ClipPush>(dl.commands[0]);
    CHECK(clip.rect.origin.x.raw() == 10.f);
    CHECK(clip.rect.origin.y.raw() == 20.f);
    CHECK(clip.rect.extent.w.raw() == 80.f);
    CHECK(clip.rect.extent.h.raw() == 40.f);

    auto& fr = std::get<FilledRect>(dl.commands[1]);
    CHECK(fr.rect.origin.x.raw() == 10.f);
    CHECK(fr.rect.origin.y.raw() == 20.f);
    CHECK(fr.rect.extent.w.raw() == 30.f);
    CHECK(fr.rect.extent.h.raw() == 15.f);
}

TEST_CASE("clip_push offsets text origin") {
    DrawList dl;
    dl.clip_push(Point{X{5.f}, Y{10.f}}, Size{Width{100.f}, Height{50.f}});
    dl.text("hi", Point{X{0}, Y{0}}, 14.f, Color::rgba(0, 0, 0));
    dl.clip_pop();

    auto& t = std::get<TextCmd>(dl.commands[1]);
    CHECK(t.origin.x.raw() == 5.f);
    CHECK(t.origin.y.raw() == 10.f);
}

TEST_CASE("clip_push offsets rect_outline") {
    DrawList dl;
    dl.clip_push(Point{X{3.f}, Y{7.f}}, Size{Width{50.f}, Height{50.f}});
    dl.rect_outline(make_rect(0, 0, 20, 20), Color::rgba(0, 0, 0), 1.f);
    dl.clip_pop();

    auto& ro = std::get<RectOutline>(dl.commands[1]);
    CHECK(ro.rect.origin.x.raw() == 3.f);
    CHECK(ro.rect.origin.y.raw() == 7.f);
}

TEST_CASE("nested clip_push offsets compose additively") {
    DrawList dl;
    dl.clip_push(Point{X{10.f}, Y{10.f}}, Size{Width{100.f}, Height{100.f}});
    dl.clip_push(Point{X{5.f}, Y{5.f}}, Size{Width{80.f}, Height{80.f}});
    dl.filled_rect(make_rect(0, 0, 10, 10), Color::rgba(255, 0, 0));
    dl.clip_pop();
    dl.filled_rect(make_rect(0, 0, 10, 10), Color::rgba(0, 255, 0));
    dl.clip_pop();

    auto& inner_clip = std::get<ClipPush>(dl.commands[1]);
    CHECK(inner_clip.rect.origin.x.raw() == 15.f);
    CHECK(inner_clip.rect.origin.y.raw() == 15.f);

    auto& inner_rect = std::get<FilledRect>(dl.commands[2]);
    CHECK(inner_rect.rect.origin.x.raw() == 15.f);
    CHECK(inner_rect.rect.origin.y.raw() == 15.f);

    auto& outer_rect = std::get<FilledRect>(dl.commands[4]);
    CHECK(outer_rect.rect.origin.x.raw() == 10.f);
    CHECK(outer_rect.rect.origin.y.raw() == 10.f);
}

TEST_CASE("no clip_push means no offset") {
    DrawList dl;
    dl.filled_rect(make_rect(5, 5, 10, 10), Color::rgba(255, 0, 0));
    dl.text("hi", Point{X{3}, Y{7}}, 14.f, Color::rgba(0, 0, 0));

    auto& fr = std::get<FilledRect>(dl.commands[0]);
    CHECK(fr.rect.origin.x.raw() == 5.f);
    CHECK(fr.rect.origin.y.raw() == 5.f);

    auto& t = std::get<TextCmd>(dl.commands[1]);
    CHECK(t.origin.x.raw() == 3.f);
    CHECK(t.origin.y.raw() == 7.f);
}

TEST_CASE("clip_pop restores previous offset") {
    DrawList dl;
    dl.clip_push(Point{X{10.f}, Y{10.f}}, Size{Width{100.f}, Height{100.f}});
    dl.text("inside", Point{X{0}, Y{0}}, 14.f, Color::rgba(0, 0, 0));
    dl.clip_pop();
    dl.text("outside", Point{X{0}, Y{0}}, 14.f, Color::rgba(0, 0, 0));

    auto& inside = std::get<TextCmd>(dl.commands[1]);
    CHECK(inside.origin.x.raw() == 10.f);
    CHECK(inside.origin.y.raw() == 10.f);

    auto& outside = std::get<TextCmd>(dl.commands[3]);
    CHECK(outside.origin.x.raw() == 0.f);
    CHECK(outside.origin.y.raw() == 0.f);
}
```

- [ ] **Step 3: Run draw_list tests**

Run: `meson test -C builddir draw_list types --verbose`
Expected: All PASS.

- [ ] **Step 4: Commit**

```bash
git add include/prism/core/draw_list.hpp tests/test_draw_list.cpp
git commit -m "refactor: migrate draw_list.hpp to strong coordinate types

Replace local Point/Size/Rect with types.hpp. Rect is now
{Point origin; Size extent;}. origin_stack_ stores Offset."
```

---

### Task 3: Migrate pixel_buffer, input_event, layout, and scene infrastructure

**Files:**
- Modify: `include/prism/core/pixel_buffer.hpp`
- Modify: `include/prism/core/input_event.hpp`
- Modify: `include/prism/core/layout.hpp`
- Modify: `include/prism/core/scene_snapshot.hpp` (no changes — uses Rect transitively)
- Modify: `include/prism/core/hit_test.hpp` (no changes — uses Point/Rect transitively)

- [ ] **Step 1: Update pixel_buffer.hpp**

The `fill_rect` method receives a `Rect` and extracts raw coordinates for pixel operations. Update it to use `.origin.x.raw()` etc:

```cpp
void fill_rect(Rect r, Color c) {
    int x0 = std::max(0, static_cast<int>(r.origin.x.raw()));
    int y0 = std::max(0, static_cast<int>(r.origin.y.raw()));
    int x1 = std::min(w_, static_cast<int>(r.origin.x.raw() + r.extent.w.raw()));
    int y1 = std::min(h_, static_cast<int>(r.origin.y.raw() + r.extent.h.raw()));
    // rest unchanged
```

- [ ] **Step 2: Update input_event.hpp**

`MouseScroll` has `float dx, dy` for scroll deltas — these become `DX dx; DY dy;` since scroll deltas are offsets.

`localize_mouse` subtracts the widget rect origin from mouse position. This is `Point - Offset` semantically. The widget rect origin needs to be converted to an Offset for the subtraction:

```cpp
inline InputEvent localize_mouse(const InputEvent& ev, Rect widget_rect) {
    Offset origin_offset{DX{widget_rect.origin.x.raw()}, DY{widget_rect.origin.y.raw()}};
    if (auto* mb = std::get_if<MouseButton>(&ev)) {
        auto local = *mb;
        local.position = local.position - origin_offset;
        return local;
    }
    if (auto* mm = std::get_if<MouseMove>(&ev)) {
        auto local = *mm;
        local.position = local.position - origin_offset;
        return local;
    }
    return ev;
}
```

- [ ] **Step 3: Update layout.hpp**

This is the most complex migration in this task. Key changes:

**SizeHint** — `preferred`, `min`, `max`, `cross` are raw floats representing either width or height depending on axis orientation. These are inherently axis-polymorphic (they represent "the main axis dimension" not specifically Width or Height). Keep them as `float` with a comment explaining why — this is a justified exception to the "no raw floats" rule. The alternative (templating SizeHint on axis) would add complexity with no safety gain since the axis is always known at the call site.

**layout_arrange** — receives `Rect available`, accesses `.origin.x`, `.origin.y`, `.extent.w`, `.extent.h`. Internal arithmetic uses raw floats for the axis-polymorphic offset/remaining/expand_share calculations (same justification as SizeHint).

**translate_draw_list** — takes `DX dx, DY dy` instead of `float dx, float dy`. The body mutates commands by adding offsets. Since commands store strong types, the mutations use `.raw()` and reconstruct:

```cpp
inline void translate_draw_list(DrawList& dl, DX dx, DY dy) {
    for (auto& cmd : dl.commands) {
        std::visit([dx, dy](auto& c) {
            if constexpr (requires { c.rect; }) {
                c.rect.origin = Point{X{c.rect.origin.x.raw() + dx.raw()},
                                      Y{c.rect.origin.y.raw() + dy.raw()}};
            } else if constexpr (requires { c.origin; }) {
                c.origin = Point{X{c.origin.x.raw() + dx.raw()},
                                 Y{c.origin.y.raw() + dy.raw()}};
            }
        }, cmd);
    }
}
```

**layout_flatten** — calls `translate_draw_list` with `DX{node.allocated.origin.x.raw()}` and `DY{node.allocated.origin.y.raw()}`.

**layout_arrange** — internal child rect construction:

```cpp
Rect child_rect;
if (horizontal) {
    child_rect = Rect{Point{X{available.origin.x.raw() + offset},
                             available.origin.y},
                      Size{Width{main_size}, available.extent.h}};
} else {
    child_rect = Rect{Point{available.origin.x,
                             Y{available.origin.y.raw() + offset}},
                      Size{available.extent.w, Height{main_size}}};
}
```

**layout_measure** — uses `bounding_box()` which returns `Rect`. Extract `.extent.w.raw()` and `.extent.h.raw()` for the axis-polymorphic SizeHint floats.

- [ ] **Step 4: Build and run all tests**

Run: `meson test -C builddir --verbose`
Expected: Compilation may fail on downstream files (delegate.hpp, widget_tree.hpp) that use `{x, y, w, h}` Rect syntax. That's expected — those files are migrated in the next task. If compilation succeeds (because the old code still compiles with the new types), all tests should pass.

**Important:** If compilation fails on delegate.hpp or widget_tree.hpp, add transitional helper functions to draw_list.hpp temporarily (same pattern as the clip_push migration). These are removed in Task 4.

- [ ] **Step 5: Commit**

```bash
git add include/prism/core/pixel_buffer.hpp include/prism/core/input_event.hpp include/prism/core/layout.hpp
git commit -m "refactor: migrate pixel_buffer, input_event, layout to strong types

SizeHint keeps raw floats (axis-polymorphic by design).
translate_draw_list takes DX/DY. localize_mouse uses Point - Offset."
```

---

### Task 4: Migrate delegate.hpp — all delegate constants and record/handle_input bodies

**Files:**
- Modify: `include/prism/core/delegate.hpp`

This is a large mechanical migration. Every `{x, y, w, h}` becomes `Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}`, every `{x, y}` Point becomes `Point{X{x}, Y{y}}`, and delegate constants become strong types.

- [ ] **Step 1: Update delegate constants and inline record/handle_input bodies**

Key changes throughout the file:

**Constants** (example from Delegate<Checkbox>):
```cpp
// Before
static constexpr float widget_w = 200.f, widget_h = 30.f;
static constexpr float box_size = 16.f;

// After
static constexpr Width widget_w{200.f};
static constexpr Height widget_h{30.f};
static constexpr Width box_size_w{16.f};   // box is square but Width/Height are distinct
static constexpr Height box_size_h{16.f};
```

**draw_check_box** — takes `X x, Y y` instead of `float x, float y`:
```cpp
inline void draw_check_box(DrawList& dl, X x, Y y, bool checked,
                           const WidgetVisualState& vs) {
    constexpr Width box_w{16.f};
    constexpr Height box_h{16.f};
    // ...
    dl.filled_rect(Rect{Point{x, y}, Size{box_w, box_h}}, fill);
    dl.text("\xe2\x9c\x93", Point{x + DX{2}, Y{y.raw() + 1}}, 13, ...);
    // ...
    dl.rect_outline(Rect{Point{x, y}, Size{box_w, box_h}}, ...);
}
```

**char_width** — returns `Width` instead of `float`:
```cpp
inline Width char_width(float font_size) { return Width{0.6f * font_size}; }
```

**TextEditState** — scroll_offset becomes `DX`:
```cpp
struct TextEditState {
    size_t cursor = 0;
    DX scroll_offset{0.f};
};
```

**TextAreaEditState** — scroll_y becomes `DY`:
```cpp
struct TextAreaEditState {
    size_t cursor = 0;
    DY scroll_y{0.f};
};
```

**DropdownEditState** — popup_rect uses composed Rect:
```cpp
struct DropdownEditState {
    bool open = false;
    size_t highlighted = 0;
    Rect popup_rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
};
```

Apply the same pattern to every delegate's `record()` and `handle_input()` methods. For inline bodies in delegate.hpp (Delegate<T>, Delegate<StringLike>, Delegate<Checkbox>, Delegate<bool>, Delegate<Label<T>>, Delegate<Slider<T>>, Delegate<Button>), convert all coordinate literals.

For declared-but-not-defined methods (TextField, Password, TextArea, Dropdown, ScopedEnum) — only the constants in the struct need updating here. The method bodies in widget_tree.hpp are migrated in Task 5.

**Slider handle_input** — the computation `mb->position.x / track_w` divides an `X` by a `Width`. This needs `.raw()`:
```cpp
float t = std::clamp(mb->position.x.raw() / track_w.raw(), 0.f, 1.f);
```

- [ ] **Step 2: Build (expect widget_tree.hpp errors)**

Run: `meson setup builddir --reconfigure && meson compile -C builddir 2>&1 | head -50`

Compilation will likely fail in widget_tree.hpp where method bodies still use old types. This is expected — Task 5 handles it.

- [ ] **Step 3: Commit (partial — compiles only delegate.hpp changes)**

If the build fails, commit just delegate.hpp:
```bash
git add include/prism/core/delegate.hpp
git commit -m "refactor: migrate delegate.hpp constants and inline bodies to strong types

Widget dimensions use Width/Height, positions use X/Y, offsets use DX/DY.
char_width returns Width. TextEditState.scroll_offset is DX."
```

---

### Task 5: Migrate widget_tree.hpp — all detail:: helpers and delegate method bodies

**Files:**
- Modify: `include/prism/core/widget_tree.hpp`

This is the largest migration — all the detail:: functions and all delegate method bodies defined in widget_tree.hpp.

- [ ] **Step 1: Update detail:: constants**

```cpp
// text field
constexpr Width tf_widget_w{200.f};
constexpr Height tf_widget_h{30.f};
constexpr Width tf_padding_w{4.f};    // padding as width for x-axis
constexpr Height tf_padding_h{4.f};   // padding as height for y-axis
constexpr float tf_font_size = 14.f;  // font size stays float (not a coordinate)
constexpr Width tf_cursor_w{2.f};

// text area
constexpr Width ta_widget_w{200.f};
constexpr Width ta_padding_w{4.f};
constexpr Height ta_padding_h{4.f};
constexpr float ta_font_size = 14.f;
constexpr Height ta_line_height{ta_font_size * 1.4f};
constexpr Width ta_cursor_w{2.f};

// dropdown
constexpr Width dd_widget_w{200.f};
constexpr Height dd_widget_h{30.f};
constexpr Width dd_padding_w{8.f};
constexpr float dd_font_size = 14.f;
constexpr Height dd_option_h{28.f};
```

- [ ] **Step 2: Update text_field_record**

```cpp
template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field,
                       const WidgetNode& node, DisplayFn display_fn) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    auto& es = get_text_edit_state(node);
    Width cw = char_width(tf_font_size);

    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{tf_widget_w, tf_widget_h}}, bg);

    if (vs.focused)
        dl.rect_outline(Rect{Point{X{-1}, Y{-1}},
                              Size{tf_widget_w + Width{2}, tf_widget_h + Height{2}}},
                        Color::rgba(80, 160, 240), 2.0f);

    Width text_area_w = tf_widget_w - tf_padding_w * 2.f;
    dl.clip_push(Point{X{tf_padding_w.raw()}, Y{0}}, Size{text_area_w, tf_widget_h});

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, Point{X{0}, Y{tf_padding_h.raw() + 2}}, tf_font_size,
                Color::rgba(120, 120, 130));
    } else {
        DX text_dx = -es.scroll_offset;
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, Point{X{text_dx.raw()}, Y{tf_padding_h.raw() + 2}},
                tf_font_size, Color::rgba(220, 220, 220));
    }

    if (vs.focused) {
        DX cursor_dx = DX{static_cast<float>(es.cursor) * cw.raw()} - es.scroll_offset;
        dl.filled_rect(Rect{Point{X{cursor_dx.raw()}, Y{tf_padding_h.raw()}},
                            Size{tf_cursor_w, tf_widget_h - tf_padding_h * 2.f}},
                       Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
}
```

- [ ] **Step 3: Update text_field_handle_input**

Convert all coordinate arithmetic. Key patterns:
- `mb->position.x` is now `X`, use `.raw()` for arithmetic with size_t
- `es.scroll_offset` is `DX`
- `char_width()` returns `Width`

```cpp
// click-to-position:
float rel_x = mb->position.x.raw() - tf_padding_w.raw() + es.scroll_offset.raw();
size_t pos = std::clamp(static_cast<size_t>(rel_x / cw.raw() + 0.5f), size_t{0}, len);

// scroll update:
float cursor_px = static_cast<float>(es.cursor) * cw.raw();
float text_area_w_raw = (tf_widget_w - tf_padding_w * 2.f).raw();
if (cursor_px - es.scroll_offset.raw() > text_area_w_raw)
    es.scroll_offset = DX{cursor_px - text_area_w_raw};
if (cursor_px - es.scroll_offset.raw() < 0)
    es.scroll_offset = DX{cursor_px};
```

- [ ] **Step 4: Update text_area_record and text_area_handle_input**

Same pattern. Key changes:
- `ta_padding` split into `ta_padding_w` (Width) and `ta_padding_h` (Height)
- `es.scroll_y` is `DY`
- Line y-positions: `float y = i * ta_line_height - es.scroll_y` becomes `Height y_h = Height{static_cast<float>(i)} * ta_line_height.raw() - Height{es.scroll_y.raw()};` — or use `.raw()` for the arithmetic since it's mixing size_t indices with Height.

In practice, the inner rendering loops deal with computed pixel positions from indices — use `.raw()` for the arithmetic and wrap back into strong types at the draw call boundary.

- [ ] **Step 5: Update dropdown_record and dropdown_handle_input**

Same pattern. Popup position arithmetic uses `dd_widget_h`, `dd_option_h`, etc.

- [ ] **Step 6: Build and run all tests**

Run: `meson test -C builddir --verbose`
Expected: All tests PASS.

- [ ] **Step 7: Commit**

```bash
git add include/prism/core/widget_tree.hpp
git commit -m "refactor: migrate widget_tree.hpp to strong coordinate types

All detail:: constants, text_field/text_area/dropdown record and
handle_input use Width/Height/X/Y/DX/DY. Raw float arithmetic
used only for index-to-pixel conversions."
```

---

### Task 6: Migrate app.hpp, software_backend.cpp, software_renderer.hpp, and all test files

**Files:**
- Modify: `include/prism/core/app.hpp`
- Modify: `src/backends/software_backend.cpp`
- Modify: `include/prism/core/software_renderer.hpp`
- Modify: All test files that construct Point/Rect/Size

- [ ] **Step 1: Update app.hpp**

The Frame class methods already take the right types (Point, Size, Rect) — they just need to match the new Rect structure. The window dimension cast:

```cpp
// Before
{0, 0, static_cast<float>(width_), static_cast<float>(height_)}

// After
Rect{Point{X{0}, Y{0}}, Size{Width{static_cast<float>(width_)}, Height{static_cast<float>(height_)}}}
```

- [ ] **Step 2: Update software_backend.cpp**

Replace the `to_sdl` helper:

```cpp
SDL_FRect to_sdl(Rect r) {
    return {r.origin.x.raw(), r.origin.y.raw(), r.extent.w.raw(), r.extent.h.raw()};
}
```

Update TextCmd rendering:
```cpp
SDL_FRect dst = {cmd.origin.x.raw(), cmd.origin.y.raw(),
                 static_cast<float>(surface->w),
                 static_cast<float>(surface->h)};
```

Update ClipPush rendering:
```cpp
SDL_Rect r = {static_cast<int>(cmd.rect.origin.x.raw()),
              static_cast<int>(cmd.rect.origin.y.raw()),
              static_cast<int>(cmd.rect.extent.w.raw()),
              static_cast<int>(cmd.rect.extent.h.raw())};
```

Update mouse event construction — SDL gives raw floats, wrap them:
```cpp
case SDL_EVENT_MOUSE_MOTION:
    event_cb(MouseMove{Point{X{ev.motion.x}, Y{ev.motion.y}}});
    break;
case SDL_EVENT_MOUSE_BUTTON_DOWN:
    event_cb(MouseButton{
        Point{X{ev.button.x}, Y{ev.button.y}}, ev.button.button, true});
    break;
// etc.
```

MouseScroll:
```cpp
event_cb(MouseScroll{
    Point{X{ev.wheel.mouse_x}, Y{ev.wheel.mouse_y}},
    DX{ev.wheel.x}, DY{ev.wheel.y}});
```

- [ ] **Step 3: Update software_renderer.hpp**

Minimal change — `rasterise(FilledRect)` calls `buf_.fill_rect(cmd.rect, cmd.color)` which now works because pixel_buffer was already updated.

- [ ] **Step 4: Update all test files**

Every test file that constructs `{x, y, w, h}` Rect or `{x, y}` Point needs updating. Use the `make_rect` helper pattern or construct directly. Key files:
- `test_layout.cpp`
- `test_hit_test.cpp`
- `test_software_renderer.cpp`
- `test_pixel_buffer.cpp`
- `test_delegate.cpp`
- `test_widget_tree.cpp`
- `test_text_field.cpp`
- `test_password.cpp`
- `test_text_area.cpp`
- `test_dropdown.cpp`
- `test_model_app.cpp`
- `test_ui.cpp`
- `test_app.cpp`

Pattern for each test:
```cpp
// Before
prism::Point{100.f, 200.f}
prism::Rect{0, 0, 800, 600}
MouseButton{{50, 25}, 1, true}

// After
prism::Point{prism::X{100.f}, prism::Y{200.f}}
prism::Rect{prism::Point{prism::X{0}, prism::Y{0}}, prism::Size{prism::Width{800}, prism::Height{600}}}
MouseButton{Point{X{50}, Y{25}}, 1, true}
```

For tests with many constructions, add `using namespace prism;` at file scope or use a `make_rect` helper.

- [ ] **Step 5: Build and run all tests**

Run: `meson test -C builddir --verbose`
Expected: ALL 25 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/prism/core/app.hpp src/backends/software_backend.cpp include/prism/core/software_renderer.hpp tests/
git commit -m "refactor: complete strong types migration across all files

Backend uses .raw() at SDL boundary. Mouse events wrap raw SDL coords
in strong types. All test files updated. 25/25 tests pass."
```

---

### Task 7: Update examples and clean up

**Files:**
- Modify: `examples/model_dashboard.cpp` (if it uses any coordinate types directly — unlikely since it only uses Field<T> sentinels)
- Verify: no remaining raw float coordinates in public API

- [ ] **Step 1: Verify no raw floats leak into public API**

Search for any remaining `float` parameters in public-facing functions that represent coordinates:

```bash
grep -rn 'float.*\b[xywh]\b' include/prism/core/ --include='*.hpp' | grep -v '//' | grep -v 'raw()'
```

Fix any remaining raw float coordinate parameters.

- [ ] **Step 2: Run full test suite one final time**

Run: `meson test -C builddir --verbose`
Expected: ALL tests PASS.

- [ ] **Step 3: Commit (if any changes)**

```bash
git add -A
git commit -m "chore: verify no raw float coordinates in public API"
```
