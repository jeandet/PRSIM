#include <prism/backends/software_backend.hpp>

#include <cmath>

namespace prism {

const char* SoftwareBackend::resolve_font_path(const RenderConfig& cfg) {
    if (cfg.font_path) return cfg.font_path;
#ifdef PRISM_FONT_PATH
    return PRISM_FONT_PATH;
#else
    return nullptr;
#endif
}

SoftwareBackend::SoftwareBackend(RenderConfig cfg)
    : render_config_(cfg)
{}

SoftwareBackend::~SoftwareBackend() {
    if (font_) TTF_CloseFont(font_);
    TTF_Quit();
    windows_.clear();
    SDL_Quit();
}

Window& SoftwareBackend::create_window(WindowConfig cfg) {
    auto id = ++next_id_;
    auto window = std::make_unique<SdlWindow>(id, cfg);
    auto& ref = *window;
    windows_.emplace(id, std::move(window));
    snapshots_[id]; // default-construct snapshot slot
    return ref;
}

WindowId SoftwareBackend::sdl_id_to_prism_id(uint32_t sdl_window_id) const {
    for (auto& [id, win] : windows_) {
        if (SDL_GetWindowID(win->sdl_window()) == sdl_window_id)
            return id;
    }
    return 0;
}

void SoftwareBackend::run(std::function<void(const WindowEvent&)> event_cb) {
    SDL_Init(SDL_INIT_VIDEO);

    TTF_Init();
    const char* fpath = resolve_font_path(render_config_);
    if (fpath) {
        font_ = TTF_OpenFont(fpath, 16.0f);
    }

    // Create SDL windows (deferred from create_window()) and start text input
    for (auto& [id, win] : windows_) {
        win->ensure_created();
        SDL_StartTextInput(win->sdl_window());
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    while (running_.load(std::memory_order_relaxed)) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) continue;

        do {
            // Resolve which prism window this event belongs to
            WindowId wid = 0;
            if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) {
                wid = sdl_id_to_prism_id(ev.window.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                wid = sdl_id_to_prism_id(ev.motion.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                wid = sdl_id_to_prism_id(ev.button.windowID);
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                wid = sdl_id_to_prism_id(ev.wheel.windowID);
            } else if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                wid = sdl_id_to_prism_id(ev.key.windowID);
            } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                wid = sdl_id_to_prism_id(ev.text.windowID);
            }
            // For single-window case, fall back to first window
            if (wid == 0 && windows_.size() == 1)
                wid = windows_.begin()->first;

            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowEvent{wid, WindowClose{}});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                event_cb(WindowEvent{wid, WindowResize{ev.window.data1, ev.window.data2}});
                break;
            case SDL_EVENT_MOUSE_MOTION:
                event_cb(WindowEvent{wid, MouseMove{Point{X{ev.motion.x}, Y{ev.motion.y}}}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{ev.button.y}}, ev.button.button, true}});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{ev.button.y}}, ev.button.button, false}});
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                event_cb(WindowEvent{wid, MouseScroll{
                    Point{X{ev.wheel.mouse_x}, Y{ev.wheel.mouse_y}}, DX{ev.wheel.x}, DY{ev.wheel.y}}});
                break;
            case SDL_EVENT_KEY_DOWN:
                event_cb(WindowEvent{wid, KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_KEY_UP:
                event_cb(WindowEvent{wid, KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_TEXT_INPUT:
                event_cb(WindowEvent{wid, TextInput{ev.text.text}});
                break;
            case SDL_EVENT_USER:
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (!running_.load(std::memory_order_relaxed)) break;

        // Render any pending snapshots
        for (auto& [id, snap_slot] : snapshots_) {
            auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
            if (snap) {
                if (auto it = windows_.find(id); it != windows_.end())
                    it->second->render_snapshot(*snap, font_);
            }
        }
    }
}

void SoftwareBackend::submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) {
    if (auto it = snapshots_.find(window); it != snapshots_.end())
        it->second.snapshot.store(std::move(snap), std::memory_order_release);
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

Backend Backend::software(RenderConfig cfg) {
    return Backend{std::make_unique<SoftwareBackend>(cfg)};
}

} // namespace prism
