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

struct Theme {
    // Surface backgrounds (widget fill)
    Color surface         = Color::rgba(45, 45, 55);
    Color surface_hover   = Color::rgba(55, 55, 68);
    Color surface_active  = Color::rgba(65, 65, 78);

    // Primary accent (buttons)
    Color primary         = Color::rgba(40, 105, 180);
    Color primary_hover   = Color::rgba(50, 120, 200);
    Color primary_active  = Color::rgba(30, 90, 160);
    Color primary_outline = Color::rgba(60, 140, 220);

    // Accent (slider thumbs, checked checkboxes — purer blue)
    Color accent          = Color::rgba(0, 140, 200);
    Color accent_hover    = Color::rgba(0, 160, 220);
    Color accent_active   = Color::rgba(0, 120, 180);

    // Text
    Color text            = Color::rgba(220, 220, 220);
    Color text_muted      = Color::rgba(180, 180, 190);
    Color text_placeholder= Color::rgba(120, 120, 130);
    Color text_on_primary = Color::rgba(240, 240, 240);

    // Borders & outlines
    Color border          = Color::rgba(90, 90, 105);
    Color border_hover    = Color::rgba(120, 120, 135);
    Color focus_ring      = Color::rgba(80, 160, 240);

    // Track (slider, scrollbar track backgrounds)
    Color track           = Color::rgba(60, 60, 70);
    Color track_hover     = Color::rgba(70, 70, 82);

    // Scrollbar thumb
    Color scrollbar_thumb = Color::rgba(120, 120, 130, 160);

    // Text cursor
    Color cursor          = Color::rgba(220, 220, 240);

    // Dropdown popup
    Color popup_bg        = Color::rgba(50, 50, 62);
    Color popup_border    = Color::rgba(80, 80, 95);
    Color popup_highlight = Color::rgba(60, 100, 180);

    // Table
    Color table_row_even  = Color::rgba(30, 30, 50);
    Color table_row_odd   = Color::rgba(26, 26, 46);
    Color table_selected  = Color::rgba(50, 50, 120);
    Color table_header    = Color::rgba(42, 42, 74);
    Color table_divider   = Color::rgba(50, 50, 70);
    Color table_header_divider = Color::rgba(74, 74, 106);
    Color table_header_text    = Color::rgba(136, 136, 204);

    // Tabs
    Color tab_bar_bg      = Color::rgba(42, 42, 58);
    Color tab_active_bg   = Color::rgba(30, 30, 46);
    Color tab_text        = Color::rgba(140, 140, 160);
    Color tab_text_active = Color::rgba(220, 220, 240);
    Color tab_accent      = Color::rgba(124, 111, 255);

    // Window chrome
    Color chrome_bg       = Color::rgba(45, 45, 48);
    Color chrome_border   = Color::rgba(60, 60, 65);
    Color chrome_text     = Color::rgba(200, 200, 200);
    Color chrome_icon     = Color::rgba(180, 180, 180);
    Color chrome_close    = Color::rgba(196, 43, 28);

    // Canvas clear color
    Color canvas_bg       = Color::rgba(30, 30, 30);
};

inline Theme default_theme() { return {}; }

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
