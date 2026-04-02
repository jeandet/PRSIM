#pragma once
#include <prism/core/field.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/widgets/plot_render.hpp>
#include <functional>
#include <vector>
#include <span>
#include <memory>
#include <cmath>

namespace prism::plot {
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;

template <typename T>
concept PlotSource = requires(const T& src, size_t i) {
    { src.size() } -> std::convertible_to<size_t>;
    { src.x(i) } -> std::convertible_to<double>;
    { src.y(i) } -> std::convertible_to<double>;
};

struct XYData {
    std::vector<double> xs;
    std::vector<double> ys;
    size_t size() const { return xs.size(); }
    double x(size_t i) const { return xs[i]; }
    double y(size_t i) const { return ys[i]; }
};

struct SeriesStyle {
    Color color = Color::rgba(0, 140, 200);
    float thickness = 2.f;
};

class Series {
  public:
    Series() = default;

    template <PlotSource S>
    Series(S source, SeriesStyle s)
        : style_(s)
    {
        auto shared = std::make_shared<S>(std::move(source));
        size_fn_ = [shared]() { return shared->size(); };
        x_fn_ = [shared](size_t i) { return shared->x(i); };
        y_fn_ = [shared](size_t i) { return shared->y(i); };
    }

    size_t size() const { return size_fn_ ? size_fn_() : 0; }
    double x(size_t i) const { return x_fn_(i); }
    double y(size_t i) const { return y_fn_(i); }
    const SeriesStyle& style() const { return style_; }

  private:
    std::function<size_t()> size_fn_;
    std::function<double(size_t)> x_fn_;
    std::function<double(size_t)> y_fn_;
    SeriesStyle style_;
};

struct ViewTransform {
    double offset_x = 0.0;
    double offset_y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    bool operator==(const ViewTransform&) const = default;
};

enum class DragMode { None, Pan };

constexpr double zoom_base = 1.1;

inline PlotMapping compute_mapping(Rect bounds,
                                   const Field<AxisRange>& xr,
                                   const Field<AxisRange>& yr,
                                   const Field<ViewTransform>& vt,
                                   std::span<const Series> series)
{
    AxisRange eff_x = xr.get();
    AxisRange eff_y = yr.get();

    if (eff_x.auto_fit) eff_x = auto_fit_range(series, Axis::X);
    if (eff_y.auto_fit) eff_y = auto_fit_range(series, Axis::Y);

    auto& v = vt.get();
    eff_x = PlotMapping::apply_view(eff_x, v.offset_x, v.scale_x);
    eff_y = PlotMapping::apply_view(eff_y, v.offset_y, v.scale_y);

    Rect plot_area{
        Point{X{bounds.origin.x.raw() + margin_left},
              Y{bounds.origin.y.raw() + margin_top}},
        Size{Width{bounds.extent.w.raw() - margin_left - margin_right},
             Height{bounds.extent.h.raw() - margin_top - margin_bottom}},
    };

    return PlotMapping{eff_x, eff_y, plot_area};
}

struct PlotModel {
    // Reactive state
    Field<AxisRange> x_range{};
    Field<AxisRange> y_range{};
    Field<std::string> x_label{""};
    Field<std::string> y_label{""};
    Field<ViewTransform> view{};
    Field<CursorState> cursor{};
    Field<uint32_t> revision{0};

    // Transient interaction state
    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    // Series management
    template <PlotSource S>
    void add_series(S source, SeriesStyle style)
    {
        series_.emplace_back(std::move(source), style);
    }

    void add_series(std::vector<double> xs, std::vector<double> ys, SeriesStyle style)
    {
        add_series(XYData{std::move(xs), std::move(ys)}, style);
    }

    void remove_series(size_t index)
    {
        if (index < series_.size())
            series_.erase(series_.begin() + static_cast<ptrdiff_t>(index));
    }

    void clear_series() { series_.clear(); }
    size_t series_count() const { return series_.size(); }

    void reset_view()
    {
        view.set(ViewTransform{});
        x_range.set(AxisRange{});
        y_range.set(AxisRange{});
    }

    void notify()
    {
        revision.set(revision.get() + 1);
    }

    // Canvas interface
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
    {
        auto& t = *node.theme;
        auto map = compute_mapping(bounds, x_range, y_range, view,
                                   std::span<const Series>(series_));

        draw_background(dl, map.plot_area, t);
        auto ticks = compute_ticks(map);

        // Inside clip_push, coordinates are local (origin = {0,0})
        PlotMapping local_map = map;
        local_map.plot_area.origin = Point{X{0}, Y{0}};

        dl.clip_push(map.plot_area.origin, map.plot_area.extent);
        draw_grid_lines(dl, local_map, ticks, t);
        draw_series(dl, local_map, std::span<const Series>(series_));
        draw_cursor(dl, local_map, cursor.get(), t);
        dl.clip_pop();
        draw_tick_labels(dl, map, ticks, t);
        draw_axes_labels(dl, map, x_label.get(), y_label.get(), t);
    }

    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    std::vector<Series> series_;
};

inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& /*nd*/, Rect bounds)
{
    auto series_span = std::span<const Series>(series_);
    auto map = compute_mapping(bounds, x_range, y_range, view, series_span);

    auto freeze_auto_fit = [&] {
        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) {
            xr = auto_fit_range(series_span, Axis::X);
            xr.auto_fit = false;
            x_range.set(xr);
        }
        if (yr.auto_fit) {
            yr = auto_fit_range(series_span, Axis::Y);
            yr.auto_fit = false;
            y_range.set(yr);
        }
    };

    if (auto* mm = std::get_if<MouseMove>(&ev)) {
        if (drag_mode == DragMode::Pan) {
            float dx_px = mm->position.x.raw() - drag_start_pixel.x.raw();
            float dy_px = mm->position.y.raw() - drag_start_pixel.y.raw();

            double data_dx = dx_px / map.plot_area.extent.w.raw()
                             * (map.x_range.max - map.x_range.min);
            double data_dy = -(dy_px / map.plot_area.extent.h.raw()
                               * (map.y_range.max - map.y_range.min));

            auto v = drag_start_view;
            v.offset_x -= data_dx;
            v.offset_y -= data_dy;
            view.set(v);
        }

        if (map.plot_area.contains(mm->position)) {
            auto [dx, dy] = map.to_data(mm->position);
            cursor.set(CursorState{dx, dy, true});
        } else {
            auto c = cursor.get();
            if (c.visible) cursor.set(CursorState{c.data_x, c.data_y, false});
        }

    } else if (auto* mb = std::get_if<MouseButton>(&ev)) {
        if (mb->button == buttons::left) {
            if (mb->pressed) {
                drag_mode = DragMode::Pan;
                drag_start_pixel = mb->position;
                drag_start_view = view.get();

                freeze_auto_fit();
            } else {
                drag_mode = DragMode::None;
            }
        } else if (mb->button == buttons::right && mb->pressed) {
            if (map.plot_area.contains(mb->position))
                reset_view();
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        float px = ms->position.x.raw();
        float py = ms->position.y.raw();

        bool in_plot = map.plot_area.contains(ms->position);
        bool in_x_axis = (px >= map.left() && px <= map.right()
                          && py > map.bottom() && py <= map.bottom() + margin_bottom);
        bool in_y_axis = (py >= map.top() && py <= map.bottom()
                          && px >= map.left() - margin_left && px < map.left());

        if (!in_plot && !in_x_axis && !in_y_axis) return;

        freeze_auto_fit();

        double factor = std::pow(zoom_base, ms->dy.raw());
        Point clamp_pt{X{std::clamp(px, map.left(), map.right())},
                       Y{std::clamp(py, map.top(), map.bottom())}};
        auto [data_x, data_y] = map.to_data(clamp_pt);

        auto zoom_axis = [factor](double& scale, double& offset,
                                  double data_anchor, AxisRange base) {
            double old = scale;
            scale *= factor;
            double center = (base.min + base.max) / 2.0;
            offset = data_anchor - center
                     + (center + offset - data_anchor) * old / scale;
        };

        auto v = view.get();
        if (in_plot || in_x_axis)
            zoom_axis(v.scale_x, v.offset_x, data_x, x_range.get());
        if (in_plot || in_y_axis)
            zoom_axis(v.scale_y, v.offset_y, data_y, y_range.get());

        view.set(v);
    }
}

} // namespace prism::plot
