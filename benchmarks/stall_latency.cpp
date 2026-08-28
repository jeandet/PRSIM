// Perf-lab benchmark #1 (doc/review-2026-08-28.md, "Recommended order of work" step 1):
// how long does an input event take to reach the screen when the app thread's own
// scene-production work (record()/build_snapshot()) is slow?
//
// PRISM's README frames the renderer as independent of application work. Reading the real
// threading code (src/backends/software_backend.cpp) shows the backend/render thread only
// redraws in response to a `wake()` call, and `wake()` is only ever issued by the app thread
// right after it publishes a new snapshot -- there is no independent timer-driven redraw. So
// while the app thread is busy, nothing new can appear on screen; input latency is bounded
// below by however long that busy work takes. This benchmark quantifies that bound directly,
// using the same threaded backend/app-thread split model_app() uses in production (a custom
// BackendBase running on its own std::thread, exactly like the SDL backend), just with a
// headless window so it can run without a display.
//
// A single Field<StallTrigger> stands in for "a widget whose record() is slow" (e.g. a plot
// recomputing a large series) -- clicking it flips its value, marking it dirty, and its
// record() sleeps for a configurable duration before drawing. That isolates exactly the
// app-thread-busy scenario without needing a real expensive workload.
//
// Building this surfaced a second, unplanned finding, since fixed: WidgetTree::build_snapshot()
// was calling a dirty widget's record() twice per publish, not once -- refresh_dirty() runs it
// once pre-layout (its output sizes the widget for measurement), and update_canvas_bounds() was
// then unconditionally re-running it post-layout for every Leaf/Canvas/Handle node in the whole
// window, dirty or not, so delegates can draw at their real allocated size instead of a
// pre-layout guess. That second pass had no dirty check at all: a StaticSibling widget that is
// never mutated still got one record() call on every publish the window made, purely because
// something else in the same window was dirty -- see untouched_sibling_record_calls below,
// which this benchmark's own assertions now expect to be 0. update_canvas_bounds() now only
// re-records when the widget's resolved allocated size actually differs from what its current
// draws were produced at (see its comment in widget_tree.hpp, and tests/test_record_reuse.cpp);
// no widget in this codebase sizes its record() output from field content independent of
// allocated size, so that's a safe, precise gate rather than a heuristic.

#include <prism/app/model_app.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/core/field.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<int> g_stall_ms{0};
std::atomic<int> g_record_calls{0};

struct StallTrigger {
    int tick = 0;
    bool operator==(const StallTrigger&) const = default;
};

// A sibling widget that is never mutated -- its own dirty flag never flips. Its record()
// call count answers a second question: when clicking `trigger` forces this window to
// republish, does an untouched sibling pay a needless re-record too?
struct StaticSibling { int value = 0; bool operator==(const StaticSibling&) const = default; };
std::atomic<int> g_static_record_calls{0};

} // namespace

namespace prism::ui {

template <>
struct Widget<StallTrigger> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Height widget_h{60.f};

    static void record(DrawList& dl, const Field<StallTrigger>& field, WidgetNode& node) {
        g_record_calls.fetch_add(1, std::memory_order_relaxed);
        if (int ms = g_stall_ms.load(std::memory_order_relaxed); ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, widget_h), Color::rgba(40, 40, 40));
        dl.text("tick " + std::to_string(field.get().tick),
                 detail::make_point(X{4.f}, Y{4.f}), 14, Color::rgba(230, 230, 230));
    }

    static void handle_input(Field<StallTrigger>& field, const InputEvent& ev, WidgetNode&) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            field.set(StallTrigger{field.get().tick + 1});
    }
};

template <>
struct Widget<StaticSibling> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height widget_h{30.f};

    static void record(DrawList& dl, const Field<StaticSibling>&, WidgetNode& node) {
        g_static_record_calls.fetch_add(1, std::memory_order_relaxed);
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, widget_h), Color::rgba(20, 20, 20));
    }
    static void handle_input(Field<StaticSibling>&, const InputEvent&, WidgetNode&) {}
};

} // namespace prism::ui

namespace {

struct StallModel {
    Field<StallTrigger> trigger{};
    Field<StaticSibling> sibling{};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(trigger, sibling); }
};

// Real threaded backend, same shape as model_app() drives an SDL backend with: run() executes
// on its own std::thread and feeds input events into the app thread via the callback; submit()
// is called back from the app thread once a new snapshot is ready. std::atomic<size_t>::wait()
// gives the benchmark (running on the backend thread, inside run()) a way to block until a
// specific publish has actually happened, without polling or an arbitrary sleep.
struct BenchBackend final : public prism::BackendBase {
    HeadlessWindow window_{0, {}};
    std::shared_ptr<const SceneSnapshot> latest_;
    std::atomic<size_t> publish_count_{0};

    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot> s) override {
        latest_ = std::move(s);
        publish_count_.fetch_add(1, std::memory_order_release);
        publish_count_.notify_all();
    }
    void wake() override {}
    void quit() override {}

    void run(std::function<void(const WindowEvent&)> cb) override {
        publish_count_.wait(0, std::memory_order_acquire); // initial publish
        auto snap0 = latest_;
        if (snap0->geometry.empty()) { std::fprintf(stderr, "no geometry in initial snapshot\n"); std::exit(1); }
        Point click_at = snap0->geometry[0].second.center();

        std::printf("%-10s %12s %12s\n", "stall_ms", "latency_ms", "overhead_ms");
        for (int stall_ms : {0, 10, 50, 100, 500}) {
            g_stall_ms.store(stall_ms, std::memory_order_relaxed);

            auto before = publish_count_.load(std::memory_order_acquire);
            auto record_before = g_record_calls.load(std::memory_order_relaxed);
            auto static_before = g_static_record_calls.load(std::memory_order_relaxed);
            auto t0 = Clock::now();
            cb(WindowEvent{window_.id(), MouseButton{click_at, 1, true}});
            publish_count_.wait(before, std::memory_order_acquire);
            auto t1 = Clock::now();
            auto record_after = g_record_calls.load(std::memory_order_relaxed);
            auto static_after = g_static_record_calls.load(std::memory_order_relaxed);

            double latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::printf("%-10d %12.2f %12.2f  trigger_record_calls=%d  untouched_sibling_record_calls=%d\n",
                         stall_ms, latency_ms, latency_ms - stall_ms,
                         record_after - record_before, static_after - static_before);

            // The whole point of the "no independent render cadence" architecture: the input
            // event that took `stall_ms` to process is the ONLY thing that produced a new
            // publish. If anything else were redrawing independently during the stall, we'd
            // see extra publish_count_ increments beyond the one this loop iteration caused.
            if (publish_count_.load(std::memory_order_acquire) != before + 1) {
                std::fprintf(stderr, "unexpected publish count after stall_ms=%d\n", stall_ms);
                std::exit(1);
            }
            if (latency_ms < static_cast<double>(stall_ms)) {
                std::fprintf(stderr,
                    "latency %.2fms is LESS than the stall it should include (stall_ms=%d) -- "
                    "the stall isn't actually blocking the app thread's publish path\n",
                    latency_ms, stall_ms);
                std::exit(1);
            }
            if (record_after - record_before != 1) {
                std::fprintf(stderr,
                    "trigger's record() ran %d times, not 1 (stall_ms=%d) -- the "
                    "update_canvas_bounds double-record regression is back\n",
                    record_after - record_before, stall_ms);
                std::exit(1);
            }
            if (static_after != static_before) {
                std::fprintf(stderr,
                    "untouched sibling's record() ran (stall_ms=%d) -- the "
                    "update_canvas_bounds double-record regression is back\n", stall_ms);
                std::exit(1);
            }
        }

        cb(WindowEvent{window_.id(), WindowClose{}});
    }
};

} // namespace

int main() {
    StallModel model;
    auto backend_ptr = std::make_unique<BenchBackend>();
    auto backend = Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({});

    model_app(backend, window, model);

    std::printf("\nOK: input latency tracks app-thread stall duration 1:1, with no "
                "independent redraw filling the gap.\n");
    return 0;
}
