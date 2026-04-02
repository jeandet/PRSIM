#pragma once

#include <prism/app/backend.hpp>
#include <prism/backends/sdl_window.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <atomic>
#include <unordered_map>
#include <memory>

namespace prism::backends {
using namespace prism::app;


class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(RenderConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    Window& create_window(WindowConfig cfg) override;
    void run(std::function<void(const WindowEvent&)> event_cb) override;
    void submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;

private:
    RenderConfig render_config_;
    std::unordered_map<WindowId, std::unique_ptr<SdlWindow>> windows_;
    uint32_t next_id_ = 0;
    TTF_Font* font_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    WindowId pressed_window_ = 0;

    // Per-window snapshot storage
    struct WindowSnapshot {
        std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot;
    };
    std::unordered_map<WindowId, WindowSnapshot> snapshots_;

    WindowId sdl_id_to_prism_id(uint32_t sdl_window_id) const;

    static const char* resolve_font_path(const RenderConfig& cfg);
};

} // namespace prism::backends
