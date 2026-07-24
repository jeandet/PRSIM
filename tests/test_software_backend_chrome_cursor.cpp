#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <atomic>
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
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
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

} // namespace

TEST_CASE("SoftwareBackend sets a resize cursor when the mouse hovers a custom-chrome window edge") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread runner([&] { backend.run([](const prism::WindowEvent&) {}); });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 150.f); // left edge, well below the title bar
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeEW));

    backend.quit();
    runner.join();
}

TEST_CASE("SoftwareBackend sets a diagonal resize cursor when the mouse hovers a corner") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread runner([&] { backend.run([](const prism::WindowEvent&) {}); });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 2.f); // top-left corner
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeNWSE));

    backend.quit();
    runner.join();
}

TEST_CASE("SoftwareBackend forwards MouseMove and leaves the cursor alone when the mouse is over client content") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::atomic<int> mouse_move_count{0};
    std::thread runner([&] {
        backend.run([&](const prism::WindowEvent& we) {
            if (std::holds_alternative<prism::MouseMove>(we.event))
                mouse_move_count.fetch_add(1, std::memory_order_release);
        });
    });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    push_motion(window_id, 2.f, 150.f); // left edge — chrome-owned, not forwarded
    CHECK(wait_for_cursor(sdl_win, prism::CursorShape::ResizeEW));

    push_motion(window_id, 150.f, 150.f); // client area — forwarded, cursor untouched by chrome
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (mouse_move_count.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    CHECK(mouse_move_count.load(std::memory_order_acquire) >= 1);
    CHECK(sdl_win.cursor() == prism::CursorShape::ResizeEW); // chrome layer never touched it back

    backend.quit();
    runner.join();
}

TEST_CASE("SoftwareBackend coalesces a burst of queued motion events into far fewer dispatches") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::atomic<int> mouse_move_count{0};
    std::thread runner([&] {
        backend.run([&](const prism::WindowEvent& we) {
            if (std::holds_alternative<prism::MouseMove>(we.event))
                mouse_move_count.fetch_add(1, std::memory_order_release);
        });
    });
    backend.wait_ready();
    auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

    // All client-area (y=150, well below the title bar) so every one of these reaches the
    // "forward as MouseMove" path, not the chrome/resize early-outs -- same zone as the test
    // above. Pushed back-to-back from this thread, far faster than the backend thread can wake
    // from SDL_WaitEvent and drain them one at a time, so most queue up before draining starts.
    constexpr int burst = 200;
    for (int i = 0; i < burst; ++i)
        push_motion(window_id, 100.f + static_cast<float>(i), 150.f);

    // Wait for the dispatched count to stop changing (the whole burst has been drained and
    // processed) rather than guessing a fixed sleep duration.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    int last = -1, stable_ticks = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        int now = mouse_move_count.load(std::memory_order_acquire);
        if (now == last) {
            if (++stable_ticks >= 5) break;
        } else {
            stable_ticks = 0;
            last = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    int dispatched = mouse_move_count.load(std::memory_order_acquire);
    CHECK(dispatched >= 1);
    // Coalescing should collapse this burst to a small handful of dispatches, not merely
    // "fewer than the absolute max by chance" -- a loose `< burst` bound passed even against
    // the pre-fix code (194/200 got through with zero coalescing), so it proved nothing.
    CHECK(dispatched < burst / 4);

    backend.quit();
    runner.join();
}
