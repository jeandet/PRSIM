#pragma once
#include <prism/core/draw_list.hpp>
#include <prism/core/context.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace prism::plot {

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

inline void draw_grid_lines(DrawList& dl, const PlotMapping& map, const Theme& t)
{
    auto x_ticks = nice_ticks(map.x_range.min, map.x_range.max, 6);
    auto y_ticks = nice_ticks(map.y_range.min, map.y_range.max, 5);

    float left = map.plot_area.origin.x.raw();
    float right = left + map.plot_area.extent.w.raw();
    float top = map.plot_area.origin.y.raw();
    float bottom = top + map.plot_area.extent.h.raw();

    for (double tx : x_ticks) {
        auto px = map.to_pixel(tx, 0.0);
        float x = px.x.raw();
        if (x < left || x > right) continue;
        dl.line(Point{X{x}, Y{top}}, Point{X{x}, Y{bottom}}, t.track, 1.f);
    }

    for (double ty : y_ticks) {
        auto px = map.to_pixel(0.0, ty);
        float y = px.y.raw();
        if (y < top || y > bottom) continue;
        dl.line(Point{X{left}, Y{y}}, Point{X{right}, Y{y}}, t.track, 1.f);
    }

    dl.line(Point{X{left}, Y{top}}, Point{X{left}, Y{bottom}}, t.border, 1.f);
    dl.line(Point{X{left}, Y{bottom}}, Point{X{right}, Y{bottom}}, t.border, 1.f);
}

inline void draw_tick_labels(DrawList& dl, const PlotMapping& map, const Theme& t)
{
    auto x_ticks = nice_ticks(map.x_range.min, map.x_range.max, 6);
    auto y_ticks = nice_ticks(map.y_range.min, map.y_range.max, 5);

    float left = map.plot_area.origin.x.raw();
    float right = left + map.plot_area.extent.w.raw();
    float top = map.plot_area.origin.y.raw();
    float bottom = top + map.plot_area.extent.h.raw();

    for (double tx : x_ticks) {
        auto px = map.to_pixel(tx, 0.0);
        float x = px.x.raw();
        if (x < left || x > right) continue;
        dl.text(fmt::format("{:.6g}", tx), Point{X{x - 15.f}, Y{bottom + 4.f}}, 11.f, t.text_muted);
    }

    for (double ty : y_ticks) {
        auto px = map.to_pixel(0.0, ty);
        float y = px.y.raw();
        if (y < top || y > bottom) continue;
        dl.text(fmt::format("{:.6g}", ty), Point{X{left - 55.f}, Y{y - 6.f}}, 11.f, t.text_muted);
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
    float left = map.plot_area.origin.x.raw();
    float right = left + map.plot_area.extent.w.raw();
    float top = map.plot_area.origin.y.raw();
    float bottom = top + map.plot_area.extent.h.raw();

    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);

    dl.line(Point{X{px.x.raw()}, Y{top}}, Point{X{px.x.raw()}, Y{bottom}}, crosshair_color, 1.f);
    dl.line(Point{X{left}, Y{px.y.raw()}}, Point{X{right}, Y{px.y.raw()}}, crosshair_color, 1.f);

    auto label = fmt::format("({:.4g}, {:.4g})", cursor.data_x, cursor.data_y);
    float tx = px.x.raw() + 10.f;
    float ty = px.y.raw() - 20.f;
    if (tx + 120.f > right) tx = px.x.raw() - 130.f;
    if (ty < top) ty = px.y.raw() + 10.f;

    dl.filled_rect(Rect{Point{X{tx - 2.f}, Y{ty - 2.f}}, Size{Width{120.f}, Height{18.f}}}, t.surface);
    dl.text(std::move(label), Point{X{tx}, Y{ty}}, 12.f, t.text);
}

inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t)
{
    float bottom = map.plot_area.origin.y.raw() + map.plot_area.extent.h.raw();
    float cx = map.plot_area.origin.x.raw() + map.plot_area.extent.w.raw() / 2.f;

    if (!x_label.empty())
        dl.text(x_label, Point{X{cx - 30.f}, Y{bottom + 18.f}}, 12.f, t.text);

    if (!y_label.empty()) {
        float lx = map.plot_area.origin.x.raw() - margin_left + 2.f;
        float ly = map.plot_area.origin.y.raw() + map.plot_area.extent.h.raw() / 2.f;
        dl.text(y_label, Point{X{lx}, Y{ly}}, 12.f, t.text);
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
