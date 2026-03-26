#pragma once

#include <cstdint>
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
};

} // namespace prism
