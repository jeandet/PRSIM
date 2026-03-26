#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/software_renderer.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>
#include <memory>

namespace prism {

struct RenderLoopConfig {
    const char* title = "PRISM";
    int width  = 800;
    int height = 600;
};

// Owns the SDL window and render thread frame loop.
// Created on the render thread. Runs until running flag is false.
class RenderLoop {
public:
    explicit RenderLoop(RenderLoopConfig config,
                        atomic_cell<SceneSnapshot>& snapshot_cell,
                        mpsc_queue<InputEvent>& input_queue,
                        std::atomic<bool>& running)
        : snapshot_cell_(snapshot_cell)
        , input_queue_(input_queue)
        , running_(running)
        , renderer_(config.width, config.height)
    {
        window_ = SDL_CreateWindow(config.title, config.width, config.height, 0);
    }

    ~RenderLoop() {
        if (window_) SDL_DestroyWindow(window_);
    }

    RenderLoop(const RenderLoop&) = delete;
    RenderLoop& operator=(const RenderLoop&) = delete;

    // Blocking frame loop — call from the render thread.
    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            pump_events();
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
    SoftwareRenderer renderer_;
    SDL_Window* window_ = nullptr;

    void pump_events() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                input_queue_.push(WindowClose{});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer_.resize(ev.window.data1, ev.window.data2);
                input_queue_.push(WindowResize{ev.window.data1, ev.window.data2});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                input_queue_.push(MouseMove{{ev.motion.x, ev.motion.y}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                input_queue_.push(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, true});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                input_queue_.push(MouseButton{
                    {ev.button.x, ev.button.y}, ev.button.button, false});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                input_queue_.push(MouseScroll{
                    {ev.wheel.mouse_x, ev.wheel.mouse_y}, ev.wheel.x, ev.wheel.y});
                break;
            default:
                break;
            }
        }
    }

    void present() {
        SDL_Surface* surface = SDL_GetWindowSurface(window_);
        if (!surface) return;

        auto& buf = renderer_.buffer();
        if (surface->w != buf.width() || surface->h != buf.height()) {
            renderer_.resize(surface->w, surface->h);
            return; // skip this frame, render at new size next frame
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
