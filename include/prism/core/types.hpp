#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>

namespace prism::core {

template <typename Tag>
struct Scalar {
    constexpr Scalar() : v_(0.f) {}
    constexpr explicit Scalar(float v) : v_(v) {}
    [[nodiscard]] constexpr float raw() const { return v_; }
    constexpr auto operator<=>(const Scalar&) const = default;
private:
    float v_;
};

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

template <typename LTag, typename RTag> struct AddResult;
template <typename LTag, typename RTag> struct SubResult;
template <typename Tag> struct ScaleResult;

template <typename LTag, typename RTag> using add_result_t = typename AddResult<LTag, RTag>::type;
template <typename LTag, typename RTag> using sub_result_t = typename SubResult<LTag, RTag>::type;
template <typename Tag> using scale_result_t = typename ScaleResult<Tag>::type;

template <typename L, typename R> concept Addable = requires { typename AddResult<L, R>::type; };
template <typename L, typename R> concept Subtractable = requires { typename SubResult<L, R>::type; };
template <typename T> concept Scalable = requires { typename ScaleResult<T>::type; };

// Position rules: X
template <> struct SubResult<XTag, XTag>   { using type = DXTag; };
template <> struct AddResult<XTag, DXTag>  { using type = XTag; };
template <> struct SubResult<XTag, DXTag>  { using type = XTag; };

// Position rules: Y
template <> struct SubResult<YTag, YTag>   { using type = DYTag; };
template <> struct AddResult<YTag, DYTag>  { using type = YTag; };
template <> struct SubResult<YTag, DYTag>  { using type = YTag; };

// Offset rules: DX
template <> struct AddResult<DXTag, DXTag> { using type = DXTag; };
template <> struct SubResult<DXTag, DXTag> { using type = DXTag; };
template <> struct ScaleResult<DXTag>      { using type = DXTag; };

// Offset rules: DY
template <> struct AddResult<DYTag, DYTag> { using type = DYTag; };
template <> struct SubResult<DYTag, DYTag> { using type = DYTag; };
template <> struct ScaleResult<DYTag>      { using type = DYTag; };

// Dimension rules: Width
template <> struct AddResult<WidthTag, WidthTag> { using type = WidthTag; };
template <> struct SubResult<WidthTag, WidthTag> { using type = WidthTag; };
template <> struct ScaleResult<WidthTag>         { using type = WidthTag; };

// Dimension rules: Height
template <> struct AddResult<HeightTag, HeightTag> { using type = HeightTag; };
template <> struct SubResult<HeightTag, HeightTag> { using type = HeightTag; };
template <> struct ScaleResult<HeightTag>          { using type = HeightTag; };

// Scalar operators
template <typename L, typename R> requires Addable<L, R>
constexpr Scalar<add_result_t<L, R>> operator+(Scalar<L> a, Scalar<R> b) {
    return Scalar<add_result_t<L, R>>{a.raw() + b.raw()};
}

template <typename L, typename R> requires Subtractable<L, R>
constexpr Scalar<sub_result_t<L, R>> operator-(Scalar<L> a, Scalar<R> b) {
    return Scalar<sub_result_t<L, R>>{a.raw() - b.raw()};
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator*(Scalar<Tag> a, float s) {
    return Scalar<scale_result_t<Tag>>{a.raw() * s};
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator*(float s, Scalar<Tag> a) {
    return Scalar<scale_result_t<Tag>>{s * a.raw()};
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator/(Scalar<Tag> a, float s) {
    return Scalar<scale_result_t<Tag>>{a.raw() / s};
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<scale_result_t<Tag>> operator-(Scalar<Tag> a) {
    return Scalar<scale_result_t<Tag>>{-a.raw()};
}

// Compound assignment operators
template <typename L, typename R> requires (Addable<L, R> && std::same_as<L, add_result_t<L, R>>)
constexpr Scalar<L>& operator+=(Scalar<L>& a, Scalar<R> b) {
    a = a + b; return a;
}

template <typename L, typename R> requires (Subtractable<L, R> && std::same_as<L, sub_result_t<L, R>>)
constexpr Scalar<L>& operator-=(Scalar<L>& a, Scalar<R> b) {
    a = a - b; return a;
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<Tag>& operator*=(Scalar<Tag>& a, float s) {
    a = a * s; return a;
}

template <typename Tag> requires Scalable<Tag>
constexpr Scalar<Tag>& operator/=(Scalar<Tag>& a, float s) {
    a = a / s; return a;
}

// Integer scalar (for item indices and counts)
template <typename Tag>
struct IntScalar {
    constexpr IntScalar() : v_(0) {}
    constexpr explicit IntScalar(size_t v) : v_(v) {}
    [[nodiscard]] constexpr size_t raw() const { return v_; }
    constexpr auto operator<=>(const IntScalar&) const = default;
private:
    size_t v_;
};

struct ItemIndexTag {};
struct ItemCountTag {};

using ItemIndex = IntScalar<ItemIndexTag>;
using ItemCount = IntScalar<ItemCountTag>;

// Affine arithmetic rules for IntScalar (mirrors Scalar patterns)
template <typename LTag, typename RTag> struct IntAddResult;
template <typename LTag, typename RTag> struct IntSubResult;

template <> struct IntAddResult<ItemIndexTag, ItemCountTag> { using type = ItemIndexTag; };
template <> struct IntAddResult<ItemCountTag, ItemCountTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemIndexTag, ItemIndexTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemCountTag, ItemCountTag> { using type = ItemCountTag; };
template <> struct IntSubResult<ItemIndexTag, ItemCountTag> { using type = ItemIndexTag; };

template <typename L, typename R> concept IntAddable = requires { typename IntAddResult<L, R>::type; };
template <typename L, typename R> concept IntSubtractable = requires { typename IntSubResult<L, R>::type; };

template <typename L, typename R> requires IntAddable<L, R>
constexpr IntScalar<typename IntAddResult<L, R>::type> operator+(IntScalar<L> a, IntScalar<R> b) {
    return IntScalar<typename IntAddResult<L, R>::type>{a.raw() + b.raw()};
}

template <typename L, typename R> requires IntSubtractable<L, R>
constexpr IntScalar<typename IntSubResult<L, R>::type> operator-(IntScalar<L> a, IntScalar<R> b) {
    return IntScalar<typename IntSubResult<L, R>::type>{a.raw() - b.raw()};
}

// Composite types
struct Offset {
    DX dx; DY dy;
    constexpr auto operator<=>(const Offset&) const = default;
    [[nodiscard]] float length() const { return std::sqrt(dx.raw() * dx.raw() + dy.raw() * dy.raw()); }
};

struct Point {
    X x; Y y;
    constexpr auto operator<=>(const Point&) const = default;
};

struct Size {
    Width w; Height h;
    constexpr auto operator<=>(const Size&) const = default;
};

struct Rect {
    Point origin; Size extent;
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

    [[nodiscard]] constexpr Rect intersect(Rect other) const {
        float ax1 = origin.x.raw(), ay1 = origin.y.raw();
        float ax2 = ax1 + extent.w.raw(), ay2 = ay1 + extent.h.raw();
        float bx1 = other.origin.x.raw(), by1 = other.origin.y.raw();
        float bx2 = bx1 + other.extent.w.raw(), by2 = by1 + other.extent.h.raw();
        float ix1 = ax1 > bx1 ? ax1 : bx1;
        float iy1 = ay1 > by1 ? ay1 : by1;
        float ix2 = ax2 < bx2 ? ax2 : bx2;
        float iy2 = ay2 < by2 ? ay2 : by2;
        float w = ix2 > ix1 ? ix2 - ix1 : 0.f;
        float h = iy2 > iy1 ? iy2 - iy1 : 0.f;
        return {Point{X{ix1}, Y{iy1}}, Size{Width{w}, Height{h}}};
    }
};

// Animation progress types
struct ProgressTag {};
struct EasedProgressTag {};

using Progress = Scalar<ProgressTag>;
using EasedProgress = Scalar<EasedProgressTag>;

// Composite operators
constexpr Offset operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
constexpr Point operator+(Point p, Offset o) { return {p.x + o.dx, p.y + o.dy}; }
constexpr Point operator-(Point p, Offset o) { return {p.x - o.dx, p.y - o.dy}; }
constexpr Offset operator+(Offset a, Offset b) { return {a.dx + b.dx, a.dy + b.dy}; }
constexpr Offset operator-(Offset a, Offset b) { return {a.dx - b.dx, a.dy - b.dy}; }
constexpr Offset operator*(Offset o, float s) { return {o.dx * s, o.dy * s}; }
constexpr Offset operator*(float s, Offset o) { return {s * o.dx, s * o.dy}; }
constexpr Offset operator/(Offset o, float s) { return {o.dx / s, o.dy / s}; }
constexpr Offset operator-(Offset o) { return {-o.dx, -o.dy}; }
constexpr Size operator+(Size a, Size b) { return {a.w + b.w, a.h + b.h}; }
constexpr Size operator-(Size a, Size b) { return {a.w - b.w, a.h - b.h}; }
constexpr Size operator*(Size s, float f) { return {s.w * f, s.h * f}; }
constexpr Size operator*(float f, Size s) { return {f * s.w, f * s.h}; }
constexpr Size operator/(Size s, float f) { return {s.w / f, s.h / f}; }

struct Color {
    uint8_t r, g, b, a;

    static constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return {r, g, b, a};
    }
};

inline Color lerp(Color a, Color b, EasedProgress t) {
    auto mix = [&](uint8_t x, uint8_t y) -> uint8_t {
        return static_cast<uint8_t>(x + (y - x) * t.raw() + 0.5f);
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

enum class ExpandAxis { None, Both, Horizontal, Vertical };

// User-defined literals
namespace literals {
constexpr X operator""_x(long double v) { return X{static_cast<float>(v)}; }
constexpr Y operator""_y(long double v) { return Y{static_cast<float>(v)}; }
constexpr DX operator""_dx(long double v) { return DX{static_cast<float>(v)}; }
constexpr DY operator""_dy(long double v) { return DY{static_cast<float>(v)}; }
constexpr Width operator""_w(long double v) { return Width{static_cast<float>(v)}; }
constexpr Height operator""_h(long double v) { return Height{static_cast<float>(v)}; }
} // namespace literals

} // namespace prism::core
