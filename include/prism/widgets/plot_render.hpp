#pragma once
#include <prism/render/draw_list.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/ui/context.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace prism::plot {
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;

struct AxisRange {
    double min = 0.0;
    double max = 1.0;
    bool auto_fit = true;
    bool operator==(const AxisRange&) const = default;
};

struct PlotMapping {
    AxisRange x_range;
    AxisRange y_range;
    Rect plot_area;

    Point to_pixel(double data_x, double data_y) const
    {
        float px = plot_area.origin.x.raw()
                   + static_cast<float>((data_x - x_range.min) / (x_range.max - x_range.min))
                     * plot_area.extent.w.raw();
        float py = plot_area.origin.y.raw()
                   + static_cast<float>(1.0 - (data_y - y_range.min) / (y_range.max - y_range.min))
                     * plot_area.extent.h.raw();
        return Point{X{px}, Y{py}};
    }

    std::pair<double, double> to_data(Point pixel) const
    {
        double dx = x_range.min
                    + (pixel.x.raw() - plot_area.origin.x.raw())
                      / plot_area.extent.w.raw()
                      * (x_range.max - x_range.min);
        double dy = y_range.min
                    + (1.0 - (pixel.y.raw() - plot_area.origin.y.raw())
                             / plot_area.extent.h.raw())
                      * (y_range.max - y_range.min);
        return {dx, dy};
    }

    float left() const { return plot_area.origin.x.raw(); }
    float right() const { return left() + plot_area.extent.w.raw(); }
    float top() const { return plot_area.origin.y.raw(); }
    float bottom() const { return top() + plot_area.extent.h.raw(); }

    static AxisRange apply_view(AxisRange base, double offset, double scale)
    {
        double center = (base.min + base.max) / 2.0 + offset;
        double half_range = (base.max - base.min) / (2.0 * scale);
        return {center - half_range, center + half_range, false};
    }
};

inline std::vector<double> nice_ticks(double min, double max, int target_count)
{
    if (target_count < 1) target_count = 1;

    double range = max - min;
    if (range <= 0.0) return {min};

    double rough_step = range / target_count;
    double magnitude = std::pow(10.0, std::floor(std::log10(rough_step)));
    double residual = rough_step / magnitude;

    double nice_step;
    if (residual <= 1.5)
        nice_step = 1.0 * magnitude;
    else if (residual <= 3.5)
        nice_step = 2.0 * magnitude;
    else if (residual <= 7.5)
        nice_step = 5.0 * magnitude;
    else
        nice_step = 10.0 * magnitude;

    double tick_min = std::ceil(min / nice_step) * nice_step;
    std::vector<double> ticks;
    for (double v = tick_min; v <= max + nice_step * 1e-9; v += nice_step)
        ticks.push_back(v);

    return ticks;
}

enum class Axis { X, Y };

template <typename Range>
AxisRange auto_fit_range(const Range& series, Axis axis)
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    bool any = false;

    for (auto& s : series) {
        for (size_t i = 0; i < s.size(); ++i) {
            double v = (axis == Axis::X) ? s.x(i) : s.y(i);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            any = true;
        }
    }

    if (!any) return {0.0, 1.0, true};
    if (lo == hi) { lo -= 0.5; hi += 0.5; }
    double pad = (hi - lo) * 0.05;
    return {lo - pad, hi + pad, true};
}

constexpr float margin_left = 60.f;
constexpr float margin_bottom = 45.f;
constexpr float margin_top = 10.f;
constexpr float margin_right = 10.f;
constexpr float tick_len = 5.f;
constexpr float tick_font_size = 11.f;
constexpr float label_font_size = 12.f;

struct CursorState {
    double data_x = 0.0;
    double data_y = 0.0;
    bool visible = false;
    bool operator==(const CursorState&) const = default;
};

inline void draw_background(DrawList& dl, Rect plot_area, const Theme& t)
{
    dl.filled_rect(plot_area, t.canvas_bg);
    dl.rect_outline(plot_area, t.border);
}

struct TickArrays {
    std::vector<double> x;
    std::vector<double> y;
};

inline TickArrays compute_ticks(const PlotMapping& map)
{
    return {nice_ticks(map.x_range.min, map.x_range.max, 6),
            nice_ticks(map.y_range.min, map.y_range.max, 5)};
}

inline void draw_grid_lines(DrawList& dl, const PlotMapping& map,
                            const TickArrays& ticks, const Theme& t)
{
    for (double tx : ticks.x) {
        float x = map.to_pixel(tx, 0.0).x.raw();
        if (x < map.left() || x > map.right()) continue;
        dl.line(Point{X{x}, Y{map.top()}}, Point{X{x}, Y{map.bottom()}}, t.track, 1.f);
    }

    for (double ty : ticks.y) {
        float y = map.to_pixel(0.0, ty).y.raw();
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{X{map.left()}, Y{y}}, Point{X{map.right()}, Y{y}}, t.track, 1.f);
    }

    dl.line(Point{X{map.left()}, Y{map.top()}}, Point{X{map.left()}, Y{map.bottom()}}, t.border, 1.f);
    dl.line(Point{X{map.left()}, Y{map.bottom()}}, Point{X{map.right()}, Y{map.bottom()}}, t.border, 1.f);
}

inline void draw_tick_labels(DrawList& dl, const PlotMapping& map,
                             const TickArrays& ticks, const Theme& t)
{
    float cw = char_width(tick_font_size);

    for (double tx : ticks.x) {
        float x = map.to_pixel(tx, 0.0).x.raw();
        if (x < map.left() || x > map.right()) continue;
        dl.line(Point{X{x}, Y{map.bottom()}},
                Point{X{x}, Y{map.bottom() + tick_len}}, t.border, 1.f);
        dl.text(fmt::format("{:.6g}", tx),
                Point{X{x - 15.f}, Y{map.bottom() + tick_len + 2.f}},
                tick_font_size, t.text_muted);
    }

    for (double ty : ticks.y) {
        float y = map.to_pixel(0.0, ty).y.raw();
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{X{map.left() - tick_len}, Y{y}},
                Point{X{map.left()}, Y{y}}, t.border, 1.f);
        auto label = fmt::format("{:.6g}", ty);
        float label_w = static_cast<float>(label.size()) * cw;
        dl.text(std::move(label),
                Point{X{map.left() - tick_len - 2.f - label_w}, Y{y - 6.f}},
                tick_font_size, t.text_muted);
    }
}

template <typename SeriesRange>
void draw_series(DrawList& dl, const PlotMapping& map, const SeriesRange& series)
{
    for (auto& s : series) {
        if (s.size() < 2) continue;
        std::vector<Point> pts;
        pts.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
            pts.push_back(map.to_pixel(s.x(i), s.y(i)));
        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
}

inline void draw_cursor(DrawList& dl, const PlotMapping& map,
                        const CursorState& cursor, const Theme& t)
{
    if (!cursor.visible) return;

    auto px = map.to_pixel(cursor.data_x, cursor.data_y);
    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);

    dl.line(Point{X{px.x.raw()}, Y{map.top()}},
            Point{X{px.x.raw()}, Y{map.bottom()}}, crosshair_color, 1.f);
    dl.line(Point{X{map.left()}, Y{px.y.raw()}},
            Point{X{map.right()}, Y{px.y.raw()}}, crosshair_color, 1.f);

    auto label = fmt::format("({:.4g}, {:.4g})", cursor.data_x, cursor.data_y);
    float tx = px.x.raw() + 10.f;
    float ty = px.y.raw() - 20.f;
    if (tx + 120.f > map.right()) tx = px.x.raw() - 130.f;
    if (ty < map.top()) ty = px.y.raw() + 10.f;

    dl.filled_rect(Rect{Point{X{tx - 2.f}, Y{ty - 2.f}}, Size{Width{120.f}, Height{18.f}}}, t.surface);
    dl.text(std::move(label), Point{X{tx}, Y{ty}}, label_font_size, t.text);
}

inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t)
{
    float cx = (map.left() + map.right()) / 2.f;

    if (!x_label.empty())
        dl.text(x_label, Point{X{cx - 30.f}, Y{map.bottom() + 18.f}}, label_font_size, t.text);

    if (!y_label.empty()) {
        float lx = map.left() - margin_left + 10.f;
        float cy = (map.top() + map.bottom()) / 2.f;
        dl.text(y_label, Point{X{lx}, Y{cy}}, label_font_size, t.text, 90.f, TextAnchor::Center);
    }
}

inline std::array<Color, 8> default_series_colors(const Theme& t)
{
    return {{
        t.accent,                              // cyan-ish
        t.primary,                             // blue
        Color::rgba(220, 80, 60),              // red
        Color::rgba(80, 180, 80),              // green
        Color::rgba(200, 160, 40),             // yellow
        Color::rgba(160, 80, 200),             // purple
        Color::rgba(240, 130, 40),             // orange
        Color::rgba(100, 200, 200),            // teal
    }};
}

} // namespace prism::plot
