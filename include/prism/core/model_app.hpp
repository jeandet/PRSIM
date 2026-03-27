#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/widget_tree.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <variant>

namespace prism {

template <typename Model>
void model_app(Backend backend, BackendConfig cfg, Model& model) {
    mpsc_queue<InputEvent> input_queue;
    std::atomic<bool> running{true};
    std::atomic<bool> input_pending{false};

    std::thread backend_thread([&] {
        backend.run([&](const InputEvent& ev) {
            input_queue.push(ev);
            input_pending.store(true, std::memory_order_release);
            input_pending.notify_one();
        });
    });

    backend.wait_ready();

    WidgetTree tree(model);
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    // Initial snapshot
    backend.submit(tree.build_snapshot(w, h, ++version));
    backend.wake();

    while (running.load(std::memory_order_relaxed)) {
        input_pending.wait(false, std::memory_order_acquire);
        input_pending.store(false, std::memory_order_relaxed);

        while (auto ev = input_queue.pop()) {
            if (std::holds_alternative<WindowClose>(*ev)) {
                running.store(false, std::memory_order_relaxed);
                break;
            }
            if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                w = resize->width;
                h = resize->height;
            }
            // Future: hit_test + event routing to widget senders
        }

        if (!running.load(std::memory_order_relaxed)) break;

        // Rebuild snapshot (only dirty widgets re-record in the future)
        backend.submit(tree.build_snapshot(w, h, ++version));
        backend.wake();
        tree.clear_dirty();
    }

    backend.quit();
    backend_thread.join();
}

template <typename Model>
void model_app(std::string_view title, Model& model) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    model_app(Backend::software(cfg), cfg, model);
}

} // namespace prism
