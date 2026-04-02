#pragma once

#include <prism/core/types.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace prism::input {
using namespace prism::core;


struct MouseMove   { Point position; };
struct MouseButton { Point position; uint8_t button; bool pressed; };
struct MouseScroll { Point position; DX dx; DY dy; };
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
    inline constexpr int32_t up        = 0x4000'0052;  // matches SDLK_UP
    inline constexpr int32_t down      = 0x4000'0051;  // matches SDLK_DOWN
    inline constexpr int32_t escape    = 0x1B;          // matches SDLK_ESCAPE
    inline constexpr int32_t page_up   = 0x4000'004B;  // matches SDLK_PAGEUP
    inline constexpr int32_t page_down = 0x4000'004E;  // matches SDLK_PAGEDOWN
}

namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT
}

namespace buttons {
    inline constexpr uint8_t left   = 1;  // matches SDL_BUTTON_LEFT
    inline constexpr uint8_t middle = 2;  // matches SDL_BUTTON_MIDDLE
    inline constexpr uint8_t right  = 3;  // matches SDL_BUTTON_RIGHT
}

inline InputEvent localize_mouse(const InputEvent& ev, Rect widget_rect) {
    Offset off{DX{widget_rect.origin.x.raw()}, DY{widget_rect.origin.y.raw()}};
    if (auto* mb = std::get_if<MouseButton>(&ev)) {
        auto local = *mb;
        local.position = local.position - off;
        return local;
    }
    if (auto* mm = std::get_if<MouseMove>(&ev)) {
        auto local = *mm;
        local.position = local.position - off;
        return local;
    }
    if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        auto local = *ms;
        local.position = local.position - off;
        return local;
    }
    return ev;
}

} // namespace prism::input
