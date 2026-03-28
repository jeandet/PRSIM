#pragma once

#include <prism/core/draw_list.hpp> // Point

#include <cstdint>
#include <string>
#include <variant>

namespace prism {

struct MouseMove   { Point position; };
struct MouseButton { Point position; uint8_t button; bool pressed; };
struct MouseScroll { Point position; float dx, dy; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct TextInput   { std::string text; };
struct WindowResize { int width, height; };
struct WindowClose {};

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput,
    WindowResize, WindowClose
>;

namespace keys {
    inline constexpr int32_t tab       = 0x09;         // matches SDLK_TAB
    inline constexpr int32_t space     = 0x20;         // matches SDLK_SPACE
    inline constexpr int32_t enter     = 0x0D;         // matches SDLK_RETURN
    inline constexpr int32_t backspace = 0x08;         // matches SDLK_BACKSPACE
    inline constexpr int32_t delete_   = 0x7F;         // matches SDLK_DELETE
    inline constexpr int32_t right     = 0x4000'004F;  // matches SDLK_RIGHT
    inline constexpr int32_t left      = 0x4000'0050;  // matches SDLK_LEFT
    inline constexpr int32_t home      = 0x4000'004A;  // matches SDLK_HOME
    inline constexpr int32_t end       = 0x4000'004D;  // matches SDLK_END
}

namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT
}

} // namespace prism
