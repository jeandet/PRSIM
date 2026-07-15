#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
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
