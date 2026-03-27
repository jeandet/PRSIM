#pragma once

#include <prism/core/app.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace prism {

template <typename State>
using UpdateFn = std::function<void(State&, const InputEvent&)>;

template <typename State>
class Ui {
public:
    const State* operator->() const { return state_; }
    const State& state() const { return *state_; }
    Frame& frame() { return *frame_; }

private:
    const State* state_;
    Frame* frame_;

    Ui(const State& s, Frame& f) : state_(&s), frame_(&f) {}

    template <typename S>
    friend void app(Backend, BackendConfig, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(Backend, BackendConfig,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view, S,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
    template <typename S>
    friend void app(std::string_view,
                    std::function<void(Ui<S>&)>, UpdateFn<S>);
};

template <typename State>
void app(Backend backend, BackendConfig cfg, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
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

    State state = std::move(initial);
    Frame frame;
    int w = cfg.width, h = cfg.height;
    uint64_t version = 0;

    AppAccess::reset(frame, w, h);
    Ui<State> ui(state, frame);
    view(ui);
    backend.submit(AppAccess::take_snapshot(frame, ++version));
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
            if (update) { update(state, *ev); }
        }

        AppAccess::reset(frame, w, h);
        Ui<State> ui2(state, frame);
        view(ui2);

        if (!running.load(std::memory_order_relaxed)) break;

        backend.submit(AppAccess::take_snapshot(frame, ++version));
        backend.wake();
    }

    backend.quit();
    backend_thread.join();
}

template <typename State>
void app(Backend backend, BackendConfig cfg,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(std::move(backend), cfg, State{}, std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title, State initial,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    BackendConfig cfg{.title = title.data(), .width = 800, .height = 600};
    app<State>(Backend::software(cfg), cfg, std::move(initial), std::move(view), std::move(update));
}

template <typename State>
void app(std::string_view title,
         std::function<void(Ui<State>&)> view, UpdateFn<State> update = {}) {
    app<State>(title, State{}, std::move(view), std::move(update));
}

} // namespace prism
