#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>
#include "../showcase/showcase_common.hpp"
#include "lab_model.hpp"
#include "headless_lab_backend.hpp"

#include <fmt/format.h>

#include <chrono>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// The perf lab workload: a 1 kHz synthetic telemetry stream ingested through a
// coalescing Shared<double> (intermediate samples are dropped by design), a 1M-point
// ring plot, and a 100k-row table whose visible rows rebind every publish. The stats
// line at the bottom shows what that costs: present FPS, present/build times, dirty
// counts, snapshot bytes, snapshot age.
struct PerfLab {
    prism::Shared<double> telemetry{};

    perf_lab::RingBuffer ring;
    perf_lab::LabTable table;
    prism::plot::PlotModel plot;
    prism::Field<std::string> stats_line{"warming up"};

    uint64_t tick_count = 0;

    // ~2 Hz stats-window state
    std::chrono::steady_clock::time_point stats_window_at_{};
    uint64_t window_present_count_ = 0;

    PerfLab(size_t rows, size_t points) : ring(points), table(rows) {
        plot.x_label.set("sample");
        plot.y_label.set("value");
        // The series source reads the live ring buffer at record() time — added once,
        // never rebuilt; revision bumps drive re-recording.
        plot.add_series(perf_lab::RingPlotSource{&ring}, prism::plot::SeriesStyle{});
        // Pre-fill the ring to capacity: --points is the number of points the plot
        // RENDERS (the record() stress this lab exists to measure), not a capacity the
        // 1 kHz stream would need ~points/rate seconds to reach. Negative t = history.
        perf_lab::TelemetryGenerator gen;
        for (size_t i = 0; i < points; ++i)
            ring.push(gen.next(-static_cast<double>(points - i) / 1000.0));
    }

    void drain() { telemetry.drain_notifications(); }

    void ingest(double v) {
        ring.push(v);
        table.update_slice((tick_count * 997) % table.row_count(), 1000, v);
        ++tick_count;
        plot.notify();
    }

    // Throttled to ~2 Hz; called from the perpetual AnimationClock callback in setup.
    void update_stats(prism::AppContext& ctx, std::chrono::steady_clock::time_point now) {
        if (stats_window_at_ == std::chrono::steady_clock::time_point{}) {
            stats_window_at_ = now;
            return;
        }
        const double dt = std::chrono::duration<double>(now - stats_window_at_).count();
        if (dt < 0.5) return;

        double fps = 0.0, present_ms = 0.0;
        bool have_presents = false;
        if (auto ps = ctx.backend().present_stats(ctx.window().id())) {
            fps = perf_lab::rate_per_second(ps->present_count - window_present_count_, dt);
            window_present_count_ = ps->present_count;
            present_ms = ps->last_present_ms;
            have_presents = true;
        }
        stats_window_at_ = now;

        std::string scene = "no snapshot yet";
        if (auto* entry = ctx.registry().find(ctx.window().id()); entry && entry->current_snap) {
            const auto& s = *entry->current_snap;
            const double age_ms =
                std::chrono::duration<double, std::milli>(now - s.built_at).count();
            scene = fmt::format("build {:.1f}ms · dirty {} · cmds {} · {} · age {:.0f}ms",
                                s.build_time_ms, s.dirty_widget_count, s.draw_command_count,
                                perf_lab::format_bytes(s.approx_bytes), age_ms);
        }
        stats_line.set(have_presents
            ? fmt::format("FPS {:.1f} · present {:.1f}ms · {}", fps, present_ms, scene)
            : fmt::format("FPS — · {}", scene));
    }

    // A representative static frame for the SVG capture path (no live telemetry there).
    void seed_demo_data() {
        perf_lab::TelemetryGenerator gen;
        for (size_t i = 0; i < 2000; ++i)
            ring.push(gen.next(static_cast<double>(i) * 0.01));
        table.update_slice(0, table.row_count(), 0.5);
        stats_line.set("perf lab — static capture");
        plot.notify();
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.hstack([&] {
                vb.table(table);
                vb.canvas(plot)
                    .depends_on(plot.x_range)
                    .depends_on(plot.y_range)
                    .depends_on(plot.view)
                    .depends_on(plot.cursor)
                    .depends_on(plot.revision);
            });
            vb.widget(stats_line);
        });
    }
};

// Drift-free fixed-rate producer: posts the telemetry value at wall time t into the
// coalescing Shared — the logic thread drains whatever is latest.
static void produce_loop(std::stop_token st, prism::Shared<double>& out, double rate_hz) {
    perf_lab::TelemetryGenerator gen;
    const auto t0 = std::chrono::steady_clock::now();
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / rate_hz));
    for (uint64_t n = 1; !st.stop_requested(); ++n) {
        const double t =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        out.set(gen.next(t));
        std::this_thread::sleep_until(t0 + period * n);
    }
}

static constexpr std::string_view kUsage =
    "usage: perf_lab [--rows N] [--points N] [--rate HZ] [--headless SECONDS] [capture.svg]";

int main(int argc, char* argv[]) {
    auto cfg = perf_lab::parse_lab_args({argv + 1, argv + argc});
    if (!cfg) {
        fmt::print(stderr, "{}\n", kUsage);
        return 2;
    }
    PerfLab app(cfg->rows, cfg->points);

    // One-frame SVG capture (examples convention: positional argv[1] is the output path).
    if (!cfg->svg_path.empty()) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1280, 800);
    }

    if (cfg->headless_seconds > 0) {
        auto lab_backend = std::make_unique<perf_lab::HeadlessLabBackend>(cfg->headless_seconds);
        auto* stats_source = lab_backend.get();
        auto backend = prism::Backend{std::move(lab_backend)};
        auto& window = backend.create_window({.title = "perf_lab", .width = 1280, .height = 800});

        std::jthread producer;
        const auto started = std::chrono::steady_clock::now();
        prism::model_app(backend, window, app, [&](prism::AppContext& ctx) {
            app.telemetry.observe([&app](const double& v) { app.ingest(v); });
            // Perpetual tick: keeps Shared<T> draining with zero input events (same
            // rationale as model_system_monitor's heartbeat callback).
            ctx.clock().add([](prism::AnimationClock::time_point) { return true; });
            producer = std::jthread(produce_loop, std::ref(app.telemetry), cfg->rate_hz);
        });
        producer.request_stop();
        producer.join();

        const auto summary = perf_lab::summarize_build_times(stats_source->build_times());
        const auto last = stats_source->last_stats();
        // Rate against measured wall time: the cv deadline overshoots and startup/teardown
        // adds time, so nominal headless_seconds would flatter the printed /s.
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        fmt::print("perf_lab headless report ({} s, {} rows, {} points, {:.0f} Hz target)\n",
                   cfg->headless_seconds, cfg->rows, cfg->points, cfg->rate_hz);
        fmt::print("publishes: {} ({:.1f}/s, measured {:.1f} s)\n", stats_source->publish_count(),
                   perf_lab::rate_per_second(stats_source->publish_count(), seconds), seconds);
        fmt::print("build_time_ms: min {:.2f} median {:.2f} p95 {:.2f} max {:.2f}\n",
                   summary.min_ms, summary.median_ms, summary.p95_ms, summary.max_ms);
        fmt::print("last frame: dirty {} · cmds {} · {}\n",
                   last.dirty_widgets, last.draw_commands,
                   perf_lab::format_bytes(last.approx_bytes));
        return 0;
    }

    std::jthread producer;
    prism::model_app({.title = "PRISM Perf Lab", .width = 1280, .height = 800}, app,
                     [&](prism::AppContext& ctx) {
        app.telemetry.observe([&app](const double& v) { app.ingest(v); });
        ctx.clock().add([&app, &ctx](prism::AnimationClock::time_point now) {
            app.update_stats(ctx, now);
            return true; // perpetual — also what keeps Shared<T> draining with zero input
        });
        producer = std::jthread(produce_loop, std::ref(app.telemetry), cfg->rate_hz);
    });
    return 0;
}
