#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/fixed_string.hpp>
#include <prism/ui/delegate.hpp>

#include <tuple>
#include <type_traits>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#include <ranges>

namespace prism::inspector {
using namespace prism::core;
using namespace prism::ui;

// --- Annotations ---------------------------------------------------------
// Attach to a member of a plain struct passed to Inspector<T>/FieldMirror<T>:
//
//   struct Settings {
//       [[=prism::inspector::skip]]                  int internal_version;
//       [[=prism::inspector::readonly]]               std::string device_id;
//       [[=prism::inspector::label<"Sample Rate">]]   int sample_rate;
//       [[=prism::inspector::section<"Audio">]]       float volume;
//   };

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

template <typename T>
concept MirrorLeaf = Numeric<T> || StringLike<T> || ScopedEnum<T>;

// One labeled row: a static name caption + the live editable value.
template <typename M>
struct LeafSlot {
    Field<Label<std::string>> name{};
    Field<M> value{};
};

template <typename T>
concept NestedMirrorSlot = requires(T& t) { t.slots; };

template <typename T> struct FieldMirror;

template <typename M>
using MirrorSlot = std::conditional_t<MirrorLeaf<M>, LeafSlot<M>, FieldMirror<M>>;

template <typename T>
consteval std::meta::info field_mirror_tuple_info() {
    std::vector<std::meta::info> slot_types;
    for (auto m : std::meta::nonstatic_data_members_of(
             ^^T, std::meta::access_context::unchecked())) {
        auto mtype = std::meta::type_of(m);
        slot_types.push_back(std::meta::substitute(^^MirrorSlot, {mtype}));
    }
    return std::meta::substitute(^^std::tuple, slot_types);
}

template <typename T>
using FieldMirrorTuple = [: field_mirror_tuple_info<T>() :];

template <typename T>
struct FieldMirror {
    FieldMirrorTuple<T> slots;

    void sync_from(const T& v) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.sync_from(v.[:m:]);
            } else {
                slot.name.set(Label<std::string>{std::string(std::meta::identifier_of(m))});
                slot.value.set(v.[:m:]);
            }
        }
    }

    T build() const {
        T out{};
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            constexpr auto m = members[i];
            const auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                out.[:m:] = slot.build();
            } else {
                out.[:m:] = slot.value.get();
            }
        }
        return out;
    }

    template <typename Fn>
    void for_each_leaf(Fn&& fn) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        template for (constexpr auto i : std::views::iota(std::size_t{0}, members.size())) {
            auto& slot = std::get<i>(slots);
            using SlotT = std::remove_cvref_t<decltype(slot)>;
            if constexpr (NestedMirrorSlot<SlotT>) {
                slot.for_each_leaf(fn);
            } else {
                fn(slot.value);
            }
        }
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        std::apply([&](auto&... s) { vb.vstack(s...); }, slots);
    }
};

} // namespace prism::inspector

#endif // __cpp_impl_reflection
