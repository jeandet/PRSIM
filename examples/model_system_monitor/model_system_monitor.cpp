#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>
#include "proc_metrics.hpp"
#include "process_tree_source.hpp"
#include "../showcase/showcase_common.hpp"

#include <cmath>
#include <numbers>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

#if __cpp_impl_reflection

struct SystemMonitor {
    // Background-thread ingest points. Never placed via vb.widget() -- they are read only
    // through .observe(), which fires during drain via the void drain() opt-in below. A
    // harmless "not placed by view()" debug-build warning is expected for these two fields.
    prism::Shared<SystemSample> sys_sample{};
    prism::Shared<std::vector<ProcessInfo>> proc_list{};

    History cpu_history;
    History mem_history;
    History net_history;

    prism::plot::PlotGroup plot_group;
    prism::plot::PlotPanel* cpu_plot = &plot_group.add_plot("CPU %");
    prism::plot::PlotPanel* mem_plot = &plot_group.add_plot("Mem (MB)");
    prism::plot::PlotPanel* net_plot = &plot_group.add_plot("Net (KB/s)");

    prism::Field<SortKey> sort_key{SortKey::CpuPercent};
    prism::List<ProcessInfo> table_rows;
    FlatProcessTreeSource tree_source;
    prism::TreeController tree_ctrl{prism::wrap_tree_storage(tree_source)};
    prism::Field<prism::TabBar<>> tabs;

    prism::Field<float> heartbeat_phase{0.f};

    static void rebuild_plot(prism::plot::PlotPanel& plot, const History& h, bool fill = false) {
        std::vector<double> xs(h.values.size());
        std::vector<double> ys(h.values.begin(), h.values.end());
        for (size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        auto colors = prism::plot::default_series_colors(prism::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{std::move(xs), std::move(ys)},
                        prism::plot::SeriesStyle{colors[0], 2.f, fill, 0.0});
        plot.notify();
    }

    void ingest_system(const SystemSample& s) {
        cpu_history.push(s.cpu_percent);
        mem_history.push(static_cast<float>(s.mem_used_mb));
        net_history.push(static_cast<float>(s.net_rx_kbps));
        rebuild_plot(*cpu_plot, cpu_history, /*fill=*/true);
        rebuild_plot(*mem_plot, mem_history);
        rebuild_plot(*net_plot, net_history);
    }

    void ingest_processes(const std::vector<ProcessInfo>& processes) {
        table_rows.replace_all(sort_by(processes, sort_key.get()));
        tree_source.update(processes);
        tree_ctrl.refresh();
    }

    void seed_demo_data() {
        struct { float cpu; double mem; double net; } samples[] = {
            {12.5f, 4096.0, 128.0}, {18.0f, 4200.0, 96.0},
            {9.0f, 4150.0, 210.0}, {22.0f, 4300.0, 150.0},
        };
        for (auto& s : samples) {
            ingest_system(SystemSample{.cpu_percent = s.cpu, .mem_used_mb = s.mem,
                                        .mem_total_mb = 16384.0, .net_rx_kbps = s.net,
                                        .net_tx_kbps = 32.0});
        }
        std::vector<ProcessInfo> demo;
        demo.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init",
                                    .cpu_percent = 0.1f, .mem_percent = 0.5f});
        demo.push_back(ProcessInfo{.pid = 42, .ppid = 1, .name = "prism_demo",
                                    .cpu_percent = 3.2f, .mem_percent = 1.8f});
        ingest_processes(demo);
    }

    void drain() {
        sys_sample.drain_notifications();
        proc_list.drain_notifications();
    }

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode& node) {
        auto& t = *node.theme;
        float size = 8.f + 4.f * std::sin(heartbeat_phase.get());
        dl.rounded_rect(
            prism::Rect{bounds.origin, prism::Size{prism::Width{size}, prism::Height{size}}},
            t.accent_hover, size * 0.5f);
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            plot_group.view(vb);
            vb.handle();
            vb.widget(sort_key);
            vb.tabs(tabs, [&] {
                vb.tab("Table", [&](prism::WidgetTree::ViewBuilder& tvb) {
                    tvb.table(table_rows);
                });
                vb.tab("Tree", [&](prism::WidgetTree::ViewBuilder& tvb) {
                    tvb.tree(tree_ctrl);
                });
            });
            vb.canvas(*this).depends_on(heartbeat_phase).min_size(prism::Height{24});
        });
    }
};

int main(int argc, char* argv[]) {
    SystemMonitor app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 900, 700);
    }

    std::jthread sys_thread;
    std::jthread proc_thread;

    prism::model_app({.title = "PRISM System Monitor", .width = 900, .height = 700,
                       .decoration = prism::DecorationMode::Custom},
                      app, [&](prism::AppContext& ctx) {
        ctx.clock().add([&app](prism::AnimationClock::time_point now) {
            double t = std::chrono::duration<double>(now.time_since_epoch()).count();
            // now.time_since_epoch() is steady_clock's time since boot, which on a machine
            // with real uptime is huge -- wrap into [0, 2pi) with fmod (double precision)
            // BEFORE casting to float, so the float's ULP stays negligible next to one
            // frame's phase advance. Casting the huge value straight to float would give
            // it a ULP larger than a frame's worth of advance, silently freezing
            // heartbeat_phase.set() (Field<T>::set() no-ops on an unchanged value) and
            // making the heartbeat visibly stutter.
            double phase = std::fmod(t * 4.0, 2.0 * std::numbers::pi); // ~4 rad/s
            app.heartbeat_phase.set(static_cast<float>(phase));
            return true; // never remove -- keeps schedule_tick perpetually re-scheduling,
                         // which is what lets Task 1's fix keep draining Shared<T> with
                         // zero mouse/keyboard input.
        });
        app.sys_sample.observe([&app](const SystemSample& s) { app.ingest_system(s); });
        app.proc_list.observe([&app](const std::vector<ProcessInfo>& p) {
            app.ingest_processes(p);
        });
        // Re-sort immediately on a Dropdown change using the last known snapshot, rather
        // than waiting up to 1.5s for the next background poll to land.
        app.sort_key.observe([&app](const SortKey&) {
            app.ingest_processes(app.proc_list.get());
        });

        sys_thread = std::jthread(poll_system_loop, std::ref(app.sys_sample));
        proc_thread = std::jthread(poll_processes_loop, std::ref(app.proc_list));
    });

    return 0;
}

#else
int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::ofstream(argv[1]) << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    return 0;
}
#endif
