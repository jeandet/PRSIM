#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/software_renderer.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>

namespace prism {

struct RenderLoopConfig {
    const char* title = "PRISM";
    int width  = 800;
    int height = 600;
};

// Owns the SDL window and render thread frame loop.
// Blocks on SDL_WaitEvent — zero CPU when idle.
class RenderLoop {
public:
    explicit RenderLoop(RenderLoopConfig config,
                        atomic_cell<SceneSnapshot>& snapshot_cell,
                        mpsc_queue<InputEvent>& input_queue,
                        std::atomic<bool>& running,
                        std::atomic<bool>& input_pending,
                        std::atomic<bool>& sdl_ready)
        : snapshot_cell_(snapshot_cell)
        , input_queue_(input_queue)
        , running_(running)
        , input_pending_(input_pending)
        , renderer_(config.width, config.height)
    {
        SDL_Init(SDL_INIT_VIDEO);
        window_ = SDL_CreateWindow(config.title, config.width, config.height, 0);
        sdl_ready.store(true, std::memory_order_release);
        sdl_ready.notify_one();
    }

    ~RenderLoop() {
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    RenderLoop(const RenderLoop&) = delete;
    RenderLoop& operator=(const RenderLoop&) = delete;

    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            SDL_Event ev;
            if (!SDL_WaitEvent(&ev)) continue;
            handle_event(ev);
            while (SDL_PollEvent(&ev)) handle_event(ev);

            if (!running_.load(std::memory_order_relaxed)) break;

            auto snap = snapshot_cell_.load();
            if (snap) {
                renderer_.render_frame(*snap);
                present();
            }
        }
    }

private:
    atomic_cell<SceneSnapshot>& snapshot_cell_;
    mpsc_queue<InputEvent>& input_queue_;
    std::atomic<bool>& running_;
    std::atomic<bool>& input_pending_;
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;

    void push_input(InputEvent event) {
        input_queue_.push(std::move(event));
        input_pending_.store(true, std::memory_order_release);
        input_pending_.notify_one();
    }

    void handle_event(const SDL_Event& ev) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            push_input(WindowClose{});
            running_.store(false, std::memory_order_relaxed);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            renderer_.resize(ev.window.data1, ev.window.data2);
            push_input(WindowResize{ev.window.data1, ev.window.data2});
            break;
        case SDL_EVENT_MOUSE_MOTION:
            push_input(MouseMove{{ev.motion.x, ev.motion.y}});
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            push_input(MouseButton{
                {ev.button.x, ev.button.y}, ev.button.button, true});
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            push_input(MouseButton{
                {ev.button.x, ev.button.y}, ev.button.button, false});
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            push_input(MouseScroll{
                {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
            break;
        case SDL_EVENT_USER:
            break; // wake signal from app thread
        default:
            break;
        }
    }

    void present() {
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
};

} // namespace prism
