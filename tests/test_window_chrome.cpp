#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/window_chrome.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using HZ = prism::WindowChrome::HitZone;

TEST_CASE("WindowChrome::cursor_for maps straight edges to axis-aligned resize shapes") {
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeN) == prism::CursorShape::ResizeNS);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeS) == prism::CursorShape::ResizeNS);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeE) == prism::CursorShape::ResizeEW);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeW) == prism::CursorShape::ResizeEW);
}

TEST_CASE("WindowChrome::cursor_for maps corners to the matching diagonal resize shape") {
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeNW) == prism::CursorShape::ResizeNWSE);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeSE) == prism::CursorShape::ResizeNWSE);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeNE) == prism::CursorShape::ResizeNESW);
    CHECK(prism::WindowChrome::cursor_for(HZ::ResizeSW) == prism::CursorShape::ResizeNESW);
}

TEST_CASE("WindowChrome::cursor_for is Default for non-resize zones") {
    CHECK(prism::WindowChrome::cursor_for(HZ::Client) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::TitleBar) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Close) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Minimize) == prism::CursorShape::Default);
    CHECK(prism::WindowChrome::cursor_for(HZ::Maximize) == prism::CursorShape::Default);
}

// Regression tests: dragging the West/North edges (and their diagonals) must
// keep the OPPOSITE edge fixed on screen, like every real window manager --
// growing/shrinking width from the left border should move the left border
// and leave the right border in place. SdlWindow::update_resize() used to
// call only SDL_SetWindowSize() with no compensating SDL_SetWindowPosition(),
// so SDL's "top-left stays put on resize" default made the RIGHT border move
// while the LEFT border (and the window's on-screen position) never did --
// exactly backwards from a left-edge drag.

TEST_CASE("resize_from_drag on the East edge grows width and leaves position untouched") {
    auto r = prism::WindowChrome::resize_from_drag(HZ::ResizeE, 400, 300, 100, 50, 30, 0);
    CHECK(r.w == 430);
    CHECK(r.h == 300);
    CHECK(r.x == 100);
    CHECK(r.y == 50);
}

TEST_CASE("resize_from_drag on the West edge keeps the right edge fixed") {
    auto r = prism::WindowChrome::resize_from_drag(HZ::ResizeW, 400, 300, 100, 50, -30, 0);
    int right_edge_before = 100 + 400;
    CHECK(r.w == 430);
    CHECK(r.x == 100 - 30);
    CHECK(r.x + r.w == right_edge_before);
    CHECK(r.y == 50);
}

TEST_CASE("resize_from_drag on the North edge keeps the bottom edge fixed") {
    auto r = prism::WindowChrome::resize_from_drag(HZ::ResizeN, 400, 300, 100, 50, 0, -20);
    int bottom_edge_before = 50 + 300;
    CHECK(r.h == 320);
    CHECK(r.y == 50 - 20);
    CHECK(r.y + r.h == bottom_edge_before);
    CHECK(r.x == 100);
}

TEST_CASE("resize_from_drag on a diagonal (NW) keeps the opposite corner fixed") {
    auto r = prism::WindowChrome::resize_from_drag(HZ::ResizeNW, 400, 300, 100, 50, -30, -20);
    CHECK(r.w == 430);
    CHECK(r.h == 320);
    CHECK(r.x + r.w == 100 + 400);
    CHECK(r.y + r.h == 50 + 300);
}

TEST_CASE("resize_from_drag still keeps the right edge fixed when the West drag clamps at min width") {
    // Drag far past the minimum width -- the position compensation must be
    // derived from the CLAMPED width, not the raw unclamped delta, or the
    // window keeps sliding left even after it stops shrinking.
    auto r = prism::WindowChrome::resize_from_drag(HZ::ResizeW, 400, 300, 100, 50, 10000, 0);
    CHECK(r.w == prism::WindowChrome::min_width);
    CHECK(r.x + r.w == 100 + 400);
}
