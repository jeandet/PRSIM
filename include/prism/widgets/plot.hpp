#pragma once
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/widget_node.hpp>
#include <prism/widgets/plot_render.hpp>
#include <functional>
#include <vector>
#include <span>
#include <memory>
#include <cmath>

namespace prism::plot {

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
        dl.clip_push(map.plot_area.origin, map.plot_area.extent);
        draw_grid(dl, map, t);
        draw_series(dl, map, std::span<const Series>(series_));
        draw_cursor(dl, map, cursor.get(), t);
        dl.clip_pop();
        draw_axes_labels(dl, map, x_label.get(), y_label.get(), t);
    }

    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    std::vector<Series> series_;
};

inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& /*nd*/, Rect bounds)
{
    auto map = compute_mapping(bounds, x_range, y_range, view,
                               std::span<const Series>(series_));

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
        if (mb->button == 0) {
            if (mb->pressed) {
                drag_mode = DragMode::Pan;
                drag_start_pixel = mb->position;
                drag_start_view = view.get();

                auto xr = x_range.get();
                auto yr = y_range.get();
                if (xr.auto_fit) { xr.auto_fit = false; x_range.set(xr); }
                if (yr.auto_fit) { yr.auto_fit = false; y_range.set(yr); }
            } else {
                drag_mode = DragMode::None;
            }
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        if (!map.plot_area.contains(ms->position)) return;

        double factor = std::pow(1.1, ms->dy.raw());
        auto [data_x, data_y] = map.to_data(ms->position);

        auto v = view.get();
        double old_scale_x = v.scale_x;
        double old_scale_y = v.scale_y;
        v.scale_x *= factor;
        v.scale_y *= factor;

        auto base_x = x_range.get();
        auto base_y = y_range.get();
        double cx = (base_x.min + base_x.max) / 2.0;
        double cy = (base_y.min + base_y.max) / 2.0;

        v.offset_x = data_x - cx + (cx + v.offset_x - data_x) * old_scale_x / v.scale_x;
        v.offset_y = data_y - cy + (cy + v.offset_y - data_y) * old_scale_y / v.scale_y;

        view.set(v);

        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) { xr.auto_fit = false; x_range.set(xr); }
        if (yr.auto_fit) { yr.auto_fit = false; y_range.set(yr); }
    }
}

} // namespace prism::plot
