#pragma once

#include <prism/core/draw_list.hpp> // Point

#include <cstdint>
#include <variant>

namespace prism {

struct MouseMove   { Point position; };
struct MouseButton { Point position; uint8_t button; bool pressed; };
struct MouseScroll { Point position; float dx, dy; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct WindowResize { int width, height; };
struct WindowClose {};

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease,
    WindowResize, WindowClose
>;

namespace keys {
    inline constexpr int32_t tab   = 0x09;   // matches SDLK_TAB
    inline constexpr int32_t space = 0x20;   // matches SDLK_SPACE
    inline constexpr int32_t enter = 0x0D;   // matches SDLK_RETURN
}

namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT
}

} // namespace prism
