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
    std::vector<std::shared_ptr<const SceneSnapshot>> submitted_;

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

    void close_window(WindowId id) override { windows_.erase(id); }
    [[nodiscard]] size_t window_count() const { return windows_.size(); }

    void run(std::function<void(const WindowEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(WindowEvent{primary_id_, ev});
        event_cb(WindowEvent{primary_id_, WindowClose{}});
    }

    void submit(WindowId, std::shared_ptr<const SceneSnapshot> s) override {
        submitted_.push_back(std::move(s));
    }
    void wake() override {}
    void quit() override {}

    // Every snapshot submit() received, in publish order -- for tests that need to inspect
    // what the app thread actually produced while replaying `events`, not just that it ran.
    [[nodiscard]] const std::vector<std::shared_ptr<const SceneSnapshot>>& submitted() const {
        return submitted_;
    }
};

} // namespace prism::app
