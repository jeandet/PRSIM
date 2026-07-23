#pragma once

#include <cstddef>
#include <string_view>

namespace prism::core {

// A structural compile-time string, usable as a template non-type parameter.
// Needed because C++26 annotations ([[=expr]]) must be constants of
// structural type, and prism::core::label/section (see
// reflect_annotations.hpp) carry a string.
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
