#pragma once

#include <prism/core/backend.hpp>

#include <atomic>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_FRect;
typedef struct _TTF_Font TTF_Font;

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
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    std::vector<SDL_FRect> clip_stack_;
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    std::atomic<std::shared_ptr<const SceneSnapshot>> snapshot_;

    void render_snapshot(const SceneSnapshot& snap);
    void render_draw_list(const DrawList& dl);
    void render_cmd(const FilledRect& cmd);
    void render_cmd(const RectOutline& cmd);
    void render_cmd(const TextCmd& cmd);
    void render_cmd(const ClipPush& cmd);
    void render_cmd(const ClipPop& cmd);
};

} // namespace prism
