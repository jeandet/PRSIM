#pragma once

#include <prism/core/window.hpp>
#include <prism/core/window_chrome.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/context.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <vector>

namespace prism {

class SdlWindow final : public Window {
public:
    SdlWindow(WindowId id, WindowConfig cfg);
    ~SdlWindow() override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&& other) noexcept;
    SdlWindow& operator=(SdlWindow&& other) noexcept;

    WindowId id() const override { return id_; }

    void set_title(std::string_view title) override;
    void set_size(int w, int h) override;
    std::pair<int, int> size() const override;
    void set_position(int x, int y) override;
    std::pair<int, int> position() const override;

    void set_decoration_mode(DecorationMode mode) override;
    DecorationMode decoration_mode() const override { return decoration_; }

    void set_resizable(bool r) override;
    bool is_resizable() const override;
    void set_fullscreen(bool f) override;
    bool is_fullscreen() const override;
    void minimize() override;
    void maximize() override;
    void restore() override;
    void show() override;
    void hide() override;
    void close() override;

    // Backend-internal access
    SDL_Window* sdl_window() { return sdl_window_; }
    SDL_Renderer* renderer() { return renderer_; }
    void ensure_created();

    void render_snapshot(const SceneSnapshot& snap, TTF_Font* font, const Theme& theme = Theme{});

    // Manual resize tracking for custom chrome
    bool begin_resize(int mouse_x, int mouse_y);
    bool update_resize(int mouse_x, int mouse_y);
    void end_resize();
    bool in_resize() const { return resize_zone_ != WindowChrome::HitZone::Client; }

private:
    WindowId id_;
    SDL_Window* sdl_window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    DecorationMode decoration_;
    std::string title_;
    WindowConfig config_;
    std::vector<SDL_Rect> clip_stack_;

    // Manual resize tracking
    WindowChrome::HitZone resize_zone_ = WindowChrome::HitZone::Client;
    float resize_start_x_ = 0, resize_start_y_ = 0;
    int resize_start_w_ = 0, resize_start_h_ = 0;

    void create_sdl_window();
    void destroy_sdl_window();

    void render_draw_list(const DrawList& dl, TTF_Font* font);
    void render_cmd(const FilledRect& cmd);
    void render_cmd(const RectOutline& cmd);
    void render_cmd(const TextCmd& cmd, TTF_Font* font);
    void render_cmd(const ClipPush& cmd);
    void render_cmd(const ClipPop& cmd);
    void render_cmd(const RoundedRect& cmd);
    void render_cmd(const Line& cmd);
    void render_cmd(const Polyline& cmd);
    void render_cmd(const Circle& cmd);
};

} // namespace prism
