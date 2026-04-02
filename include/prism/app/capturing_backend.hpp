#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

namespace prism::app {

class CapturingBackend final : public BackendBase {
public:
    explicit CapturingBackend(std::shared_ptr<const render::SceneSnapshot>& snap_out)
        : snap_ref_(snap_out) {}

    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> cb) override {
        cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const render::SceneSnapshot> s) override {
        snap_ref_ = std::move(s);
    }

    void wake() override {}
    void quit() override {}

private:
    std::shared_ptr<const render::SceneSnapshot>& snap_ref_;
    HeadlessWindow window_{0, {}};
};

} // namespace prism::app
