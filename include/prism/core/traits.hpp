#pragma once

#include <type_traits>

namespace prism::core {

template <typename T>
struct is_field : std::false_type {};

template <typename T>
    requires requires { { T::is_prism_field } -> std::convertible_to<bool>; }
    && T::is_prism_field
    && requires(T t) { t.on_change(); }
struct is_field<T> : std::true_type {};

template <typename T>
inline constexpr bool is_field_v = is_field<T>::value;

template <typename T>
concept field_type = is_field_v<T>;

template <typename T>
struct is_state : std::false_type {};

template <typename T>
    requires requires { typename std::remove_cvref_t<decltype(std::declval<T>().value)>; }
    && requires(T t) { t.on_change(); }
    && (!requires { { T::is_prism_field } -> std::convertible_to<bool>; })
struct is_state<T> : std::true_type {};

template <typename T>
inline constexpr bool is_state_v = is_state<T>::value;

template <typename T>
concept component_type = std::is_class_v<T> && !is_field_v<T> && !is_state_v<T>;

template <typename> class List;

template <typename T>
struct is_list : std::false_type {};

template <typename T>
struct is_list<List<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_list_v = is_list<T>::value;

} // namespace prism::core
