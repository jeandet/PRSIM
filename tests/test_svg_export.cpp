#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/svg_export.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


namespace {
prism::Rect R(float x, float y, float w, float h) {
    return {prism::Point{prism::X{x}, prism::Y{y}}, prism::Size{prism::Width{w}, prism::Height{h}}};
}
prism::Point P(float x, float y) {
    return {prism::X{x}, prism::Y{y}};
}
}

TEST_CASE("empty DrawList produces valid svg") {
    prism::DrawList dl;
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("</svg>") != std::string::npos);
}

TEST_CASE("FilledRect") {
    prism::DrawList dl;
    dl.filled_rect(R(10, 20, 100, 50), prism::Color::rgba(255, 0, 0, 128));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("x=\"10\"") != std::string::npos);
    CHECK(svg.find("y=\"20\"") != std::string::npos);
    CHECK(svg.find("width=\"100\"") != std::string::npos);
    CHECK(svg.find("height=\"50\"") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(255,0,0,") != std::string::npos);
}

TEST_CASE("RectOutline") {
    prism::DrawList dl;
    dl.rect_outline(R(5, 5, 80, 40), prism::Color::rgba(0, 255, 0), 2.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("stroke=\"rgba(0,255,0,1)\"") != std::string::npos);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2\"") != std::string::npos);
}

TEST_CASE("RoundedRect filled") {
    prism::DrawList dl;
    dl.rounded_rect(R(0, 0, 60, 30), prism::Color::rgba(0, 0, 255), 5.0f, 0.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("rx=\"5\"") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(0,0,255,1)\"") != std::string::npos);
}

TEST_CASE("RoundedRect stroke") {
    prism::DrawList dl;
    dl.rounded_rect(R(0, 0, 60, 30), prism::Color::rgba(0, 0, 255), 5.0f, 3.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("rx=\"5\"") != std::string::npos);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"3\"") != std::string::npos);
}

TEST_CASE("TextCmd") {
    prism::DrawList dl;
    dl.text("hello", P(50, 25), 14.0f, prism::Color::rgba(0, 0, 0));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<text") != std::string::npos);
    CHECK(svg.find("x=\"50\"") != std::string::npos);
    CHECK(svg.find("y=\"25\"") != std::string::npos);
    CHECK(svg.find("font-size=\"14\"") != std::string::npos);
    CHECK(svg.find(">hello</text>") != std::string::npos);
}

TEST_CASE("Line") {
    prism::DrawList dl;
    dl.line(P(0, 0), P(100, 100), prism::Color::rgba(255, 0, 0), 2.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<line") != std::string::npos);
    CHECK(svg.find("x1=\"0\"") != std::string::npos);
    CHECK(svg.find("y1=\"0\"") != std::string::npos);
    CHECK(svg.find("x2=\"100\"") != std::string::npos);
    CHECK(svg.find("y2=\"100\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2\"") != std::string::npos);
}

TEST_CASE("Polyline") {
    prism::DrawList dl;
    dl.polyline({P(0, 0), P(10, 20), P(30, 10)}, prism::Color::rgba(0, 128, 0), 1.5f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<polyline") != std::string::npos);
    CHECK(svg.find("points=\"0,0 10,20 30,10\"") != std::string::npos);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"1.5\"") != std::string::npos);
}

TEST_CASE("Circle filled") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.0f, prism::Color::rgba(255, 128, 0), 0.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<circle") != std::string::npos);
    CHECK(svg.find("cx=\"50\"") != std::string::npos);
    CHECK(svg.find("cy=\"50\"") != std::string::npos);
    CHECK(svg.find("r=\"25\"") != std::string::npos);
    CHECK(svg.find("fill=\"rgba(255,128,0,1)\"") != std::string::npos);
}

TEST_CASE("Circle stroke") {
    prism::DrawList dl;
    dl.circle(P(50, 50), 25.0f, prism::Color::rgba(255, 128, 0), 2.0f);
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<circle") != std::string::npos);
    CHECK(svg.find("fill=\"none\"") != std::string::npos);
    CHECK(svg.find("stroke-width=\"2\"") != std::string::npos);
}

TEST_CASE("ClipPush and ClipPop") {
    prism::DrawList dl;
    dl.clip_push(P(10, 10), prism::Size{prism::Width{80}, prism::Height{60}});
    dl.filled_rect(R(0, 0, 100, 100), prism::Color::rgba(255, 0, 0));
    dl.clip_pop();
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("<clipPath") != std::string::npos);
    CHECK(svg.find("<g clip-path=\"url(#clip-") != std::string::npos);
    CHECK(svg.find("</g>") != std::string::npos);
}

TEST_CASE("Text XML escaping") {
    prism::DrawList dl;
    dl.text("<b>A&B\"C</b>", P(0, 0), 12.0f, prism::Color::rgba(0, 0, 0));
    auto svg = prism::to_svg(dl);
    CHECK(svg.find("&lt;b&gt;A&amp;B&quot;C&lt;/b&gt;") != std::string::npos);
}

TEST_CASE("SceneSnapshot with z_order and overlay") {
    prism::SceneSnapshot snap;
    snap.version = 1;

    prism::DrawList dl0;
    dl0.filled_rect(R(0, 0, 50, 50), prism::Color::rgba(255, 0, 0));
    prism::DrawList dl1;
    dl1.filled_rect(R(50, 50, 50, 50), prism::Color::rgba(0, 0, 255));

    snap.geometry.push_back({0, R(0, 0, 50, 50)});
    snap.geometry.push_back({1, R(50, 50, 50, 50)});
    snap.draw_lists.push_back(std::move(dl0));
    snap.draw_lists.push_back(std::move(dl1));
    snap.z_order = {1, 0};

    snap.overlay.filled_rect(R(25, 25, 20, 20), prism::Color::rgba(0, 255, 0));

    auto svg = prism::to_svg(snap);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("</svg>") != std::string::npos);
    // Both draw lists should appear
    CHECK(svg.find("rgba(255,0,0,1)") != std::string::npos);
    CHECK(svg.find("rgba(0,0,255,1)") != std::string::npos);
    // Overlay should appear
    CHECK(svg.find("rgba(0,255,0,1)") != std::string::npos);
}
