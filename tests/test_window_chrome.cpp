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
