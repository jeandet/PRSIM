#pragma once

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>

#include <unordered_map>
#include <vector>

namespace prism::app {

class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
    std::unordered_map<WindowId, HeadlessWindow> windows_;
    WindowId next_id_ = 0;
    WindowId primary_id_ = 0;

public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    Window& create_window(WindowConfig cfg) override {
        auto id = ++next_id_;
        auto [it, _] = windows_.emplace(id, HeadlessWindow{id, cfg});
        primary_id_ = id;
        return it->second;
    }

    Window* request_window(WindowConfig cfg) override {
        auto id = ++next_id_;
        auto [it, _] = windows_.emplace(id, HeadlessWindow{id, cfg});
        return &it->second;
    }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(WindowEvent{primary_id_, ev});
        event_cb(WindowEvent{primary_id_, WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism::app
