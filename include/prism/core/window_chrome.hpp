#pragma once

#include <prism/core/draw_list.hpp>

#include <string_view>

namespace prism {

struct WindowChrome {
    static constexpr float title_bar_h = 32.f;
    static constexpr float resize_edge = 6.f;
    static constexpr float button_w = 46.f;

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
        if (y < static_cast<int>(title_bar_h)) {
            float bx = static_cast<float>(w);
            // Buttons are right-aligned: Close | Maximize | Minimize
            if (x >= bx - button_w)              return HitZone::Close;
            if (x >= bx - 2.f * button_w)        return HitZone::Maximize;
            if (x >= bx - 3.f * button_w)        return HitZone::Minimize;
            return HitZone::TitleBar;
        }

        return HitZone::Client;
    }

    static void render(DrawList& dl, int w, std::string_view title) {
        auto fw = static_cast<float>(w);

        // Title bar background
        dl.filled_rect(
            Rect{Point{X{0}, Y{0}}, Size{Width{fw}, Height{title_bar_h}}},
            Color::rgba(45, 45, 48));

        // Bottom border
        dl.filled_rect(
            Rect{Point{X{0}, Y{title_bar_h - 1.f}}, Size{Width{fw}, Height{1}}},
            Color::rgba(60, 60, 65));

        // Title text (left-aligned with padding)
        if (!title.empty()) {
            dl.text(std::string(title), Point{X{12.f}, Y{7.f}}, 15.f,
                    Color::rgba(200, 200, 200));
        }

        // Buttons (right-aligned): Minimize | Maximize | Close
        float bx = fw - 3.f * button_w;
        auto button_bg = [&](float x, Color c) {
            dl.filled_rect(
                Rect{Point{X{x}, Y{0}}, Size{Width{button_w}, Height{title_bar_h - 1.f}}},
                c);
        };

        // Minimize: "—"
        button_bg(bx, Color::rgba(45, 45, 48, 0));
        dl.text("\xe2\x80\x94", Point{X{bx + 17.f}, Y{7.f}}, 13.f,
                Color::rgba(180, 180, 180));

        // Maximize: "□"
        bx += button_w;
        button_bg(bx, Color::rgba(45, 45, 48, 0));
        dl.rect_outline(
            Rect{Point{X{bx + 16.f}, Y{9.f}}, Size{Width{13.f}, Height{13.f}}},
            Color::rgba(180, 180, 180));

        // Close: "×"
        bx += button_w;
        button_bg(bx, Color::rgba(196, 43, 28));
        dl.text("\xc3\x97", Point{X{bx + 17.f}, Y{5.f}}, 17.f,
                Color::rgba(255, 255, 255));
    }
};

} // namespace prism
