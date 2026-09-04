#pragma once

#include <prism/core/types.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace prism::render {
using namespace prism::core;


struct FilledRect {
    Rect rect;
    Color color;
};

struct RectOutline {
    Rect rect;
    Color color;
    float thickness;
};

enum class TextAnchor { TopLeft, Center };

struct TextCmd {
    std::string text;
    Point origin;
    float size;
    Color color;
    float angle = 0.f;       // degrees, counter-clockwise
    TextAnchor anchor = TextAnchor::TopLeft;
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

struct FilledPolygon {
    // Vertices in triangle-strip order: triangle i is (i, i+1, i+2) for every
    // i in [0, points.size()-3] -- the same generic primitive as a GPU triangle strip.
    std::vector<Point> points;
    Color color;
};

using DrawCmd = std::variant<FilledRect, RectOutline, TextCmd, ClipPush, ClipPop,
                             RoundedRect, Line, Polyline, Circle, FilledPolygon>;

struct DrawList {
    std::vector<DrawCmd> commands;

    void filled_rect(Rect r, Color c)
    {
        commands.emplace_back(FilledRect{translated(r), c});
    }

    void rect_outline(Rect r, Color c, float thickness = 1.0f)
    {
        commands.emplace_back(RectOutline{translated(r), c, thickness});
    }

    void circle(Point center, float radius, Color c, float thickness = 0.f)
    {
        commands.emplace_back(Circle{translated(center), radius, c, thickness});
    }

    void polyline(std::vector<Point> pts, Color c, float thickness = 1.f)
    {
        for (auto& p : pts)
            p = translated(p);
        commands.emplace_back(Polyline{std::move(pts), c, thickness});
    }

    void filled_polygon(std::vector<Point> pts, Color c)
    {
        for (auto& p : pts)
            p = translated(p);
        commands.emplace_back(FilledPolygon{std::move(pts), c});
    }

    void line(Point from, Point to, Color c, float thickness = 1.f)
    {
        commands.emplace_back(Line{translated(from), translated(to), c, thickness});
    }

    void rounded_rect(Rect r, Color c, float radius, float thickness = 0.f)
    {
        commands.emplace_back(RoundedRect{translated(r), c, radius, thickness});
    }

    void text(std::string s, Point origin, float size, Color c,
              float angle = 0.f, TextAnchor anchor = TextAnchor::TopLeft)
    {
        commands.emplace_back(
            TextCmd{std::move(s), translated(origin), size, c, angle, anchor});
    }

    void clip_push(Point origin, Size extent)
    {
        Point abs = translated(origin);
        origin_stack_.push_back(Offset{DX{abs.x.raw()}, DY{abs.y.raw()}});
        commands.emplace_back(ClipPush{{abs, extent}});
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

    // Rough heap footprint: command storage plus the string/point payloads
    // commands own. Deliberately approximate (uses capacity, not size, and
    // ignores variant/allocator overhead) -- good enough to compare snapshots
    // relatively, not a precise allocator accounting.
    [[nodiscard]] std::size_t approx_bytes() const {
        std::size_t total = commands.capacity() * sizeof(DrawCmd);
        for (const auto& cmd : commands) {
            std::visit([&](const auto& c) {
                if constexpr (requires { c.text; }) total += c.text.capacity();
                if constexpr (requires { c.points; })
                    total += c.points.capacity() * sizeof(Point);
            }, cmd);
        }
        return total;
    }

  private:
    std::vector<Offset> origin_stack_;

    [[nodiscard]] Offset current_offset() const
    {
        return origin_stack_.empty() ? Offset{DX{0.f}, DY{0.f}} : origin_stack_.back();
    }

    [[nodiscard]] Point translated(Point p) const
    {
        auto o = current_offset();
        return Point{p.x + o.dx, p.y + o.dy};
    }

    [[nodiscard]] Rect translated(Rect r) const
    {
        return Rect{translated(r.origin), r.extent};
    }
};

} // namespace prism::render
