#pragma once
#include <prism/render/draw_list.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/ui/context.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <span>
#include <string>
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
    bool operator==(const PlotMapping&) const = default;

    Point to_pixel(double data_x, double data_y) const
    {
        X px = plot_area.origin.x + DX{
            static_cast<float>((data_x - x_range.min) / (x_range.max - x_range.min))
            * plot_area.extent.w.raw()};
        Y py = plot_area.origin.y + DY{
            static_cast<float>(1.0 - (data_y - y_range.min) / (y_range.max - y_range.min))
            * plot_area.extent.h.raw()};
        return Point{px, py};
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

    X left() const { return plot_area.origin.x; }
    X right() const { return left() + DX{plot_area.extent.w.raw()}; }
    Y top() const { return plot_area.origin.y; }
    Y bottom() const { return top() + DY{plot_area.extent.h.raw()}; }

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

template <typename S>
concept ScannableSeries = requires(const S& s, void* ctx, void (*v)(void*, double, double)) {
    { s.has_scan() } -> std::convertible_to<bool>;
    { s.scan(ctx, v) } -> std::same_as<void>;
};

struct BothAxisFit {
    AxisRange x{};
    AxisRange y{};
};

// Both axes in a single pass: the previous per-axis scans visited every sample
// twice (two std::function dispatches per sample each). Per-axis updates keep
// their exact standalone order, so a combined scan returns bit-identical ranges.
template <typename Range>
BothAxisFit auto_fit_ranges(const Range& series)
{
    struct FitState {
        double x_lo = std::numeric_limits<double>::max();
        double x_hi = std::numeric_limits<double>::lowest();
        double y_lo = std::numeric_limits<double>::max();
        double y_hi = std::numeric_limits<double>::lowest();
        bool any = false;
    };
    FitState state;
    auto visit = [](void* ctx, double x, double y) {
        auto& st = *static_cast<FitState*>(ctx);
        st.x_lo = std::min(st.x_lo, x);
        st.x_hi = std::max(st.x_hi, x);
        st.y_lo = std::min(st.y_lo, y);
        st.y_hi = std::max(st.y_hi, y);
        st.any = true;
    };

    for (auto& s : series) {
        if constexpr (ScannableSeries<std::remove_cvref_t<decltype(s)>>) {
            if (s.has_scan()) {
                s.scan(&state, visit);
                continue;
            }
        }
        for (size_t i = 0; i < s.size(); ++i) {
            visit(&state, s.x(i), s.y(i));
        }
    }

    auto finish = [](double lo, double hi) {
        if (lo == hi) { lo -= 0.5; hi += 0.5; }
        double pad = (hi - lo) * 0.05;
        return AxisRange{lo - pad, hi + pad, true};
    };
    if (!state.any) return {{0.0, 1.0, true}, {0.0, 1.0, true}};
    return {finish(state.x_lo, state.x_hi), finish(state.y_lo, state.y_hi)};
}

template <typename Range>
AxisRange auto_fit_range(const Range& series, Axis axis)
{
    BothAxisFit fit = auto_fit_ranges(series);
    return (axis == Axis::X) ? fit.x : fit.y;
}

constexpr Width margin_left{60.f};
constexpr Height margin_bottom{45.f};
constexpr Height margin_top{10.f};
constexpr Width margin_right{10.f};
constexpr float tick_len = 5.f; // axis-polymorphic: used as both a horizontal and vertical tick length
constexpr float tick_font_size = 11.f;
constexpr float label_font_size = 12.f;

struct CursorState {
    double data_x = 0.0;
    double data_y = 0.0;
    bool visible = false;
    bool operator==(const CursorState&) const = default;
};

template <typename C>
concept PlotCursor = requires(C c) {
    { c.data_x } -> std::convertible_to<double>;
    { c.visible } -> std::convertible_to<bool>;
};

struct PlotGroupCursor {
    double data_x = 0.0;
    bool visible = false;
    bool operator==(const PlotGroupCursor&) const = default;
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
        X x = map.to_pixel(tx, 0.0).x;
        if (x < map.left() || x > map.right()) continue;
        dl.line(Point{x, map.top()}, Point{x, map.bottom()}, t.track, 1.f);
    }

    for (double ty : ticks.y) {
        Y y = map.to_pixel(0.0, ty).y;
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{map.left(), y}, Point{map.right(), y}, t.track, 1.f);
    }

    dl.line(Point{map.left(), map.top()}, Point{map.left(), map.bottom()}, t.border, 1.f);
    dl.line(Point{map.left(), map.bottom()}, Point{map.right(), map.bottom()}, t.border, 1.f);
}

inline void draw_tick_labels(DrawList& dl, const PlotMapping& map,
                             const TickArrays& ticks, const Theme& t,
                             bool draw_x_axis = true,
                             const std::function<std::string(double)>& x_tick_format = {})
{
    Width cw = char_width(tick_font_size);

    if (draw_x_axis) {
        for (double tx : ticks.x) {
            X x = map.to_pixel(tx, 0.0).x;
            if (x < map.left() || x > map.right()) continue;
            dl.line(Point{x, map.bottom()},
                    Point{x, map.bottom() + DY{tick_len}}, t.border, 1.f);
            auto label = x_tick_format ? x_tick_format(tx) : fmt::format("{:.6g}", tx);
            Width label_w = cw * static_cast<float>(label.size());
            dl.text(std::move(label),
                    Point{x - DX{label_w.raw() / 2.f}, map.bottom() + DY{tick_len + 2.f}},
                    tick_font_size, t.text_muted);
        }
    }

    for (double ty : ticks.y) {
        Y y = map.to_pixel(0.0, ty).y;
        if (y < map.top() || y > map.bottom()) continue;
        dl.line(Point{map.left() - DX{tick_len}, y},
                Point{map.left(), y}, t.border, 1.f);
        auto label = fmt::format("{:.6g}", ty);
        Width label_w = cw * static_cast<float>(label.size());
        dl.text(std::move(label),
                Point{map.left() - DX{tick_len + 2.f + label_w.raw()}, y - DY{6.f}},
                tick_font_size, t.text_muted);
    }
}

// Gate for min-max decimation: per-column bucketing would turn a non-monotonic
// series' back-and-forth lines into an envelope — a silent visual change, so dense
// decimation is only allowed for non-decreasing x.
template <typename S>
bool series_is_monotonic_x(const S& s)
{
    struct MonoState {
        double prev = 0.0;
        bool first = true;
        bool ok = true;
    };
    if constexpr (ScannableSeries<S>) {
        if (s.has_scan()) {
            MonoState state;
            auto visit = [](void* ctx, double x, double) {
                auto& st = *static_cast<MonoState*>(ctx);
                if (!st.first && x < st.prev) st.ok = false;
                st.prev = x;
                st.first = false;
            };
            s.scan(&state, visit);
            return state.ok;
        }
    }
    for (size_t i = 1; i < s.size(); ++i)
        if (s.x(i) < s.x(i - 1)) return false;
    return true;
}

// Min-max decimation: the min-y and max-y pixel point of each pixel column, in column
// order — the exact visual envelope of the full polyline at this density, at
// <= 2 points per column. Only meaningful for monotonic-x series (see the gate above).
// NaN samples are skipped, so a NaN-only column vanishes (a gap) instead of drawing
// a garbage point.
template <typename S>
std::vector<Point> decimated_series_points(const S& s, const PlotMapping& map)
{
    const float left = map.left().raw();
    const int columns = std::max(1, static_cast<int>(map.right().raw() - left));

    struct Bucket {
        Point min_pt{}, max_pt{};
        size_t min_i = 0, max_i = 0;
        bool any = false;
    };
    std::vector<Bucket> buckets(static_cast<size_t>(columns));

    struct DecimState {
        const PlotMapping* map;
        std::vector<Bucket>* buckets;
        float left;
        int columns;
        size_t i = 0;
    };
    DecimState state{&map, &buckets, left, columns};
    auto visit = [](void* ctx, double x, double y) {
        auto& st = *static_cast<DecimState*>(ctx);
        const Point p = st.map->to_pixel(x, y);
        const size_t i = st.i++;
        if (std::isnan(p.x.raw()) || std::isnan(p.y.raw())) return;
        // Points outside the visible x-range fold into the edge columns, keeping the
        // polyline continuous instead of opening a gap at the boundary.
        const int col = std::clamp(static_cast<int>(p.x.raw() - st.left), 0, st.columns - 1);
        auto& b = (*st.buckets)[static_cast<size_t>(col)];
        if (!b.any || p.y.raw() < b.min_pt.y.raw()) { b.min_pt = p; b.min_i = i; }
        if (!b.any || p.y.raw() > b.max_pt.y.raw()) { b.max_pt = p; b.max_i = i; }
        b.any = true;
    };
    if constexpr (ScannableSeries<S>) {
        if (s.has_scan()) {
            s.scan(&state, visit);
        } else {
            for (size_t i = 0; i < s.size(); ++i) visit(&state, s.x(i), s.y(i));
        }
    } else {
        for (size_t i = 0; i < s.size(); ++i) visit(&state, s.x(i), s.y(i));
    }

    std::vector<Point> out;
    out.reserve(static_cast<size_t>(columns) * 2);
    for (const auto& b : buckets) {
        if (!b.any) continue;
        if (b.min_i == b.max_i) {
            out.push_back(b.min_pt);
        } else if (b.min_i < b.max_i) {
            out.push_back(b.min_pt);
            out.push_back(b.max_pt);
        } else {
            out.push_back(b.max_pt);
            out.push_back(b.min_pt);
        }
    }
    return out;
}

// Memoized per-series polyline points for one (data revision, mapping) pair. A hit
// skips the monotonicity gate and the full transform pass -- the wins show up on
// re-renders with unchanged data (cursor moves, hover, expose), not on streaming
// ingest, where the revision bump correctly invalidates every frame.
struct SeriesDrawCache {
    std::vector<Point> pts;
    PlotMapping map{};
    uint32_t revision = 0;
    size_t data_size = 0;
    bool decimated = false;
    bool valid = false;
};

template <typename SeriesRange>
void draw_series(DrawList& dl, const PlotMapping& map, const SeriesRange& series,
                 uint32_t data_revision = 0,
                 std::span<SeriesDrawCache> caches = {})
{
    const bool caching = caches.size() == series.size();
    size_t index = 0;
    for (auto& s : series) {
        SeriesDrawCache* slot = caching ? &caches[index++] : nullptr;
        if (s.size() < 2) continue;

        std::vector<Point> pts;
        if (slot && slot->valid && slot->revision == data_revision
            && slot->data_size == s.size() && slot->map == map) {
            pts = slot->pts;
        } else {
            // A series much denser than the plot is wide pays O(N) transform + allocation
            // here and O(N) per-segment rendering in the backend, for output no pixel can
            // show (measured by the perf lab: ~260 ms/publish at 1M points). Min-max
            // decimation is the exact visual envelope at <= 2 points per pixel column;
            // non-monotonic-x series keep the full-fidelity path — their back-and-forth
            // line shape IS the data.
            const float plot_w = map.right().raw() - map.left().raw();
            const bool decimate = plot_w > 0.f
                && s.size() > static_cast<size_t>(plot_w) * 4
                && series_is_monotonic_x(s);

            if (decimate) {
                pts = decimated_series_points(s, map);
            } else {
                pts.reserve(s.size());
                struct PushState {
                    const PlotMapping* map;
                    std::vector<Point>* pts;
                };
                PushState state{&map, &pts};
                auto visit = [](void* ctx, double x, double y) {
                    auto& st = *static_cast<PushState*>(ctx);
                    st.pts->push_back(st.map->to_pixel(x, y));
                };
                using Elem = std::remove_cvref_t<decltype(s)>;
                if constexpr (ScannableSeries<Elem>) {
                    if (s.has_scan()) {
                        s.scan(&state, visit);
                    } else {
                        for (size_t i = 0; i < s.size(); ++i)
                            visit(&state, s.x(i), s.y(i));
                    }
                } else {
                    for (size_t i = 0; i < s.size(); ++i)
                        visit(&state, s.x(i), s.y(i));
                }
            }

            if (slot)
                *slot = SeriesDrawCache{pts, map, data_revision, s.size(), decimate, true};
        }

        if (s.style().fill) {
            std::vector<Point> strip;
            strip.reserve(pts.size() * 2);
            const Y baseline_y = map.to_pixel(0.0, s.style().baseline).y;
            for (const auto& p : pts) {
                strip.push_back(p);
                strip.push_back(Point{p.x, baseline_y});
            }
            Color c = s.style().color;
            dl.filled_polygon(std::move(strip), Color::rgba(c.r, c.g, c.b, 40));
        }

        dl.polyline(std::move(pts), s.style().color, s.style().thickness);
    }
}

inline void draw_cursor(DrawList& dl, const PlotMapping& map,
                        const CursorState& cursor, const Theme& t)
{
    if (!cursor.visible) return;

    auto px = map.to_pixel(cursor.data_x, cursor.data_y);
    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);

    dl.line(Point{px.x, map.top()},
            Point{px.x, map.bottom()}, crosshair_color, 1.f);
    dl.line(Point{map.left(), px.y},
            Point{map.right(), px.y}, crosshair_color, 1.f);

    auto label = fmt::format("({:.4g}, {:.4g})", cursor.data_x, cursor.data_y);
    X tx = px.x + DX{10.f};
    Y ty = px.y - DY{20.f};
    if (tx + DX{120.f} > map.right()) tx = px.x - DX{130.f};
    if (ty < map.top()) ty = px.y + DY{10.f};

    dl.filled_rect(Rect{Point{tx - DX{2.f}, ty - DY{2.f}}, Size{Width{120.f}, Height{18.f}}}, t.surface);
    dl.text(std::move(label), Point{tx, ty}, label_font_size, t.text);
}

inline void draw_vertical_cursor(DrawList& dl, const PlotMapping& map,
                                 double data_x, bool visible, const Theme& t)
{
    if (!visible) return;
    auto px = map.to_pixel(data_x, 0.0);
    Color crosshair_color = Color::rgba(t.text_muted.r, t.text_muted.g, t.text_muted.b, 80);
    dl.line(Point{px.x, map.top()}, Point{px.x, map.bottom()}, crosshair_color, 1.f);
}

// Marks each series' nearest sample to `data_x` with a small dot and its value --
// used for a shared (x-only) cursor across a PlotGroup, where each panel has its own
// y-scale and no single mouse-derived y-value would be meaningful across panels.
template <typename SeriesRange>
void draw_series_values_at_cursor(DrawList& dl, const PlotMapping& map,
                                  const SeriesRange& series, double data_x, const Theme& t)
{
    Width cw = char_width(label_font_size);

    for (auto& s : series) {
        if (s.size() == 0) continue;

        struct NearestState {
            double data_x;
            double best_dist = 0.0;
            double nearest_x = 0.0;
            double nearest_y = 0.0;
            bool first = true;
        };
        NearestState state{data_x};
        auto visit = [](void* ctx, double x, double y) {
            auto& st = *static_cast<NearestState*>(ctx);
            double dist = std::abs(x - st.data_x);
            if (st.first || dist < st.best_dist) {
                st.best_dist = dist;
                st.nearest_x = x;
                st.nearest_y = y;
                st.first = false;
            }
        };
        using Elem = std::remove_cvref_t<decltype(s)>;
        if constexpr (ScannableSeries<Elem>) {
            if (s.has_scan()) {
                s.scan(&state, visit);
            } else {
                for (size_t i = 0; i < s.size(); ++i) visit(&state, s.x(i), s.y(i));
            }
        } else {
            for (size_t i = 0; i < s.size(); ++i) visit(&state, s.x(i), s.y(i));
        }

        double value = state.nearest_y;
        auto px = map.to_pixel(state.nearest_x, value);
        if (px.y < map.top() || px.y > map.bottom()) continue;

        dl.circle(px, 3.f, s.style().color, 0.f);

        auto label = fmt::format("{:.4g}", value);
        Width label_w = cw * static_cast<float>(label.size());
        X tx = px.x + DX{8.f};
        Y ty = px.y - DY{16.f};
        if (tx + DX{label_w.raw()} + DX{4.f} > map.right())
            tx = px.x - DX{8.f} - DX{label_w.raw()};
        if (ty < map.top()) ty = px.y + DY{4.f};

        dl.filled_rect(Rect{Point{tx - DX{2.f}, ty - DY{2.f}}, Size{label_w + Width{4.f}, Height{16.f}}}, t.surface);
        dl.text(std::move(label), Point{tx, ty}, label_font_size, s.style().color);
    }
}

inline void draw_axes_labels(DrawList& dl, const PlotMapping& map,
                             const std::string& x_label, const std::string& y_label,
                             const Theme& t, bool draw_x_axis = true)
{
    // X+X has no defined result (adding two absolute positions is meaningless),
    // so the midpoint is computed via the algebraically valid a + (b-a)/2 form.
    X cx = map.left() + (map.right() - map.left()) / 2.f;

    if (draw_x_axis && !x_label.empty())
        dl.text(x_label, Point{cx - DX{30.f}, map.bottom() + DY{18.f}}, label_font_size, t.text);

    if (!y_label.empty()) {
        X lx = map.left() - DX{margin_left.raw() - 10.f};
        Y cy = map.top() + (map.bottom() - map.top()) / 2.f;
        dl.text(y_label, Point{lx, cy}, label_font_size, t.text, 90.f, TextAnchor::Center);
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
