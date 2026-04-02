#pragma once

#include <prism/core/input_event.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/window.hpp>

#include <functional>
#include <memory>

namespace prism {

class BackendBase {
public:
    virtual ~BackendBase();

    virtual Window& create_window(WindowConfig cfg) = 0;
    virtual void run(std::function<void(const WindowEvent&)> event_cb) = 0;
    virtual void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
    virtual void wait_ready() {}
};

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(RenderConfig cfg);

    Window& create_window(WindowConfig cfg) { return impl_->create_window(std::move(cfg)); }
    void run(std::function<void(const WindowEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(WindowId w, std::shared_ptr<const SceneSnapshot> s) { impl_->submit(w, std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }
    void wait_ready() { impl_->wait_ready(); }

    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism
