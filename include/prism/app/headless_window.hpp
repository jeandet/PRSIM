#pragma once

#include <prism/app/window.hpp>

#include <string>

namespace prism::app {

class HeadlessWindow final : public Window {
public:
    HeadlessWindow(WindowId id, WindowConfig cfg)
        : id_(id), title_(cfg.title), w_(cfg.width), h_(cfg.height),
          resizable_(cfg.resizable), fullscreen_(cfg.fullscreen),
          decoration_(cfg.decoration) {}

    WindowId id() const override { return id_; }

    void set_title(std::string_view title) override { title_ = title; }
    void set_size(int w, int h) override { w_ = w; h_ = h; }
    std::pair<int, int> size() const override { return {w_, h_}; }
    void set_position(int x, int y) override { x_ = x; y_ = y; }
    std::pair<int, int> position() const override { return {x_, y_}; }

    void set_decoration_mode(DecorationMode m) override { decoration_ = m; }
    DecorationMode decoration_mode() const override { return decoration_; }

    void set_resizable(bool r) override { resizable_ = r; }
    bool is_resizable() const override { return resizable_; }
    void set_fullscreen(bool f) override { fullscreen_ = f; }
    bool is_fullscreen() const override { return fullscreen_; }

    void minimize() override {}
    void maximize() override {}
    void restore() override {}
    void show() override {}
    void hide() override {}
    void close() override {}

private:
    WindowId id_;
    std::string title_;
    int w_, h_;
    int x_ = 0, y_ = 0;
    bool resizable_;
    bool fullscreen_;
    DecorationMode decoration_;
};

} // namespace prism::app
