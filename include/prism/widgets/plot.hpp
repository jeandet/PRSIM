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

struct CursorState {
    double data_x = 0.0;
    double data_y = 0.0;
    bool visible = false;
    bool operator==(const CursorState&) const = default;
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

} // namespace prism::plot
