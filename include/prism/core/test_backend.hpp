#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

namespace prism {

class TestBackend final : public BackendBase {
    std::vector<InputEvent> events_;
public:
    explicit TestBackend(std::vector<InputEvent> events)
        : events_(std::move(events)) {}

    void run(std::function<void(const InputEvent&)> event_cb) override {
        for (const auto& ev : events_)
            event_cb(ev);
        event_cb(WindowClose{});
    }

    void submit(std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
