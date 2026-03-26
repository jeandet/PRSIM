#pragma once

#include <prism/core/backend.hpp>
#include <prism/core/software_renderer.hpp>

#include <atomic>

struct SDL_Window;

namespace prism {

class SoftwareBackend final : public BackendBase {
public:
    explicit SoftwareBackend(BackendConfig cfg);
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    void run(std::function<void(const InputEvent&)> event_cb) override;
    void submit(std::shared_ptr<const SceneSnapshot> snap) override;
    void wake() override;
    void quit() override;
    void wait_ready() override;

private:
    BackendConfig config_;
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot_;

    void present();
};

} // namespace prism
