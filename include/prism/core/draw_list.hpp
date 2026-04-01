#pragma once

#include <prism/core/types.hpp>

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

inline Color lerp(Color a, Color b, EasedProgress t) {
    auto mix = [&](uint8_t x, uint8_t y) -> uint8_t {
        return static_cast<uint8_t>(x + (y - x) * t.raw() + 0.5f);
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

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

struct RoundedRect {
    Rect rect;
    Color color;
    float radius;
    float thickness;  // 0 = filled, >0 = stroke only
};

struct Line {
    Point from;
    Point to;
    Color color;
    float thickness;
};

struct Polyline {
    std::vector<Point> points;
    Color color;
    float thickness;
};

struct Circle {
    Point center;
    float radius;
    Color color;
    float thickness;  // 0 = filled, >0 = stroke only
};

using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle>;

struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(FilledRect{
            {Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c});
    }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        auto o = current_offset();
        commands.emplace_back(RectOutline{
            {Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c, thickness});
    }

    void circle(Point center, float radius, Color c, float thickness = 0.f)
    {
        auto o = current_offset();
        commands.emplace_back(Circle{
            Point{center.x + o.dx, center.y + o.dy}, radius, c, thickness});
    }

    void polyline(std::vector<Point> pts, Color c, float thickness = 1.f)
    {
        auto o = current_offset();
        for (auto& p : pts)
            p = Point{p.x + o.dx, p.y + o.dy};
        commands.emplace_back(Polyline{std::move(pts), c, thickness});
    }

    void line(Point from, Point to, Color c, float thickness = 1.f)
    {
        auto o = current_offset();
        commands.emplace_back(Line{
            Point{from.x + o.dx, from.y + o.dy},
            Point{to.x + o.dx, to.y + o.dy},
            c, thickness});
    }

    void rounded_rect(Rect r, Color c, float radius, float thickness = 0.f)
    {
        auto o = current_offset();
        commands.emplace_back(RoundedRect{
            {Point{r.origin.x + o.dx, r.origin.y + o.dy}, r.extent}, c, radius, thickness});
    }

    void text(std::string s, Point origin, float size, Color c)
    {
        auto o = current_offset();
        commands.emplace_back(
            TextCmd{std::move(s), Point{origin.x + o.dx, origin.y + o.dy}, size, c});
    }

    void clip_push(Point origin, Size extent)
    {
        auto o = current_offset();
        X abs_x = origin.x + o.dx;
        Y abs_y = origin.y + o.dy;
        origin_stack_.push_back(Offset{DX{abs_x.raw()}, DY{abs_y.raw()}});
        commands.emplace_back(ClipPush{{Point{abs_x, abs_y}, extent}});
    }


    void clip_pop()
    {
        if (!origin_stack_.empty()) origin_stack_.pop_back();
        commands.emplace_back(ClipPop{});
    }

    void clear()
    {
        commands.clear();
        origin_stack_.clear();
    }

    [[nodiscard]] bool empty() const { return commands.empty(); }
    [[nodiscard]] std::size_t size() const { return commands.size(); }

    [[nodiscard]] Rect bounding_box() const {
        if (commands.empty()) return {Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        auto expand = [&](Rect r) {
            min_x = std::min(min_x, r.origin.x.raw());
            min_y = std::min(min_y, r.origin.y.raw());
            max_x = std::max(max_x, r.origin.x.raw() + r.extent.w.raw());
            max_y = std::max(max_y, r.origin.y.raw() + r.extent.h.raw());
        };
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.rect; })
                    expand(c.rect);
                else if constexpr (requires { c.center; c.radius; }) {
                    expand({Point{X{c.center.x.raw() - c.radius}, Y{c.center.y.raw() - c.radius}},
                            Size{Width{2 * c.radius}, Height{2 * c.radius}}});
                }
                else if constexpr (requires { c.points; }) {
                    for (const auto& p : c.points)
                        expand({p, Size{Width{0}, Height{0}}});
                }
                else if constexpr (requires { c.from; c.to; }) {
                    expand({c.from, Size{Width{0}, Height{0}}});
                    expand({c.to, Size{Width{0}, Height{0}}});
                }
                else if constexpr (requires { c.origin; })
                    expand({c.origin, Size{Width{0}, Height{c.size}}});
            }, cmd);
        }
        return {Point{X{min_x}, Y{min_y}},
                Size{Width{max_x - min_x}, Height{max_y - min_y}}};
    }

  private:
    std::vector<Offset> origin_stack_;

    [[nodiscard]] Offset current_offset() const
    {
        return origin_stack_.empty() ? Offset{DX{0.f}, DY{0.f}} : origin_stack_.back();
    }
};

} // namespace prism
