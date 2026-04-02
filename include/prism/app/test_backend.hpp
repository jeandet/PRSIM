#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

#include <vector>

namespace prism::app {

class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
    HeadlessWindow window_{0, {}};

public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    Window& create_window(WindowConfig cfg) override {
        window_ = HeadlessWindow{1, cfg};
        return window_;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(WindowEvent{window_.id(), ev});
        event_cb(WindowEvent{window_.id(), WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism::app
