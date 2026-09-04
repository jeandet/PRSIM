#pragma once

// Headless backend for `perf_lab --headless N`: run() blocks until the deadline (or a
// close request), then delivers WindowClose — same shape as python's DelayHeadlessBackend
// (python/src/prism_ext.cpp:2264). submit() samples every published snapshot's build
// stats for the end-of-run report. present_stats stays the BackendBase default
// (std::nullopt): there are no presents headless — the report covers publish-side costs.

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace perf_lab {

class HeadlessLabBackend final : public prism::app::BackendBase {
public:
    struct LastFrameStats {
        size_t dirty_widgets = 0;
        size_t draw_commands = 0;
        size_t approx_bytes = 0;
    };

    explicit HeadlessLabBackend(int seconds)
        : deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(seconds)) {}

    prism::app::Window& create_window(prism::app::WindowConfig cfg) override {
        window_ = prism::app::HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const prism::app::WindowEvent&)> event_cb) override {
        {
            std::unique_lock lk(m_);
            cv_.wait_until(lk, deadline_, [&] { return quit_; });
        }
        event_cb(prism::app::WindowEvent{window_.id(), prism::app::WindowClose{}});
    }

    void submit(prism::app::WindowId,
                std::shared_ptr<const prism::render::SceneSnapshot> snap) override {
        std::lock_guard lk(m_);
        ++publish_count_;
        build_times_.push_back(snap->build_time_ms);
        last_ = {snap->dirty_widget_count, snap->draw_command_count, snap->approx_bytes};
    }

    void wake() override {}

    void quit() override {
        { std::lock_guard lk(m_); quit_ = true; }
        cv_.notify_all();
    }

    // Call only after model_app has returned (the logic thread is done submitting).
    uint64_t publish_count() const { return publish_count_; }
    const std::vector<double>& build_times() const { return build_times_; }
    LastFrameStats last_stats() const { return last_; }

private:
    prism::app::HeadlessWindow window_{0, {}};
    std::chrono::steady_clock::time_point deadline_;
    std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false;
    uint64_t publish_count_ = 0;
    std::vector<double> build_times_;
    LastFrameStats last_;
};

} // namespace perf_lab
