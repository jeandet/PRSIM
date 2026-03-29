#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/exec.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>

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
    void clip_push(Point origin, Size extent) { dl_.clip_push(origin, extent); }
    void clip_pop() { dl_.clip_pop(); }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

private:
    friend class App;
    friend struct AppAccess;
    template <typename> friend class Ui;
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
            snap->geometry.push_back({0, {Point{X{0}, Y{0}}, Size{Width{static_cast<float>(width_)}, Height{static_cast<float>(height_)}}}});
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
        quit_requested_ = true;
        if (loop_) loop_->finish();
    }

    void run(std::function<void(Frame&)> on_frame) {
        stdexec::run_loop loop;
        loop_ = &loop;
        auto sched = loop.get_scheduler();

        Frame frame;
        uint64_t version = 0;
        int w = config_.width;
        int h = config_.height;

        auto publish = [&] {
            frame.reset(w, h);
            on_frame(frame);
            backend_.submit(frame.take_snapshot(++version));
            backend_.wake();
        };

        std::thread backend_thread([&] {
            backend_.run([&](const InputEvent& ev) {
                exec::start_detached(
                    stdexec::schedule(sched)
                    | stdexec::then([&, ev] {
                        if (std::holds_alternative<WindowClose>(ev)) {
                            loop.finish();
                            return;
                        }
                        if (auto* resize = std::get_if<WindowResize>(&ev)) {
                            w = resize->width;
                            h = resize->height;
                        }
                        publish();
                    })
                );
            });
        });

        backend_.wait_ready();
        publish();
        loop.run();
        loop_ = nullptr;
        backend_.quit();
        backend_thread.join();
    }

private:
    Backend backend_;
    BackendConfig config_;
    stdexec::run_loop* loop_ = nullptr;
    bool quit_requested_ = false;
};

} // namespace prism
