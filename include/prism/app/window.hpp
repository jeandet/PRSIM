#pragma once

#include <prism/core/input_event.hpp>

#include <cstdint>
#include <string_view>
#include <utility>

namespace prism {

using WindowId = uint32_t;

enum class DecorationMode { Native, Custom, None };

struct WindowConfig {
    const char* title      = "PRISM";
    int         width      = 800;
    int         height     = 600;
    bool        resizable  = true;
    bool        fullscreen = false;
    DecorationMode decoration = DecorationMode::Native;
};

struct RenderConfig {
    const char* font_path = nullptr;
};

class Window {
public:
    virtual ~Window() = default;

    virtual WindowId id() const = 0;

    virtual void set_title(std::string_view title) = 0;
    virtual void set_size(int w, int h) = 0;
    virtual std::pair<int, int> size() const = 0;
    virtual void set_position(int x, int y) = 0;
    virtual std::pair<int, int> position() const = 0;

    virtual void set_decoration_mode(DecorationMode mode) = 0;
    virtual DecorationMode decoration_mode() const = 0;

    virtual void set_resizable(bool r) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_fullscreen(bool f) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

    virtual void close() = 0;
};

struct WindowEvent {
    WindowId window;
    InputEvent event;
};

} // namespace prism
