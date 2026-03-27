#pragma once

#include <prism/core/draw_list.hpp>

#include <cstdint>

namespace prism {

// Bitmask of interactive states a widget can be in.
enum class WidgetState : uint8_t {
    Normal   = 0,
    Hovered  = 1 << 0,
    Focused  = 1 << 1,
    Pressed  = 1 << 2,
    Disabled = 1 << 3,
};

[[nodiscard]] constexpr WidgetState operator|(WidgetState a, WidgetState b)
{
    return static_cast<WidgetState>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool has(WidgetState mask, WidgetState flag)
{
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(flag)) != 0;
}

// Placeholder — will grow with real style structs later.
struct Theme {};

// Carried through the widget tree during record().
// Provides theme access and current interaction state.
struct Context {
    const Theme& theme;
    WidgetState  state = WidgetState::Normal;
};

// A widget is any type that can record draw commands given a context.
template <typename T>
concept Widget = requires(const T w, DrawList& dl, const Context& ctx) {
    { w.record(dl, ctx) } -> std::same_as<void>;
};

} // namespace prism
