#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>

// Reproduces the SDL pump's shutdown shape: after delivering WindowClose it keeps
// dispatching whatever the OS already queued (expose/focus/enter under X11) until the
// app calls quit(). Any event delivered in that window must still find a live run loop.
struct LateEventBackend final : public prism::app::BackendBase {
    prism::app::HeadlessWindow window_{0, {}};
    std::mutex m_;
    std::condition_variable cv_;
    bool quit_called_ = false;

    prism::app::Window& create_window(prism::app::WindowConfig cfg) override {
        window_ = prism::app::HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const prism::app::WindowEvent&)> cb) override {
        cb(prism::app::WindowEvent{window_.id(), prism::input::WindowClose{}});
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, std::chrono::seconds(2), [&] { return quit_called_; });
        }
        cb(prism::app::WindowEvent{window_.id(), prism::input::MouseMove{}});
    }

    void submit(prism::app::WindowId, std::shared_ptr<const prism::render::SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {
        std::lock_guard<std::mutex> lk(m_);
        quit_called_ = true;
        cv_.notify_one();
    }
};
