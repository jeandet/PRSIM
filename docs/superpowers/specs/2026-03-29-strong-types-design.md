# Strong Coordinate Types

> **Goal:** Replace all raw `float` coordinate values in PRISM's API with zero-cost strong types that enforce axis separation, affine algebra rules, and position/offset/dimension distinctions at compile time.

## Motivation

Today `Point{float, float}`, `Size{float, float}`, and `Rect{float, float, float, float}` are bags of floats. Nothing prevents passing a height where a width is expected, adding two positions, or mixing X and Y axes. The clip_push refactor showed how easy it is to get coordinate offsets wrong. Strong types eliminate this class of bug at compile time with zero runtime cost.

## Design Principles

- **Zero raw numerical types in the public API** unless strongly justified (e.g., `Offset::length()` returns `float` because magnitude is axis-independent).
- **Strict by default** — start with minimal allowed operations, relax only with justification.
- **Zero runtime cost** — all types wrap a single `float`, all operators are `constexpr`, optimized identically to raw floats.
- **Leverage C++20-26** — concepts, `<=>`, `constexpr`/`consteval`, `requires` clauses, user-defined literals.

## Scalar Foundation

### Template

A single class template `Scalar<Tag>` wrapping a `float`:

```cpp
template <typename Tag>
struct Scalar {
    constexpr explicit Scalar(float v) : v_(v) {}
    constexpr float raw() const { return v_; }
    constexpr auto operator<=>(const Scalar&) const = default;
private:
    float v_;
};
```

- `explicit` constructor prevents implicit conversion from `float`.
- `.raw()` is the only way to extract the float — used at the backend boundary.
- `operator<=>` enables all comparisons within the same tag. Cross-tag comparison is a compile error.

### Tags and Aliases

```cpp
struct XTag {};
struct YTag {};
struct DXTag {};
struct DYTag {};
struct WidthTag {};
struct HeightTag {};

using X = Scalar<XTag>;
using Y = Scalar<YTag>;
using DX = Scalar<DXTag>;
using DY = Scalar<DYTag>;
using Width = Scalar<WidthTag>;
using Height = Scalar<HeightTag>;
```

### Algebra via Traits

Operations are enabled by specializing trait templates. If no specialization exists, the operation doesn't compile.

```cpp
// Subtraction: what type does A - B produce?
template <typename LTag, typename RTag> struct SubResult;  // no default

// Addition: what type does A + B produce?
template <typename LTag, typename RTag> struct AddResult;  // no default

// Scaling: what type does A * float produce?
template <typename Tag> struct ScaleResult;  // no default
```

Operators on `Scalar<Tag>` use `requires` clauses:

```cpp
template <typename L, typename R>
    requires requires { typename AddResult<L, R>::type; }
constexpr Scalar<typename AddResult<L, R>::type>
operator+(Scalar<L> a, Scalar<R> b);
```

### Algebra Rules

**Position (X, Y):**

| Expression | Result | Trait |
|---|---|---|
| `X - X` | `DX` | `SubResult<XTag, XTag>` |
| `X + DX` | `X` | `AddResult<XTag, DXTag>` |
| `X - DX` | `X` | `SubResult<XTag, DXTag>` |
| `Y - Y` | `DY` | `SubResult<YTag, YTag>` |
| `Y + DY` | `Y` | `AddResult<YTag, DYTag>` |
| `Y - DY` | `Y` | `SubResult<YTag, DYTag>` |

**Offset (DX, DY):**

| Expression | Result | Trait |
|---|---|---|
| `DX + DX` | `DX` | `AddResult<DXTag, DXTag>` |
| `DX - DX` | `DX` | `SubResult<DXTag, DXTag>` |
| `DX * float` | `DX` | `ScaleResult<DXTag>` |
| `float * DX` | `DX` | (commutative) |
| `DX / float` | `DX` | `ScaleResult<DXTag>` |
| `DY + DY` | `DY` | `AddResult<DYTag, DYTag>` |
| `DY - DY` | `DY` | `SubResult<DYTag, DYTag>` |
| `DY * float` | `DY` | `ScaleResult<DYTag>` |
| `DY / float` | `DY` | `ScaleResult<DYTag>` |

**Dimension (Width, Height):**

| Expression | Result | Trait |
|---|---|---|
| `Width + Width` | `Width` | `AddResult<WidthTag, WidthTag>` |
| `Width - Width` | `Width` | `SubResult<WidthTag, WidthTag>` |
| `Width * float` | `Width` | `ScaleResult<WidthTag>` |
| `Width / float` | `Width` | `ScaleResult<WidthTag>` |
| `Height + Height` | `Height` | `AddResult<HeightTag, HeightTag>` |
| `Height - Height` | `Height` | `SubResult<HeightTag, HeightTag>` |
| `Height * float` | `Height` | `ScaleResult<HeightTag>` |
| `Height / float` | `Height` | `ScaleResult<HeightTag>` |

**Compile errors (no trait specialization):**

- `X + X` — adding positions is meaningless
- `X * float` — scaling a position is meaningless
- `X + Width` — mixing position and dimension
- `Width + DX` — mixing dimension and offset
- `X + Y` — mixing axes
- `DX + DY` — mixing axes

### Unary Negation

`-DX` produces `DX`, `-DY` produces `DY`, `-Width` produces `Width`, `-Height` produces `Height`. Enabled via `ScaleResult` (negation is scaling by -1). Not enabled for positions (`-X` is meaningless).

### User-Defined Literals

For ergonomics in tests and examples:

```cpp
constexpr X operator""_x(long double v);
constexpr Y operator""_y(long double v);
constexpr DX operator""_dx(long double v);
constexpr DY operator""_dy(long double v);
constexpr Width operator""_w(long double v);
constexpr Height operator""_h(long double v);
```

Usage: `Point{10.0_x, 20.0_y}`, `Size{100.0_w, 50.0_h}`.

## Composite Types

### Definitions

```cpp
struct Point  { X x; Y y; };
struct Offset { DX dx; DY dy; };
struct Size   { Width w; Height h; };
struct Rect   { Point origin; Size extent; };
```

### Derived 2D Operators

These forward to scalar operations component-wise:

| Expression | Result |
|---|---|
| `Point - Point` | `Offset` |
| `Point + Offset` | `Point` |
| `Point - Offset` | `Point` |
| `Offset + Offset` | `Offset` |
| `Offset - Offset` | `Offset` |
| `Offset * float` | `Offset` |
| `float * Offset` | `Offset` |
| `Offset / float` | `Offset` |
| `-Offset` | `Offset` |
| `Size * float` | `Size` |
| `float * Size` | `Size` |
| `Size / float` | `Size` |
| `Size + Size` | `Size` |
| `Size - Size` | `Size` |

All composite operators are `constexpr`. `operator==` and `<=>` on all composites via `= default`.

### Composite-Specific Operations

Not derivable from scalar algebra — defined directly on the composite types:

- `Rect::contains(Point) const -> bool` — point-in-rect test
- `Rect::center() const -> Point` — center point
- `Rect::from_corners(Point top_left, Point bottom_right) -> Rect` — alternative construction
- `Offset::length() const -> float` — magnitude (`std::sqrt(dx*dx + dy*dy)`), returns raw `float` because magnitude is axis-independent

## File Organization

### New file: `include/prism/core/types.hpp`

Contains all strong type machinery:
- `Scalar<Tag>` template
- All tag structs
- All trait specializations (`AddResult`, `SubResult`, `ScaleResult`)
- All scalar operators
- All type aliases (`X`, `Y`, `DX`, `DY`, `Width`, `Height`)
- All composite types (`Point`, `Offset`, `Size`, `Rect`)
- All composite operators
- User-defined literals (in `namespace prism::literals`)

No dependencies beyond `<cmath>` and `<compare>`.

### Modified: `include/prism/core/draw_list.hpp`

- `#include <prism/core/types.hpp>` replaces local `Point`/`Size`/`Rect` definitions
- `DrawList` methods use strong types (same signatures, just `Point`/`Size`/`Rect` now come from `types.hpp`)
- `ClipPush` stores `Rect` (which is now `{Point origin; Size extent;}`)

### Modified: all other core headers and source files

- `delegate.hpp`, `widget_tree.hpp` — delegate functions use strong types for coordinates
- `layout.hpp` — layout solver uses `Width`/`Height`/`X`/`Y`
- `hit_test.hpp` — hit testing uses `Point` (now strong-typed)
- `scene_snapshot.hpp` — geometry stored as `Rect`
- `input_event.hpp` — mouse coordinates become `X`/`Y`
- `app.hpp` — Frame class uses strong types
- `software_backend.cpp` — the `.raw()` extraction boundary
- All test files — construct with strong types

## Backend Boundary

Strong types live everywhere in PRISM. At the SDL boundary (and future GPU backends), `.raw()` extracts the float:

```cpp
void SoftwareBackend::render_cmd(const FilledRect& cmd) {
    SDL_FRect r = {cmd.rect.origin.x.raw(), cmd.rect.origin.y.raw(),
                   cmd.rect.extent.w.raw(), cmd.rect.extent.h.raw()};
    // ...
}
```

This is the only place raw floats should appear.

## Testing

### New: `tests/test_types.cpp`

- **Scalar algebra:** every allowed operation produces correct type and value
- **Compile-time safety:** `static_assert(!requires { X{1} + X{2}; })` for every forbidden operation
- **Composite algebra:** Point/Offset/Size/Rect operations
- **Composite-specific:** `contains()`, `center()`, `length()`, `from_corners()`
- **constexpr verification:** operations work at compile time
- **`.raw()` round-trip**
- **Comparison operators:** `==`, `<`, `<=>` within same type, cross-type doesn't compile

### Migration validation

All existing tests updated to use strong types. Identical behavior — this is a pure type-safety refactor.

## Migration Strategy

Bottom-up:
1. Create `types.hpp` with all type machinery + tests
2. Update `draw_list.hpp` to use `types.hpp` (drop local definitions)
3. Fix compilation errors outward through the codebase
4. Update backend to use `.raw()` at the boundary
5. Update all test files

## Out of Scope

- Coordinate space tagging (`Point<Screen>` vs `Point<Local>`) — deferred to Phase 4 when `map_to_parent`/`map_to_global` emerges
- Integer coordinate types (pixel positions) — all coordinates are `float` for now
- 3D types — 2D only
