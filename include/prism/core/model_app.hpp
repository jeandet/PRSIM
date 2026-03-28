#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/widget_tree.hpp>

#include <cstdint>
#include <thread>
#include <variant>

namespace prism {

class AppContext {
public:
    using scheduler_type = decltype(std::declval<stdexec::run_loop>().get_scheduler());

    explicit AppContext(scheduler_type s) : sched_(s) {}
    scheduler_type scheduler() const { return sched_; }

private:
    scheduler_type sched_;
};

template <typename Model>
void model_app(Backend backend, BackendConfig cfg, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    stdexec::run_loop loop;
    auto sched = loop.get_scheduler();

    WidgetTree tree(model);
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    std::shared_ptr<const SceneSnapshot> current_snap;

    auto publish = [&] {
        current_snap = std::shared_ptr<const SceneSnapshot>(
            tree.build_snapshot(w, h, ++version));
        backend.submit(current_snap);
        backend.wake();
        tree.clear_dirty();
    };

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
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
                    if (auto* mb = std::get_if<MouseButton>(&ev); mb && current_snap) {
                        if (auto id = hit_test(*current_snap, mb->position))
                            tree.dispatch(*id, ev);
                    }

                    if (tree.any_dirty() || needs_publish)
                        publish();
                })
            );
        });
    });

    backend.wait_ready();
    publish();

    if (setup) {
        auto ctx = AppContext(sched);
        setup(ctx);
    }

    loop.run();

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(std::string_view title, Model& model,
               std::function<void(AppContext&)> setup = nullptr) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    model_app(Backend::software(cfg), cfg, model, std::move(setup));
}

} // namespace prism
