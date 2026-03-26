#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/render_loop.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace prism {

struct AppConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

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
    DrawList dl_;
    int width_ = 0;
    int height_ = 0;

    void reset(int w, int h) {
        dl_.clear();
        width_ = w;
        height_ = h;
    }

    SceneSnapshot take_snapshot(uint64_t version) {
        SceneSnapshot snap;
        snap.version = version;
        if (!dl_.empty()) {
            snap.geometry.push_back({0, {0, 0, static_cast<float>(width_), static_cast<float>(height_)}});
            snap.draw_lists.push_back(std::move(dl_));
            snap.z_order.push_back(0);
        }
        dl_.clear();
        return snap;
    }
};

class App {
public:
    explicit App(AppConfig config) : config_(config) {}

    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void quit() {
        running_.store(false, std::memory_order_relaxed);
        // Wake both threads so they can observe running_ == false
        input_pending_.store(true, std::memory_order_release);
        input_pending_.notify_one();
        wake_render_thread();
    }

    void run(std::function<void(Frame&)> on_frame) {
        running_.store(true, std::memory_order_relaxed);

        std::thread render_thread([this] {
            RenderLoop loop(
                {config_.title, config_.width, config_.height},
                snapshot_cell_, input_queue_, running_,
                input_pending_, sdl_ready_);
            loop.run();
        });

        // Wait for SDL to be initialized before touching events
        sdl_ready_.wait(false, std::memory_order_acquire);

        Frame frame;
        uint64_t version = 0;
        int w = config_.width;
        int h = config_.height;

        // Produce initial frame
        frame.reset(w, h);
        on_frame(frame);
        snapshot_cell_.store(frame.take_snapshot(++version));
        wake_render_thread();

        // Event-driven loop: sleep until input arrives
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
            snapshot_cell_.store(frame.take_snapshot(++version));
            wake_render_thread();
        }

        wake_render_thread(); // ensure render thread exits SDL_WaitEvent
        render_thread.join();
    }

private:
    AppConfig config_;
    atomic_cell<SceneSnapshot> snapshot_cell_;
    mpsc_queue<InputEvent> input_queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> input_pending_{false};
    std::atomic<bool> sdl_ready_{false};

    void wake_render_thread() {
        SDL_Event wake{};
        wake.type = SDL_EVENT_USER;
        SDL_PushEvent(&wake);
    }
};

} // namespace prism
