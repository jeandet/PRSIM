#pragma once

#include <meta>
#include <type_traits>

namespace prism {

// Detect Field<T>
template <typename T>
struct is_field : std::false_type {};

template <typename T>
    requires requires { T::label; typename std::remove_cvref_t<decltype(std::declval<T>().value)>; }
    && requires(T t) { t.on_change(); }
struct is_field<T> : std::true_type {};

template <typename T>
inline constexpr bool is_field_v = is_field<T>::value;

// Visit all Field<T> members of a struct (non-recursive)
template <typename Model, typename Fn>
void for_each_field(Model& model, Fn&& fn) {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M>) {
            fn(member);
        }
    }
}

// Detect a component: struct with Field<T> members (direct or one level nested)
template <typename T>
consteval bool check_is_component() {
    if constexpr (!std::is_class_v<T>) return false;
    else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (is_field_v<M>) found = true;
        }
        return found;
    }
}

template <typename T>
consteval bool check_is_component_recursive() {
    if (check_is_component<T>()) return true;
    if constexpr (!std::is_class_v<T>) return false;
    else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (std::is_class_v<M> && check_is_component<M>()) found = true;
        }
        return found;
    }
}

template <typename T>
inline constexpr bool is_component_v = check_is_component_recursive<T>();

// Visit all members (Field<T> and sub-components) of a struct
template <typename Model, typename Fn>
void for_each_member(Model& model, Fn&& fn) {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked()));
    template for (constexpr auto m : members) {
        auto& member = model.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;
        if constexpr (is_field_v<M> || is_component_v<M>) {
            fn(member);
        }
    }
}

} // namespace prism
