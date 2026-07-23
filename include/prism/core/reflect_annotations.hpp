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
