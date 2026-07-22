#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_window.hpp>

#include <SDL3/SDL.h>

TEST_CASE("SdlWindow::set_cursor applies every CursorShape without crashing") {
    SDL_Init(SDL_INIT_VIDEO);
    prism::backends::SdlWindow win(1, {.title = "Test", .width = 100, .height = 100});
    win.ensure_created();

    win.set_cursor(prism::ui::CursorShape::Default);
    win.set_cursor(prism::ui::CursorShape::Text);
    win.set_cursor(prism::ui::CursorShape::ResizeNS);
    win.set_cursor(prism::ui::CursorShape::ResizeEW);
    win.set_cursor(prism::ui::CursorShape::ResizeNESW);
    win.set_cursor(prism::ui::CursorShape::ResizeNWSE);

    CHECK(true); // SDL exposes no queryable "current cursor" — absence of a crash is the assertion,
                 // matching the existing precedent in tests/test_app.cpp for real-SDL side effects.
    SDL_Quit();
}
