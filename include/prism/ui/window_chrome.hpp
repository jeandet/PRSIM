#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/cursor.hpp>

#include <algorithm>
#include <string_view>

namespace prism::ui {
using namespace prism::render;


struct WindowChrome {
    static constexpr Height title_bar_h{32.f};
    static constexpr float resize_edge = 6.f; // hit_test() is a raw-int SDL boundary function
    static constexpr Width button_w{46.f};
    static constexpr int min_width = 200;
    static constexpr int min_height = 100;

    enum class HitZone {
        Client,
        TitleBar,
        Close, Minimize, Maximize,
        ResizeN, ResizeS, ResizeE, ResizeW,
        ResizeNE, ResizeNW, ResizeSE, ResizeSW
    };

    static HitZone hit_test(int x, int y, int w, int h) {
        auto e = static_cast<int>(resize_edge);

        // Corners first (overlap of two edges)
        if (x < e && y < e)         return HitZone::ResizeNW;
        if (x >= w - e && y < e)    return HitZone::ResizeNE;
        if (x < e && y >= h - e)    return HitZone::ResizeSW;
        if (x >= w - e && y >= h - e) return HitZone::ResizeSE;

        // Edges
        if (y < e)    return HitZone::ResizeN;
        if (y >= h - e) return HitZone::ResizeS;
        if (x < e)    return HitZone::ResizeW;
        if (x >= w - e) return HitZone::ResizeE;

        // Title bar region
        if (y < static_cast<int>(title_bar_h.raw())) {
            float bx = static_cast<float>(w);
            // Buttons are right-aligned: Close | Maximize | Minimize
            if (x >= bx - button_w.raw())              return HitZone::Close;
            if (x >= bx - 2.f * button_w.raw())        return HitZone::Maximize;
            if (x >= bx - 3.f * button_w.raw())        return HitZone::Minimize;
            return HitZone::TitleBar;
        }

        return HitZone::Client;
    }

    static CursorShape cursor_for(HitZone zone) {
        switch (zone) {
            case HitZone::ResizeN:
            case HitZone::ResizeS:  return CursorShape::ResizeNS;
            case HitZone::ResizeE:
            case HitZone::ResizeW:  return CursorShape::ResizeEW;
            case HitZone::ResizeNW:
            case HitZone::ResizeSE: return CursorShape::ResizeNWSE;
            case HitZone::ResizeNE:
            case HitZone::ResizeSW: return CursorShape::ResizeNESW;
            default: return CursorShape::Default;
        }
    }

    // Whether dragging this zone requires moving the window's on-screen position
    // to keep the opposite edge fixed (West/North and their diagonals). Wayland
    // forbids a client from repositioning its own toplevel window at all, so
    // these zones can't use PRISM's own manual begin/update/end_resize tracking
    // (SDL_SetWindowPosition is a hard no-op there) -- they must hand off to the
    // platform's native interactive resize instead (see sdl_hit_test_callback).
    // East/South/SE never need repositioning and stay on the manual path.
    static bool needs_native_resize(HitZone zone) {
        return zone == HitZone::ResizeW || zone == HitZone::ResizeN ||
               zone == HitZone::ResizeNW || zone == HitZone::ResizeNE || zone == HitZone::ResizeSW;
    }

    struct ResizeResult { int w, h, x, y; };

    // Computes the new window geometry for a resize drag. West/North-involving
    // zones must keep the OPPOSITE edge fixed on screen (like every real window
    // manager), so their new x/y is derived from that fixed edge and the
    // (already-clamped) new width/height, not from the raw mouse delta -- deriving
    // it from the raw delta would keep sliding the position even after the size
    // clamps at the minimum, visually detaching the window from the mouse.
    static ResizeResult resize_from_drag(HitZone zone, int start_w, int start_h,
                                          int start_x, int start_y, int dx, int dy) {
        int w = start_w, h = start_h;
        switch (zone) {
            case HitZone::ResizeE:  w += dx; break;
            case HitZone::ResizeS:  h += dy; break;
            case HitZone::ResizeSE: w += dx; h += dy; break;
            case HitZone::ResizeW:  w -= dx; break;
            case HitZone::ResizeN:  h -= dy; break;
            case HitZone::ResizeNW: w -= dx; h -= dy; break;
            case HitZone::ResizeNE: w += dx; h -= dy; break;
            case HitZone::ResizeSW: w -= dx; h += dy; break;
            default: break;
        }
        w = std::max(w, min_width);
        h = std::max(h, min_height);

        int x = start_x, y = start_y;
        bool anchor_right = zone == HitZone::ResizeW || zone == HitZone::ResizeNW || zone == HitZone::ResizeSW;
        bool anchor_bottom = zone == HitZone::ResizeN || zone == HitZone::ResizeNW || zone == HitZone::ResizeNE;
        if (anchor_right) x = (start_x + start_w) - w;
        if (anchor_bottom) y = (start_y + start_h) - h;

        return {w, h, x, y};
    }

    static void render(DrawList& dl, int w, std::string_view title, const Theme& t) {
        Width fw{static_cast<float>(w)};

        // Title bar background
        dl.filled_rect(
            Rect{Point{X{0}, Y{0}}, Size{fw, title_bar_h}},
            t.chrome_bg);

        // Bottom border
        dl.filled_rect(
            Rect{Point{X{0}, Y{title_bar_h.raw() - 1.f}}, Size{fw, Height{1}}},
            t.chrome_border);

        // Title text (left-aligned with padding)
        if (!title.empty()) {
            dl.text(std::string(title), Point{X{12.f}, Y{7.f}}, 15.f,
                    t.chrome_text);
        }

        // Buttons (right-aligned): Minimize | Maximize | Close
        X bx{fw.raw() - 3.f * button_w.raw()};
        auto button_bg = [&](X x, Color c) {
            dl.filled_rect(
                Rect{Point{x, Y{0}}, Size{button_w, Height{title_bar_h.raw() - 1.f}}},
                c);
        };

        // Minimize: "—"
        button_bg(bx, Color::rgba(t.chrome_bg.r, t.chrome_bg.g, t.chrome_bg.b, 0));
        dl.text("\xe2\x80\x94", Point{bx + DX{17.f}, Y{7.f}}, 13.f,
                t.chrome_icon);

        // Maximize: "□"
        bx += DX{button_w.raw()};
        button_bg(bx, Color::rgba(t.chrome_bg.r, t.chrome_bg.g, t.chrome_bg.b, 0));
        dl.rect_outline(
            Rect{Point{bx + DX{16.f}, Y{9.f}}, Size{Width{13.f}, Height{13.f}}},
            t.chrome_icon);

        // Close: "×"
        bx += DX{button_w.raw()};
        button_bg(bx, t.chrome_close);
        dl.text("\xc3\x97", Point{bx + DX{17.f}, Y{5.f}}, 17.f,
                t.text_on_primary);
    }
};

} // namespace prism::ui
