#include <prism/backends/sdl_window.hpp>

#include <cmath>

namespace prism {

namespace {

SDL_FRect to_sdl(Rect r) {
    return {r.origin.x.raw(), r.origin.y.raw(), r.extent.w.raw(), r.extent.h.raw()};
}

SDL_Color to_sdl(Color c) {
    return {c.r, c.g, c.b, c.a};
}

} // namespace

SdlWindow::SdlWindow(WindowId id, WindowConfig cfg)
    : id_(id), decoration_(cfg.decoration), title_(cfg.title), config_(cfg)
{
    // SDL window creation is deferred — call ensure_created() after SDL_Init
}

SdlWindow::~SdlWindow() {
    destroy_sdl_window();
}

SdlWindow::SdlWindow(SdlWindow&& other) noexcept
    : id_(other.id_), sdl_window_(other.sdl_window_), renderer_(other.renderer_),
      decoration_(other.decoration_), title_(std::move(other.title_)),
      config_(other.config_), clip_stack_(std::move(other.clip_stack_))
{
    other.sdl_window_ = nullptr;
    other.renderer_ = nullptr;
}

SdlWindow& SdlWindow::operator=(SdlWindow&& other) noexcept {
    if (this != &other) {
        destroy_sdl_window();
        id_ = other.id_;
        sdl_window_ = other.sdl_window_;
        renderer_ = other.renderer_;
        decoration_ = other.decoration_;
        title_ = std::move(other.title_);
        config_ = other.config_;
        clip_stack_ = std::move(other.clip_stack_);
        other.sdl_window_ = nullptr;
        other.renderer_ = nullptr;
    }
    return *this;
}

void SdlWindow::ensure_created() {
    if (!sdl_window_) create_sdl_window();
}

void SdlWindow::create_sdl_window() {
    uint64_t flags = 0;
    if (config_.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if (decoration_ == DecorationMode::Custom || decoration_ == DecorationMode::None)
        flags |= SDL_WINDOW_BORDERLESS;

    sdl_window_ = SDL_CreateWindow(title_.c_str(), config_.width, config_.height, flags);
    renderer_ = SDL_CreateRenderer(sdl_window_, nullptr);
}

void SdlWindow::destroy_sdl_window() {
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (sdl_window_) { SDL_DestroyWindow(sdl_window_); sdl_window_ = nullptr; }
}

void SdlWindow::set_title(std::string_view title) {
    title_ = title;
    if (sdl_window_) SDL_SetWindowTitle(sdl_window_, title_.c_str());
}

void SdlWindow::set_size(int w, int h) {
    config_.width = w;
    config_.height = h;
    if (sdl_window_) SDL_SetWindowSize(sdl_window_, w, h);
}

std::pair<int, int> SdlWindow::size() const {
    if (sdl_window_) {
        int w, h;
        SDL_GetWindowSize(sdl_window_, &w, &h);
        return {w, h};
    }
    return {config_.width, config_.height};
}

void SdlWindow::set_position(int x, int y) {
    if (sdl_window_) SDL_SetWindowPosition(sdl_window_, x, y);
}

std::pair<int, int> SdlWindow::position() const {
    if (sdl_window_) {
        int x, y;
        SDL_GetWindowPosition(sdl_window_, &x, &y);
        return {x, y};
    }
    return {0, 0};
}

void SdlWindow::set_decoration_mode(DecorationMode mode) {
    if (decoration_ == mode) return;
    decoration_ = mode;
    // Recreate window with new flags
    auto [w, h] = size();
    config_.width = w;
    config_.height = h;
    destroy_sdl_window();
    create_sdl_window();
}

void SdlWindow::set_resizable(bool r) {
    config_.resizable = r;
    // SDL3 doesn't have a runtime toggle — recreate
    if (sdl_window_) {
        auto [w, h] = size();
        config_.width = w;
        config_.height = h;
        destroy_sdl_window();
        create_sdl_window();
    }
}

bool SdlWindow::is_resizable() const { return config_.resizable; }

void SdlWindow::set_fullscreen(bool f) {
    config_.fullscreen = f;
    if (sdl_window_) SDL_SetWindowFullscreen(sdl_window_, f);
}

bool SdlWindow::is_fullscreen() const { return config_.fullscreen; }

void SdlWindow::minimize()  { if (sdl_window_) SDL_MinimizeWindow(sdl_window_); }
void SdlWindow::maximize()  { if (sdl_window_) SDL_MaximizeWindow(sdl_window_); }
void SdlWindow::restore()   { if (sdl_window_) SDL_RestoreWindow(sdl_window_); }
void SdlWindow::show()      { if (sdl_window_) SDL_ShowWindow(sdl_window_); }
void SdlWindow::hide()      { if (sdl_window_) SDL_HideWindow(sdl_window_); }

void SdlWindow::close() {
    destroy_sdl_window();
}

void SdlWindow::render_snapshot(const SceneSnapshot& snap, TTF_Font* font) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
    SDL_RenderClear(renderer_);
    for (uint16_t idx : snap.z_order) {
        render_draw_list(snap.draw_lists[idx], font);
    }
    if (!snap.overlay.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
        render_draw_list(snap.overlay, font);
    }
    SDL_RenderPresent(renderer_);
}

void SdlWindow::render_draw_list(const DrawList& dl, TTF_Font* font) {
    for (const auto& cmd : dl.commands) {
        std::visit([this, font](const auto& c) {
            if constexpr (std::is_same_v<std::decay_t<decltype(c)>, TextCmd>)
                render_cmd(c, font);
            else
                render_cmd(c);
        }, cmd);
    }
}

void SdlWindow::render_cmd(const FilledRect& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderFillRect(renderer_, &r);
}

void SdlWindow::render_cmd(const RectOutline& cmd) {
    SDL_SetRenderDrawColor(renderer_, cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a);
    SDL_FRect r = to_sdl(cmd.rect);
    SDL_RenderRect(renderer_, &r);
}

void SdlWindow::render_cmd(const TextCmd& cmd, TTF_Font* font) {
    if (!font || cmd.text.empty()) return;

    float current_size = TTF_GetFontSize(font);
    if (std::abs(current_size - cmd.size) > 0.5f) {
        TTF_SetFontSize(font, cmd.size);
    }

    SDL_Color color = to_sdl(cmd.color);
    SDL_Surface* surface = TTF_RenderText_Blended(font, cmd.text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (texture) {
        SDL_FRect dst = {cmd.origin.x.raw(), cmd.origin.y.raw(),
                         static_cast<float>(surface->w),
                         static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

void SdlWindow::render_cmd(const ClipPush& cmd) {
    SDL_Rect r = {static_cast<int>(cmd.rect.origin.x.raw()), static_cast<int>(cmd.rect.origin.y.raw()),
                  static_cast<int>(cmd.rect.extent.w.raw()), static_cast<int>(cmd.rect.extent.h.raw())};
    clip_stack_.push_back(r);
    SDL_SetRenderClipRect(renderer_, &r);
}

void SdlWindow::render_cmd(const ClipPop&) {
    if (!clip_stack_.empty()) clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        SDL_SetRenderClipRect(renderer_, &clip_stack_.back());
    }
}

} // namespace prism
