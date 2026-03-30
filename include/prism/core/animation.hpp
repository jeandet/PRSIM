#pragma once

#include <prism/core/types.hpp>

namespace prism {

namespace ease {

inline EasedProgress linear(Progress t) {
    return EasedProgress{t.raw()};
}

inline EasedProgress in_quad(Progress t) {
    float v = t.raw();
    return EasedProgress{v * v};
}

inline EasedProgress out_quad(Progress t) {
    float v = t.raw();
    return EasedProgress{v * (2.f - v)};
}

inline EasedProgress in_out_quad(Progress t) {
    float v = t.raw();
    return v < 0.5f
        ? EasedProgress{2.f * v * v}
        : EasedProgress{-1.f + (4.f - 2.f * v) * v};
}

inline EasedProgress in_cubic(Progress t) {
    float v = t.raw();
    return EasedProgress{v * v * v};
}

inline EasedProgress out_cubic(Progress t) {
    float v = t.raw() - 1.f;
    return EasedProgress{v * v * v + 1.f};
}

inline EasedProgress in_out_cubic(Progress t) {
    float v = t.raw();
    return v < 0.5f
        ? EasedProgress{4.f * v * v * v}
        : EasedProgress{(v - 1.f) * (2.f * v - 2.f) * (2.f * v - 2.f) + 1.f};
}

} // namespace ease

// lerp overloads
inline float lerp(float a, float b, EasedProgress t) {
    return a + (b - a) * t.raw();
}

template <typename Tag>
Scalar<Tag> lerp(Scalar<Tag> a, Scalar<Tag> b, EasedProgress t) {
    return Scalar<Tag>{a.raw() + (b.raw() - a.raw()) * t.raw()};
}

// Lerpable concept
template <typename T>
concept Lerpable = requires(T a, T b, EasedProgress t) {
    { lerp(a, b, t) } -> std::convertible_to<T>;
};

} // namespace prism
