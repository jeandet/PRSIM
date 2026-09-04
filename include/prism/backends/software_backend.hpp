#pragma once

#include <prism/app/backend.hpp>
#include <prism/backends/frame_pacer.hpp>
#include <prism/backends/sdl_window.hpp>
#include <prism/core/mpsc_queue.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace prism::backends {
using namespace prism::app;

struct PendingWindowRequest {
    WindowConfig cfg;
    Window* result = nullptr;
    bool done = false;
    std::mutex m;
    std::condition_variable cv;
};

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(RenderConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    Window& create_window(WindowConfig cfg) override;
    Window* request_window(WindowConfig cfg) override;
    void close_window(WindowId id) override;
    void run(std::function<void(const WindowEvent&)> event_cb) override;
    void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;
    std::optional<PresentStats> present_stats(WindowId id) const override;

private:
    RenderConfig render_config_;
    mutable std::mutex windows_mutex_;
    std::unordered_map<WindowId, std::unique_ptr<SdlWindow>> windows_;
    uint32_t next_id_ = 0;
    TTF_Font* font_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    WindowId pressed_window_ = 0;

    // Frame pacing — see doc/design/render-backend.md "Present Cadence".
    // nullopt pacer_ = unpaced (RenderConfig::frame_pacing == false).
    std::optional<FramePacer> pacer_;
    // Last snapshot actually presented per window; a window presents only when the
    // slot's shared_ptr differs (or force_redraw_ contains it). Guarded by windows_mutex_.
    std::unordered_map<WindowId, std::shared_ptr<const SceneSnapshot>> last_presented_;
    std::unordered_set<WindowId> force_redraw_;

    // Per-window snapshot storage — guarded by windows_mutex_ except for
    // per-slot atomic snapshot pointer itself.
    struct WindowSnapshot {
        std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot;
        // Present statistics (see BackendBase::present_stats); written by the render
        // thread, read via present_stats() under windows_mutex_.
        std::atomic<uint64_t> present_count{0};
        std::atomic<int64_t> last_present_ns{0};  // steady_clock epoch ns
        std::atomic<double> last_present_ms{0.0};
    };
    std::unordered_map<WindowId, WindowSnapshot> snapshots_;

    prism::core::mpsc_queue<std::shared_ptr<PendingWindowRequest>> window_requests_;
    void drain_window_requests();

    prism::core::mpsc_queue<WindowId> close_requests_;
    void drain_close_requests();

    WindowId sdl_id_to_prism_id(uint32_t sdl_window_id) const;

    static const char* resolve_font_path(const RenderConfig& cfg);
};

} // namespace prism::backends
