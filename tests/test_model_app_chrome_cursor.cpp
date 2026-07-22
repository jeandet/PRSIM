#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <chrono>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

bool wait_for_cursor(prism::backends::SdlWindow& win, prism::CursorShape expected) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (win.cursor() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void push_motion(SDL_WindowID window_id, float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = window_id;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

struct SimpleModel {
    prism::Field<int> a{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(a); }
};

} // namespace

TEST_CASE("model_app's cursor reclaims client content after a chrome-edge excursion") {
    SimpleModel model;
    auto backend = prism::Backend{std::make_unique<prism::backends::SoftwareBackend>(prism::RenderConfig{})};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread app_thread([&] { prism::model_app(backend, window, model); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (sdl_win.sdl_window() == nullptr && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(sdl_win.sdl_window() != nullptr);
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 150.f); // left edge -- chrome sets ResizeEW
    REQUIRE(wait_for_cursor(sdl_win, prism::CursorShape::ResizeEW));

    push_motion(window_id, 150.f, 150.f); // client content -- must reclaim Default
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::Default));

    SDL_Event close_ev{};
    close_ev.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close_ev.window.windowID = window_id;
    SDL_PushEvent(&close_ev);

    app_thread.join();
}
