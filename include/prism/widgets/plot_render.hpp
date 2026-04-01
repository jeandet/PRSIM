#pragma once
#include <prism/core/draw_list.hpp>
#include <prism/core/context.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr float margin_bottom = 30.f;
constexpr float margin_top = 10.f;
constexpr float margin_right = 10.f;

} // namespace prism::plot
