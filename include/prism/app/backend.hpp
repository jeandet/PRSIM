#pragma once

#include <prism/input/input_event.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/app/window.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace prism::app {
using namespace prism::render;
using namespace prism::input;


// Present-side statistics, reported by backends that actually present frames
// (SoftwareBackend). Pull-based — no cross-thread callbacks. Backends that don't
// present (Null/Test/Capturing/headless) return std::nullopt.
struct PresentStats {
    uint64_t present_count = 0;                          // since run() start
    std::chrono::steady_clock::time_point last_present_at{};
    double last_present_ms = 0.0;                        // duration of last render+present
};

class BackendBase {
public:
    virtual ~BackendBase();

    virtual Window& create_window(WindowConfig cfg) = 0;
    virtual Window* request_window(WindowConfig cfg) { return &create_window(std::move(cfg)); }
    virtual void close_window(WindowId) {}
    virtual void run(std::function<void(const WindowEvent&)> event_cb) = 0;
    virtual void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) = 0;
    virtual void wake() = 0;
    virtual void quit() = 0;
    virtual void wait_ready() {}
    virtual std::optional<PresentStats> present_stats(WindowId) const { return std::nullopt; }
};

class Backend {
    std::unique_ptr<BackendBase> impl_;

public:
    explicit Backend(std::unique_ptr<BackendBase> impl)
        : impl_(std::move(impl)) {}

    static Backend software(RenderConfig cfg);

    Window& create_window(WindowConfig cfg) { return impl_->create_window(std::move(cfg)); }
    Window* request_window(WindowConfig cfg) { return impl_->request_window(std::move(cfg)); }
    void close_window(WindowId id) { impl_->close_window(id); }
    void run(std::function<void(const WindowEvent&)> cb) { impl_->run(std::move(cb)); }
    void submit(WindowId w, std::shared_ptr<const SceneSnapshot> s) { impl_->submit(w, std::move(s)); }
    void wake() { impl_->wake(); }
    void quit() { impl_->quit(); }
    void wait_ready() { impl_->wait_ready(); }
    std::optional<PresentStats> present_stats(WindowId w) const { return impl_->present_stats(w); }

    Backend(Backend&&) noexcept = default;
    Backend& operator=(Backend&&) noexcept = default;
};

} // namespace prism::app
