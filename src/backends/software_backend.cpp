#include <prism/backends/software_backend.hpp>
#include <prism/ui/window_chrome.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>

namespace prism::backends {
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using namespace prism::app;

const char* SoftwareBackend::resolve_font_path(const RenderConfig& cfg) {
    if (cfg.font_path) return cfg.font_path;
#ifdef PRISM_FONT_PATH
    {
        // Build-machine absolute path is valid in builddir but not in installed wheels.
        // Check existence first; fall back to installed datadir.
        std::error_code ec;
        if (std::filesystem::exists(PRISM_FONT_PATH, ec)) return PRISM_FONT_PATH;
    }
#endif
#ifdef PRISM_FONT_INSTALL_PATH
    {
        std::error_code ec;
        if (std::filesystem::exists(PRISM_FONT_INSTALL_PATH, ec)) return PRISM_FONT_INSTALL_PATH;
        // Resolve relative to executable prefix, not CWD (wheel installs).
        // PRISM_FONT_INSTALL_PATH is datadir-relative (e.g. share/prism/fonts/...),
        // try <exe_dir>/../share/... and <exe_dir>/share/...
        try {
            auto exe = std::filesystem::read_symlink("/proc/self/exe");
            auto exe_dir = exe.parent_path();
            for (auto cand : {exe_dir / ("../" PRISM_FONT_INSTALL_PATH), exe_dir / PRISM_FONT_INSTALL_PATH}) {
                std::error_code ec2;
                auto norm = cand.lexically_normal();
                if (std::filesystem::exists(norm, ec2)) {
                    static std::string cached;
                    cached = norm.string();
                    return cached.c_str();
                }
            }
        } catch (...) {}
    }
#endif
    return nullptr;
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
    // Pre-run only; after run() use request_window() (queued, thread-safe).
    assert(!ready_.load(std::memory_order_acquire) && "create_window after run(): use request_window()");
    std::lock_guard<std::mutex> lk(windows_mutex_);
    auto id = ++next_id_;
    auto window = std::make_unique<SdlWindow>(id, cfg);
    auto& ref = *window;
    windows_.emplace(id, std::move(window));
    snapshots_[id]; // default-construct snapshot slot
    return ref;
}

Window* SoftwareBackend::request_window(WindowConfig cfg) {
    auto req = std::make_shared<PendingWindowRequest>();
    req->cfg = cfg;
    window_requests_.push(req);
    wake();

    std::unique_lock lock(req->m);
    req->cv.wait_for(lock, std::chrono::seconds(2), [&] { return req->done; });
    return req->done ? req->result : nullptr;
}

void SoftwareBackend::drain_window_requests() {
    while (auto req_opt = window_requests_.pop()) {
        auto req = *req_opt;
        Window* result = nullptr;
        if (running_.load(std::memory_order_relaxed)) {
            WindowId id;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                id = ++next_id_;
            }
            auto win = std::make_unique<SdlWindow>(id, req->cfg);
            win->ensure_created();
            SDL_StartTextInput(win->sdl_window());
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                auto [it, _] = windows_.emplace(id, std::move(win));
                snapshots_[id];
                result = it->second.get();
            }
        }
        {
            std::lock_guard lock(req->m);
            req->result = result;
            req->done = true;
        }
        req->cv.notify_one();
    }
}

void SoftwareBackend::close_window(WindowId id) {
    close_requests_.push(id);
    wake();
}

void SoftwareBackend::drain_close_requests() {
    while (auto id_opt = close_requests_.pop()) {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        windows_.erase(*id_opt);
        snapshots_.erase(*id_opt);
    }
}

WindowId SoftwareBackend::sdl_id_to_prism_id(uint32_t sdl_window_id) const {
    std::lock_guard<std::mutex> lk(windows_mutex_);
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
    {
        std::lock_guard<std::mutex> lk(windows_mutex_);
        for (auto& [id, win] : windows_) {
            win->ensure_created();
            SDL_StartTextInput(win->sdl_window());
        }
    }

    ready_.store(true, std::memory_order_release);
    ready_.notify_one();

    // A fast mouse can queue many SDL_EVENT_MOUSE_MOTION events between two SDL_WaitEvent
    // wakes; each dispatched MouseMove ultimately triggers a full-tree relayout + rebuild
    // downstream (see model_app.hpp), so processing every raw motion sample individually
    // makes rendering fall further and further behind the actual mouse position under a
    // fast/continuous drag. Only the most recent queued position actually matters, so
    // consecutive motion events for the same window are buffered and only the last one is
    // ever dispatched -- flushed whenever a different kind of event (or a motion for a
    // different window) needs to be delivered first, preserving relative event order.
    std::optional<WindowEvent> pending_motion;
    auto flush_pending_motion = [&] {
        if (pending_motion) {
            event_cb(*pending_motion);
            pending_motion.reset();
        }
    };

    // Caller must hold windows_mutex_.
    auto find_window = [&](WindowId id) -> SdlWindow* {
        auto it = windows_.find(id);
        return it != windows_.end() ? it->second.get() : nullptr;
    };
    // Window-space Y -> client-space Y (custom chrome draws its own title bar
    // above the client area). Caller must hold windows_mutex_.
    auto client_y = [&](WindowId id, float y) {
        if (auto* win = find_window(id);
            win && win->decoration_mode() == DecorationMode::Custom)
            y -= WindowChrome::title_bar_h.raw();
        return y;
    };

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
            // During a press-drag, route events to the window that received the press
            if (wid == 0 && pressed_window_ != 0)
                wid = pressed_window_;
            // For single-window case, fall back to first window
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                if (wid == 0 && windows_.size() == 1)
                    wid = windows_.begin()->first;
            }

            if (ev.type != SDL_EVENT_MOUSE_MOTION || (pending_motion && pending_motion->window != wid))
                flush_pending_motion();

            switch (ev.type) {
            case SDL_EVENT_QUIT:
                event_cb(WindowEvent{wid, WindowClose{}});
                running_.store(false, std::memory_order_relaxed);
                break;
            case SDL_EVENT_WINDOW_RESIZED: {
                int rh = ev.window.data2;
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (auto* win = find_window(wid);
                        win && win->decoration_mode() == DecorationMode::Custom)
                        rh -= static_cast<int>(WindowChrome::title_bar_h.raw());
                }
                event_cb(WindowEvent{wid, WindowResize{ev.window.data1, rh}});
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    auto* win = find_window(wid);
                    if (win && win->in_resize()) {
                        win->update_resize(
                            static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y));
                        break;
                    }
                    if (win && win->decoration_mode() == DecorationMode::Custom) {
                        int ww, wh;
                        SDL_GetWindowSize(win->sdl_window(), &ww, &wh);
                        auto zone = WindowChrome::hit_test(
                            static_cast<int>(ev.motion.x), static_cast<int>(ev.motion.y), ww, wh);
                        if (zone != WindowChrome::HitZone::Client) {
                            win->set_cursor(WindowChrome::cursor_for(zone));
                            break;
                        }
                    }
                    pending_motion = WindowEvent{wid, MouseMove{
                        Point{X{ev.motion.x}, Y{client_y(wid, ev.motion.y)}}}};
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                SDL_CaptureMouse(true);
                pressed_window_ = wid;
                std::lock_guard<std::mutex> lk(windows_mutex_);
                auto* win = find_window(wid);
                if (win && win->decoration_mode() == DecorationMode::Custom) {
                    int ww, wh;
                    SDL_GetWindowSize(win->sdl_window(), &ww, &wh);
                    auto zone = WindowChrome::hit_test(
                        static_cast<int>(ev.button.x), static_cast<int>(ev.button.y), ww, wh);
                    if (zone == WindowChrome::HitZone::Close) {
                        event_cb(WindowEvent{wid, WindowClose{}});
                        break;
                    }
                    if (zone == WindowChrome::HitZone::Minimize) {
                        win->minimize();
                        break;
                    }
                    if (zone == WindowChrome::HitZone::Maximize) {
                        if (win->is_fullscreen())
                            win->restore();
                        else
                            win->maximize();
                        break;
                    }
                    if (zone != WindowChrome::HitZone::Client) {
                        win->begin_resize(
                            static_cast<int>(ev.button.x), static_cast<int>(ev.button.y));
                        break;
                    }
                }
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{client_y(wid, ev.button.y)}},
                    ev.button.button, true}});
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                SDL_CaptureMouse(false);
                pressed_window_ = 0;
                std::lock_guard<std::mutex> lk(windows_mutex_);
                auto* win = find_window(wid);
                if (win && win->in_resize()) {
                    win->end_resize();
                    break;
                }
                event_cb(WindowEvent{wid, MouseButton{
                    Point{X{ev.button.x}, Y{client_y(wid, ev.button.y)}},
                    ev.button.button, false}});
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                event_cb(WindowEvent{wid, MouseScroll{
                    Point{X{ev.wheel.mouse_x}, Y{client_y(wid, ev.wheel.mouse_y)}},
                    DX{ev.wheel.x}, DY{ev.wheel.y}}});
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                event_cb(WindowEvent{wid, KeyPress{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_KEY_UP:
                event_cb(WindowEvent{wid, KeyRelease{static_cast<int32_t>(ev.key.key), ev.key.mod}});
                break;
            case SDL_EVENT_TEXT_INPUT:
                event_cb(WindowEvent{wid, TextInput{ev.text.text}});
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                event_cb(WindowEvent{wid, WindowClose{}});
                break;
            case SDL_EVENT_USER:
                drain_window_requests();
                drain_close_requests();
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        flush_pending_motion(); // the queue's last event is very often a motion event itself

        if (!running_.load(std::memory_order_relaxed)) break;

        // Render any pending snapshots — collect under lock, render outside
        {
            std::vector<std::pair<WindowId, std::shared_ptr<const SceneSnapshot>>> to_render;
            {
                std::lock_guard<std::mutex> lk(windows_mutex_);
                for (auto& [id, snap_slot] : snapshots_) {
                    auto snap = snap_slot.snapshot.load(std::memory_order_acquire);
                    if (snap) to_render.emplace_back(id, std::move(snap));
                }
            }
            for (auto& [id, snap] : to_render) {
                SdlWindow* win = nullptr;
                {
                    std::lock_guard<std::mutex> lk(windows_mutex_);
                    if (auto it = windows_.find(id); it != windows_.end()) win = it->second.get();
                }
                if (win) win->render_snapshot(*snap, font_);
            }
        }
    }
    drain_window_requests();
    drain_close_requests();
}

void SoftwareBackend::submit(WindowId window, std::shared_ptr<const SceneSnapshot> snap) {
    std::lock_guard<std::mutex> lk(windows_mutex_);
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

} // namespace prism::backends
