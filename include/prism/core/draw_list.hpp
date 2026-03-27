#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace prism {

struct Color {
    uint8_t r, g, b, a;

    static constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return {r, g, b, a};
    }
};

struct Point {
    float x, y;
};

struct Rect {
    float x, y, w, h;

    [[nodiscard]] Point center() const { return {x + w / 2, y + h / 2}; }

    [[nodiscard]] bool contains(Point p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

struct FilledRect {
    Rect rect;
    Color color;
};

struct RectOutline {
    Rect rect;
    Color color;
    float thickness;
};

struct TextCmd {
    std::string text;
    Point origin;
    float size;
    Color color;
};

struct ClipPush {
    Rect rect;
};

struct ClipPop {};

using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop>;

struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c) { commands.emplace_back(FilledRect{r, c}); }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        commands.emplace_back(RectOutline{r, c, thickness});
    }

    void text(std::string s, Point origin, float size, Color c)
    {
        commands.emplace_back(TextCmd{std::move(s), origin, size, c});
    }

    void clip_push(Rect r) { commands.emplace_back(ClipPush{r}); }
    void clip_pop() { commands.emplace_back(ClipPop{}); }

    void clear() { commands.clear(); }
    [[nodiscard]] bool empty() const { return commands.empty(); }
    [[nodiscard]] std::size_t size() const { return commands.size(); }

    [[nodiscard]] Rect bounding_box() const {
        if (commands.empty()) return {0, 0, 0, 0};
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        auto expand = [&](Rect r) {
            min_x = std::min(min_x, r.x);
            min_y = std::min(min_y, r.y);
            max_x = std::max(max_x, r.x + r.w);
            max_y = std::max(max_y, r.y + r.h);
        };
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.rect; })
                    expand(c.rect);
                else if constexpr (requires { c.origin; })
                    expand({c.origin.x, c.origin.y, 0, c.size});
            }, cmd);
        }
        return {min_x, min_y, max_x - min_x, max_y - min_y};
    }
};

} // namespace prism
