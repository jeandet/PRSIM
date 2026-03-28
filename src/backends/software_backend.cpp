#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>

namespace prism {

namespace {

SDL_FRect to_sdl(Rect r) {
    return {r.x, r.y, r.w, r.h};
}

SDL_Color to_sdl(Color c) {
    return {c.r, c.g, c.b, c.a};
}

const char* resolve_font_path(const BackendConfig& cfg) {
    if (cfg.font_path) return cfg.font_path;
#ifdef PRISM_FONT_PATH
    return PRISM_FONT_PATH;
#else
    return nullptr;
#endif
}

} // namespace

SoftwareBackend::SoftwareBackend(BackendConfig cfg)
    : config_(cfg)
{}

SoftwareBackend::~SoftwareBackend() {
    if (font_) TTF_CloseFont(font_);
    TTF_Quit();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SoftwareBackend::run(std::function<void(const InputEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(config_.title, config_.width, config_.height, 0);
    renderer_ = SDL_CreateRenderer(window_, nullptr);

    TTF_Init();
    const char* fpath = resolve_font_path(config_);
    if (fpath) {
        font_ = TTF_OpenFont(fpath, 16.0f);
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                event_cb(WindowResize{ev.window.data1, ev.window.data2});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event_cb(MouseMove{{ev.motion.x, ev.motion.y}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, true});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event_cb(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, false});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event_cb(MouseScroll{
                    {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
                break;
            case SDL_EVENT_KEY_DOWN:
                event_cb(KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod});
                break;
            case SDL_EVENT_KEY_UP:
                event_cb(KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod});
                break;
            case SDL_EVENT_USER:
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (!running_.load(std::memory_order_relaxed)) break;

        auto snap = snapshot_.load(std::memory_order_acquire);
        if (snap) {
            render_snapshot(*snap);
        }
    }
}

void SoftwareBackend::submit(std::shared_ptr<const SceneSnapshot> snap) {
    snapshot_.store(std::move(snap), std::memory_order_release);
}

void SoftwareBackend::wake() {
    SDL_Event wake_ev{};
    wake_ev.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake_ev);
}

void SoftwareBackend::wait_ready() {
    ready_.wait(false, std::memory_order_acquire);
}

void SoftwareBackend::quit() {
    running_.store(false, std::memory_order_relaxed);
    wake();
}

void SoftwareBackend::render_snapshot(const SceneSnapshot& snap) {
    SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
    SDL_RenderClear(renderer_);
    for (uint16_t idx : snap.z_order) {
        render_draw_list(snap.draw_lists[idx]);
    }
    SDL_RenderPresent(renderer_);
}

void SoftwareBackend::render_draw_list(const DrawList& dl) {
    for (const auto& cmd : dl.commands) {
        std::visit([this](const auto& c) { render_cmd(c); }, cmd);
    }
}

void SoftwareBackend::render_cmd(const FilledRect& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderFillRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const RectOutline& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const TextCmd& cmd) {
    if (!font_ || cmd.text.empty()) return;

    float current_size = TTF_GetFontSize(font_);
    if (std::abs(current_size - cmd.size) > 0.5f) {
        TTF_SetFontSize(font_, cmd.size);
    }

    SDL_Color color = to_sdl(cmd.color);
    SDL_Surface* surface = TTF_RenderText_Blended(font_, cmd.text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (texture) {
        SDL_FRect dst = {cmd.origin.x, cmd.origin.y,
                         static_cast<float>(surface->w),
                         static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

void SoftwareBackend::render_cmd(const ClipPush& cmd) {
    SDL_Rect r = {static_cast<int>(cmd.rect.x), static_cast<int>(cmd.rect.y),
                  static_cast<int>(cmd.rect.w), static_cast<int>(cmd.rect.h)};
    clip_stack_.push_back(r);
    SDL_SetRenderClipRect(renderer_, &r);
}

void SoftwareBackend::render_cmd(const ClipPop&) {
    if (!clip_stack_.empty()) clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        SDL_SetRenderClipRect(renderer_, &clip_stack_.back());
    }
}

Backend Backend::software(BackendConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
