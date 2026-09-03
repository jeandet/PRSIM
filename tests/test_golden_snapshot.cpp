// Golden snapshot tests (doc/review-2026-08-28.md, step 3): deterministic regression coverage
// for the whole event -> WidgetTree -> layout -> SceneSnapshot path, using TestBackend's
// scripted event replay to drive model_app() and TestBackend::submitted() to capture every
// snapshot that path actually produced. Flagged in the review response as "cheap because the
// harness already exists" -- this file supplies the missing piece, a deterministic text
// serialization of a SceneSnapshot's rendered content plus fixed expected output to diff
// against. A structural change in build_snapshot()/layout_flatten() that no other test happens
// to cover will show up here as a text diff, not a debugging session.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/model_app.hpp>
#include <prism/app/test_backend.hpp>
#include <prism/core/field.hpp>

#include <fmt/format.h>

#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

std::string color_hex(prism::Color c) {
    return fmt::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a);
}

std::string rect_text(prism::Rect r) {
    return fmt::format("({:.1f},{:.1f},{:.1f},{:.1f})",
        r.origin.x.raw(), r.origin.y.raw(), r.extent.w.raw(), r.extent.h.raw());
}

// One line per draw command, in a fixed field order -- stable across runs given a fixed
// model/window size (no font-metrics or timing dependency at this layer: TextCmd stores the
// logical position/size record() chose, not rendered glyph extents).
std::string cmd_text(const prism::DrawCmd& cmd) {
    return std::visit([](const auto& c) -> std::string {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, prism::FilledRect>)
            return "FilledRect rect=" + rect_text(c.rect) + " color=" + color_hex(c.color);
        else if constexpr (std::is_same_v<T, prism::RectOutline>)
            return fmt::format("RectOutline rect={} color={} thickness={:.1f}",
                rect_text(c.rect), color_hex(c.color), c.thickness);
        else if constexpr (std::is_same_v<T, prism::TextCmd>)
            return fmt::format("Text \"{}\" at=({:.1f},{:.1f}) size={:.1f} color={}",
                c.text, c.origin.x.raw(), c.origin.y.raw(), c.size, color_hex(c.color));
        else if constexpr (std::is_same_v<T, prism::ClipPush>)
            return "ClipPush rect=" + rect_text(c.rect);
        else if constexpr (std::is_same_v<T, prism::ClipPop>)
            return std::string("ClipPop");
        else if constexpr (std::is_same_v<T, prism::RoundedRect>)
            return fmt::format("RoundedRect rect={} color={} radius={:.1f} thickness={:.1f}",
                rect_text(c.rect), color_hex(c.color), c.radius, c.thickness);
        else if constexpr (std::is_same_v<T, prism::Line>)
            return fmt::format("Line from=({:.1f},{:.1f}) to=({:.1f},{:.1f}) color={} thickness={:.1f}",
                c.from.x.raw(), c.from.y.raw(), c.to.x.raw(), c.to.y.raw(),
                color_hex(c.color), c.thickness);
        else if constexpr (std::is_same_v<T, prism::Polyline>)
            return fmt::format("Polyline points={} color={} thickness={:.1f}",
                c.points.size(), color_hex(c.color), c.thickness);
        else if constexpr (std::is_same_v<T, prism::Circle>)
            return fmt::format("Circle center=({:.1f},{:.1f}) radius={:.1f} color={} thickness={:.1f}",
                c.center.x.raw(), c.center.y.raw(), c.radius, color_hex(c.color), c.thickness);
        else if constexpr (std::is_same_v<T, prism::FilledPolygon>)
            return fmt::format("FilledPolygon points={} color={}", c.points.size(), color_hex(c.color));
    }, cmd);
}

std::string golden_text(const prism::SceneSnapshot& snap) {
    std::string out;
    for (auto idx : snap.z_order) {
        auto [id, rect] = snap.geometry[idx];
        out += fmt::format("[widget {}] rect={}\n", id, rect_text(rect));
        for (auto& cmd : snap.draw_lists[idx]->commands)
            out += "    " + cmd_text(cmd) + "\n";
    }
    if (!snap.overlay.empty()) {
        out += "[overlay]\n";
        for (auto& cmd : snap.overlay.commands)
            out += "    " + cmd_text(cmd) + "\n";
    }
    return out;
}

struct GoldenModel {
    prism::Field<int> count{0};
    prism::Field<prism::Checkbox> agree{prism::Checkbox{false, "agree"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, agree);
    }
};

} // namespace

TEST_CASE("golden: initial publish of a small multi-widget model") {
    GoldenModel model;
    std::vector<prism::InputEvent> events; // none -- just the initial publish
    auto backend_ptr = std::make_unique<prism::TestBackend>(events);
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({.width = 300, .height = 200});

    prism::model_app(backend, window, model);

    REQUIRE(!raw->submitted().empty());
    CHECK(golden_text(*raw->submitted().back()) == R"GOLDEN([widget 2] rect=(0.0,0.0,300.0,30.0)
    ClipPush rect=(0.0,0.0,300.0,30.0)
    FilledRect rect=(0.0,0.0,300.0,30.0) color=#2d2d37ff
    Text "0" at=(4.0,4.0) size=14.0 color=#dcdcdcff
    ClipPop
[widget 3] rect=(0.0,30.0,300.0,30.0)
    ClipPush rect=(0.0,30.0,300.0,30.0)
    FilledRect rect=(0.0,30.0,300.0,30.0) color=#2d2d37ff
    FilledRect rect=(8.0,37.0,16.0,16.0) color=#2d2d37ff
    RectOutline rect=(8.0,37.0,16.0,16.0) color=#5a5a69ff thickness=1.5
    Text "agree" at=(32.0,37.0) size=14.0 color=#dcdcdcff
    ClipPop
)GOLDEN");
}

TEST_CASE("golden: clicking the checkbox flips its rendered state") {
    std::vector<prism::InputEvent> events = {
        prism::MouseButton{prism::Point{prism::X{4.f}, prism::Y{34.f}}, 1, true},
    };
    GoldenModel model;
    auto backend_ptr = std::make_unique<prism::TestBackend>(events);
    auto* raw = backend_ptr.get();
    auto backend = prism::Backend{std::move(backend_ptr)};
    auto& window = backend.create_window({.width = 300, .height = 200});

    prism::model_app(backend, window, model);

    REQUIRE(!raw->submitted().empty());
    CHECK(golden_text(*raw->submitted().back()) == R"GOLDEN([widget 2] rect=(0.0,0.0,300.0,30.0)
    ClipPush rect=(0.0,0.0,300.0,30.0)
    FilledRect rect=(0.0,0.0,300.0,30.0) color=#2d2d37ff
    Text "0" at=(4.0,4.0) size=14.0 color=#dcdcdcff
    ClipPop
[widget 3] rect=(0.0,30.0,300.0,30.0)
    ClipPush rect=(0.0,30.0,300.0,30.0)
    FilledRect rect=(0.0,30.0,300.0,30.0) color=#2d2d37ff
    FilledRect rect=(8.0,37.0,16.0,16.0) color=#0078b4ff
    Text "✓" at=(10.0,38.0) size=13.0 color=#f0f0f0ff
    RectOutline rect=(8.0,37.0,16.0,16.0) color=#5a5a69ff thickness=1.5
    Text "agree" at=(32.0,37.0) size=14.0 color=#dcdcdcff
    RectOutline rect=(1.0,31.0,298.0,28.0) color=#50a0f0ff thickness=2.0
    ClipPop
)GOLDEN");
}
