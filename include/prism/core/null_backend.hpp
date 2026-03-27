#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

namespace prism {

class NullBackend final : public BackendBase {
public:
    void run(std::function<void(const InputEvent&)> event_cb) override {
        event_cb(WindowClose{});
    }

    void submit(std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
};

} // namespace prism
