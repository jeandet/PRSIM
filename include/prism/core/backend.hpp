#pragma once

#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>

#include <functional>
#include <memory>

namespace prism {

struct BackendConfig {
    const char* title  = "PRISM";
    int         width  = 800;
    int         height = 600;
};

class BackendBase {
public:
    virtual ~BackendBase();

    virtual void run(std::function<void(const InputEvent&)> event_cb) = 0;
    virtual void submit(std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;

    // Block until the backend is ready to receive submit()/wake() calls.
    // Called from app thread after spawning the backend thread.
    // Default implementation is a no-op (backend ready immediately).
    virtual void wait_ready() {}
};

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(BackendConfig cfg);

    void run(std::function<void(const InputEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(std::shared_ptr<const SceneSnapshot> s) { impl_->submit(std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }
    void wait_ready() { impl_->wait_ready(); }

    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism
