#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/headless_window.hpp>

namespace prism {

class NullBackend final : public BackendBase {
    HeadlessWindow window_{0, {}};

public:
    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        event_cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
