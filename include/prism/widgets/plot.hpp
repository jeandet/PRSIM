#pragma once
#include <prism/core/field.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/widgets/plot_render.hpp>
#include <prism/app/widget_tree.hpp>
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
using namespace prism::app;

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
    bool fill = false;
    double baseline = 0.0;
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
                                   std::span<const Series> series,
                                   bool draw_x_axis = true)
{
    AxisRange eff_x = xr.get();
    AxisRange eff_y = yr.get();

    if (eff_x.auto_fit) eff_x = auto_fit_range(series, Axis::X);
    if (eff_y.auto_fit) eff_y = auto_fit_range(series, Axis::Y);

    auto& v = vt.get();
    eff_x = PlotMapping::apply_view(eff_x, v.offset_x, v.scale_x);
    eff_y = PlotMapping::apply_view(eff_y, v.offset_y, v.scale_y);

    // With no x-axis to label (a stacked panel that isn't the group's bottom one),
    // the tick-label-sized bottom margin would otherwise sit empty -- collapse it
    // to match the top margin instead, so the plot's own box fills the canvas.
    Height bottom_margin = draw_x_axis ? margin_bottom : margin_top;
    Rect plot_area{
        Point{bounds.origin.x + DX{margin_left.raw()},
              bounds.origin.y + DY{margin_top.raw()}},
        Size{bounds.extent.w - margin_left - margin_right,
             bounds.extent.h - margin_top - bottom_margin},
    };

    return PlotMapping{eff_x, eff_y, plot_area};
}

template <PlotCursor C>
void render_plot_panel(DrawList& dl, Rect bounds, const WidgetNode& node,
                       const Field<AxisRange>& x_range, const Field<AxisRange>& y_range,
                       const Field<ViewTransform>& view, const Field<C>& cursor,
                       std::span<const Series> series,
                       const std::string& x_label, const std::string& y_label,
                       bool draw_x_axis)
{
    auto& t = *node.theme;
    auto map = compute_mapping(bounds, x_range, y_range, view, series, draw_x_axis);

    draw_background(dl, map.plot_area, t);
    auto ticks = compute_ticks(map);

    // Inside clip_push, coordinates are local (origin = {0,0})
    PlotMapping local_map = map;
    local_map.plot_area.origin = Point{X{0}, Y{0}};

    dl.clip_push(map.plot_area.origin, map.plot_area.extent);
    draw_grid_lines(dl, local_map, ticks, t);
    draw_series(dl, local_map, series);
    if constexpr (requires (C c) { c.data_y; })
        draw_cursor(dl, local_map, cursor.get(), t);
    else
        draw_vertical_cursor(dl, local_map, cursor.get().data_x, cursor.get().visible, t);
    dl.clip_pop();
    draw_tick_labels(dl, map, ticks, t, draw_x_axis);
    draw_axes_labels(dl, map, x_label, y_label, t, draw_x_axis);
}

template <PlotCursor C>
void route_plot_input(const InputEvent& ev, WidgetNode& /*nd*/, Rect bounds,
                      Field<AxisRange>& x_range, Field<AxisRange>& y_range,
                      Field<ViewTransform>& view, Field<C>& cursor,
                      DragMode& drag_mode, Point& drag_start_pixel,
                      ViewTransform& drag_start_view,
                      std::span<const Series> series,
                      bool draw_x_axis = true)
{
    auto map = compute_mapping(bounds, x_range, y_range, view, series, draw_x_axis);

    auto freeze_auto_fit = [&] {
        auto xr = x_range.get();
        auto yr = y_range.get();
        if (xr.auto_fit) {
            xr = auto_fit_range(series, Axis::X);
            xr.auto_fit = false;
            x_range.set(xr);
        }
        if (yr.auto_fit) {
            yr = auto_fit_range(series, Axis::Y);
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
            if constexpr (requires (C c) { c.data_y; })
                cursor.set(C{dx, dy, true});
            else
                cursor.set(C{dx, true});
        } else {
            auto c = cursor.get();
            if (c.visible) {
                // `cc` (not `c`) avoids shadowing the already-declared local `c` above.
                if constexpr (requires (C cc) { cc.data_y; })
                    cursor.set(C{c.data_x, c.data_y, false});
                else
                    cursor.set(C{c.data_x, false});
            }
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
            if (map.plot_area.contains(mb->position)) {
                view.set(ViewTransform{});
                x_range.set(AxisRange{});
                y_range.set(AxisRange{});
            }
        }

    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) {
        X px = ms->position.x;
        Y py = ms->position.y;

        bool in_plot = map.plot_area.contains(ms->position);
        // No visible x-axis strip to hover over when it's suppressed -- no dedicated
        // x-only-zoom hotspot for a panel whose x-axis isn't drawn.
        bool in_x_axis = draw_x_axis
                          && (px >= map.left() && px <= map.right()
                          && py > map.bottom() && py <= map.bottom() + DY{margin_bottom.raw()});
        bool in_y_axis = (py >= map.top() && py <= map.bottom()
                          && px >= map.left() - DX{margin_left.raw()} && px < map.left());

        if (!in_plot && !in_x_axis && !in_y_axis) return;

        freeze_auto_fit();

        double factor = std::pow(zoom_base, ms->dy.raw());
        Point clamp_pt{std::clamp(px, map.left(), map.right()),
                       std::clamp(py, map.top(), map.bottom())};
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
        render_plot_panel(dl, bounds, node, x_range, y_range, view, cursor,
                          std::span<const Series>(series_), x_label.get(), y_label.get(), true);
    }

    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    std::vector<Series> series_;
};

inline void PlotModel::handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds)
{
    route_plot_input(ev, nd, bounds, x_range, y_range, view, cursor,
                     drag_mode, drag_start_pixel, drag_start_view,
                     std::span<const Series>(series_));
}

class PlotGroup;

class PlotPanel {
  public:
    Field<AxisRange> y_range{};
    Field<ViewTransform> y_view{};   // this panel's own y pan/zoom (offset_y/scale_y only --
                                     // x pan/zoom is shared via PlotGroup::x_view instead)
    Field<std::string> y_label{""};
    Field<uint32_t> revision{0};

    DragMode drag_mode = DragMode::None;
    Point drag_start_pixel{};
    ViewTransform drag_start_view{};

    explicit PlotPanel(std::string label) : y_label(std::move(label)) {}

    template <PlotSource S>
    void add_series(S source, SeriesStyle style)
    {
        series_.emplace_back(std::move(source), style);
    }

    void add_series(std::vector<double> xs, std::vector<double> ys, SeriesStyle style)
    {
        add_series(XYData{std::move(xs), std::move(ys)}, style);
    }

    void clear_series() { series_.clear(); }
    void notify() { revision.set(revision.get() + 1); }

    // Fixed 3-arg signatures -- vb.canvas(panel) requires exactly `canvas(dl, r, n)` /
    // `handle_canvas_input(ev, nd, r)` (see widget_node.hpp's node_canvas()), so the group's
    // shared x-state can't be passed as a call parameter. PlotPanel reaches it through the
    // `group_` back-pointer set by PlotGroup::add_plot() instead.
    void canvas(DrawList& dl, Rect bounds, const WidgetNode& node);
    void handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds);

  private:
    friend class PlotGroup;
    PlotGroup* group_ = nullptr;   // set by PlotGroup::add_plot(); never null once added
    bool draw_x_axis_ = false;     // true only for the group's current last panel
    std::vector<Series> series_;
};

class PlotGroup {
  public:
    Field<AxisRange> x_range{};
    Field<ViewTransform> x_view{};
    Field<PlotGroupCursor> cursor{};
    Field<std::string> x_label{""};

    PlotGroup() = default;
    PlotGroup(PlotGroup&&) = delete;
    PlotGroup& operator=(PlotGroup&&) = delete;

    // node_canvas() captures &panel by reference into the widget tree for the panel's
    // lifetime, so panels must never move once added -- panels_ stores unique_ptr so the
    // owning vector can grow (on later add_plot() calls) without invalidating references
    // returned by earlier add_plot() calls or the &panel captures inside the widget tree.
    PlotPanel& add_plot(std::string y_label = "")
    {
        if (!panels_.empty())
            panels_.back()->draw_x_axis_ = false;
        panels_.push_back(std::make_unique<PlotPanel>(std::move(y_label)));
        panels_.back()->group_ = this;
        panels_.back()->draw_x_axis_ = true;
        return *panels_.back();
    }

    void reset_view()
    {
        x_view.set(ViewTransform{});
        x_range.set(AxisRange{});
        for (auto& p : panels_) {
            p->y_range.set(AxisRange{});
            p->y_view.set(ViewTransform{});
        }
    }

    // Nested in its own vstack so the whole group is exactly one pane to
    // whatever outer container places it -- otherwise each panel would be an
    // independent sibling pane, and an outer split handle would only ever
    // drag the one panel physically adjacent to it (see wire_split_handles's
    // per-non-Handle-child pane counting in widget_tree.hpp). Panels have no
    // min_size, so they expand to fill the group's own allocated height
    // (whatever that is -- the vstack's default share of remaining space, or
    // whatever an outer handle drag sets it to), splitting it evenly.
    void view(WidgetTree::ViewBuilder& vb)
    {
        vb.vstack([&] {
            for (auto& p : panels_) {
                vb.canvas(*p).depends_on(x_range, x_view, cursor, p->y_range, p->y_view, p->revision);
            }
        });
    }

  private:
    std::vector<std::unique_ptr<PlotPanel>> panels_;
};

inline void PlotPanel::canvas(DrawList& dl, Rect bounds, const WidgetNode& node)
{
    ViewTransform merged = group_->x_view.get();
    auto yv = y_view.get();
    merged.offset_y = yv.offset_y;
    merged.scale_y = yv.scale_y;
    Field<ViewTransform> merged_view{merged};

    render_plot_panel(dl, bounds, node, group_->x_range, y_range, merged_view,
                      group_->cursor, std::span<const Series>(series_),
                      group_->x_label.get(), y_label.get(), draw_x_axis_);
}

inline void PlotPanel::handle_canvas_input(const InputEvent& ev, WidgetNode& nd, Rect bounds)
{
    ViewTransform merged = group_->x_view.get();
    auto yv = y_view.get();
    merged.offset_y = yv.offset_y;
    merged.scale_y = yv.scale_y;
    Field<ViewTransform> merged_view{merged};

    route_plot_input(ev, nd, bounds, group_->x_range, y_range, merged_view,
                     group_->cursor, drag_mode, drag_start_pixel, drag_start_view,
                     std::span<const Series>(series_), draw_x_axis_);

    auto result = merged_view.get();

    auto xr = group_->x_view.get();
    xr.offset_x = result.offset_x;
    xr.scale_x = result.scale_x;
    group_->x_view.set(xr);

    auto yr = y_view.get();
    yr.offset_y = result.offset_y;
    yr.scale_y = result.scale_y;
    y_view.set(yr);
}

} // namespace prism::plot
