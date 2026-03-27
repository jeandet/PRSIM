#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <cstring>

namespace prism {

SoftwareBackend::SoftwareBackend(BackendConfig cfg)
    : config_(cfg)
    , renderer_(cfg.width, cfg.height)
{}

SoftwareBackend::~SoftwareBackend() {
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SoftwareBackend::run(std::function<void(const InputEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);
    window_ = SDL_CreateWindow(config_.title, config_.width, config_.height, 0);
    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        // Process all pending events
        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer_.resize(ev.window.data1, ev.window.data2);
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
            renderer_.render_frame(*snap);
            present();
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

void SoftwareBackend::present() {
    SDL_Surface* surface = SDL_GetWindowSurface(window_);
    if (!surface) return;

    auto& buf = renderer_.buffer();
    if (surface->w != buf.width() || surface->h != buf.height()) {
        renderer_.resize(surface->w, surface->h);
        return;
    }

    SDL_LockSurface(surface);
    auto* dst = static_cast<uint8_t*>(surface->pixels);
    auto* src = reinterpret_cast<const uint8_t*>(buf.data());
    for (int y = 0; y < buf.height(); ++y) {
        std::memcpy(dst + y * surface->pitch, src + y * buf.pitch(), buf.pitch());
    }
    SDL_UnlockSurface(surface);
    SDL_UpdateWindowSurface(window_);
}

Backend Backend::software(BackendConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
