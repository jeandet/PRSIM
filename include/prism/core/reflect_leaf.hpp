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
