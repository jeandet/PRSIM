#pragma once

#include <prism/ui/animation.hpp>
#include <prism/app/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/window_chrome.hpp>
#include <prism/app/event_routing.hpp>

#include <cstdint>
#include <thread>
#include <variant>

namespace prism::app {
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;


class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s, AnimationClock& c, Window& w)
        : sched_(s), clock_(&c), window_(&w) {}
    scheduler_type scheduler() const { return sched_; }
    AnimationClock& clock() { return *clock_; }
    Window& window() { return *window_; }

private:
    scheduler_type sched_;
    AnimationClock* clock_;
    Window* window_;
};

template <typename Model>
void model_app(Backend& backend, Window& window, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    WidgetTree tree(model);
    AnimationClock anim_clock;
    bool tick_scheduled = false;
    auto [w, h] = window.size();
    if (window.decoration_mode() == DecorationMode::Custom)
        h -= static_cast<int>(WindowChrome::title_bar_h.raw());
    uint64_t version = 0;

    std::shared_ptr<const SceneSnapshot> current_snap;

    auto publish = [&] {
        current_snap = std::shared_ptr<const SceneSnapshot>(
            tree.build_snapshot(w, h, ++version));
        backend.submit(window.id(), current_snap);
        backend.wake();
        tree.clear_dirty();
    };

    std::function<void()> schedule_tick;
    schedule_tick = [&] {
        if (!anim_clock.active() || tick_scheduled) return;
        tick_scheduled = true;
        exec::start_detached(
            stdexec::schedule(sched)
            | stdexec::then([&] {
                tick_scheduled = false;
                anim_clock.tick(AnimationClock::clock::now());
                if (tree.any_dirty())
                    publish();
                if (anim_clock.active())
                    schedule_tick();
            })
        );
    };

    std::thread backend_thread([&] {
        backend.run([&](const WindowEvent& we) {
            const auto& ev = we.event;
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([&, ev] {
                    if (std::holds_alternative<WindowClose>(ev)) {
                        loop.finish();
                        return;
                    }

                    bool needs_publish = false;
                    if (auto* resize = std::get_if<WindowResize>(&ev)) {
                        w = resize->width;
                        h = resize->height;
                        needs_publish = true;
                    }
                    if (current_snap) {
                        if (auto* mm = std::get_if<MouseMove>(&ev))
                            detail::route_mouse_move(tree, *current_snap, *mm);
                        if (auto* mb = std::get_if<MouseButton>(&ev))
                            detail::route_mouse_button(tree, *current_snap, ev, *mb);
                        if (auto* ms = std::get_if<MouseScroll>(&ev))
                            detail::route_mouse_scroll(tree, *current_snap, *ms);
                    }
                    if (auto* kp = std::get_if<KeyPress>(&ev))
                        detail::route_key_press(tree, ev, *kp);
                    if (std::get_if<TextInput>(&ev))
                        detail::route_text_input(tree, ev);

                    tree.drain_shared();
                    if (tree.any_dirty() || needs_publish)
                        publish();
                    schedule_tick();
                })
            );
        });
    });

    backend.wait_ready();
    publish();

    // AppContext must outlive setup — callbacks captured during setup use it.
    auto ctx = AppContext(sched, anim_clock, window);
    if (setup) {
        setup(ctx);
        schedule_tick();
    }

    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(WindowConfig cfg, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    auto backend = Backend::software(RenderConfig{});
    auto& window = backend.create_window(cfg);
    model_app(backend, window, model, std::move(setup));
}

template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    model_app(WindowConfig{.title = title.data()}, model, std::move(setup));
}

} // namespace prism::app
