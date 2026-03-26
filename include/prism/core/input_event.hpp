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

} // namespace prism
