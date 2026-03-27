#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace prism {

class Frame {
public:
    void filled_rect(Rect r, Color c) { dl_.filled_rect(r, c); }
    void rect_outline(Rect r, Color c, float thickness = 1.0f) { dl_.rect_outline(r, c, thickness); }
    void text(std::string s, Point origin, float size, Color c) { dl_.text(std::move(s), origin, size, c); }
    void clip_push(Rect r) { dl_.clip_push(r); }
    void clip_pop() { dl_.clip_pop(); }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    friend class App;
    friend struct AppAccess;
    DrawList dl_;
    int width_ = 0;
    int height_ = 0;

    void reset(int w, int h) {
        dl_.clear();
        width_ = w;
        height_ = h;
    }

    std::shared_ptr<const SceneSnapshot> take_snapshot(uint64_t version) {
        auto snap = std::make_shared<SceneSnapshot>();
        snap->version = version;
        if (!dl_.empty()) {
            snap->geometry.push_back({0, {0, 0, static_cast<float>(width_), static_cast<float>(height_)}});
            snap->draw_lists.push_back(std::move(dl_));
            snap->z_order.push_back(0);
        }
        dl_.clear();
        return snap;
    }
};

struct AppAccess {
    static void reset(Frame& f, int w, int h) { f.reset(w, h); }
    static std::shared_ptr<const SceneSnapshot> take_snapshot(Frame& f, uint64_t v) {
        return f.take_snapshot(v);
    }
};

class App {
public:
    explicit App(BackendConfig config)
        : backend_(Backend::software(config)), config_(config) {}

    explicit App(Backend backend, BackendConfig config = {})
        : backend_(std::move(backend)), config_(config) {}

    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() {
        running_.store(false, std::memory_order_relaxed);
        input_pending_.store(true, std::memory_order_release);
        input_pending_.notify_one();
        backend_.quit();
    }

    void run(std::function<void(Frame&)> on_frame) {
        running_.store(true, std::memory_order_relaxed);

        std::thread backend_thread([this] {
            backend_.run([this](const InputEvent& ev) {
                input_queue_.push(ev);
                input_pending_.store(true, std::memory_order_release);
                input_pending_.notify_one();
            });
        });

        backend_.wait_ready();

        Frame frame;
        uint64_t version = 0;
        int w = config_.width;
        int h = config_.height;

        frame.reset(w, h);
        on_frame(frame);
        backend_.submit(frame.take_snapshot(++version));
        backend_.wake();

        while (running_.load(std::memory_order_relaxed)) {
            input_pending_.wait(false, std::memory_order_acquire);
            input_pending_.store(false, std::memory_order_relaxed);

            while (auto ev = input_queue_.pop()) {
                if (std::holds_alternative<WindowClose>(*ev)) {
                    running_.store(false, std::memory_order_relaxed);
                    break;
                }
                if (auto* resize = std::get_if<WindowResize>(&*ev)) {
                    w = resize->width;
                    h = resize->height;
                }
            }

            if (!running_.load(std::memory_order_relaxed)) break;

            frame.reset(w, h);
            on_frame(frame);
            backend_.submit(frame.take_snapshot(++version));
            backend_.wake();
        }

        backend_.quit();
        backend_thread.join();
    }

private:
    Backend backend_;
    BackendConfig config_;
    mpsc_queue<InputEvent> input_queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> input_pending_{false};
};

} // namespace prism
