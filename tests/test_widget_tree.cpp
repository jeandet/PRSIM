#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/input/input_event.hpp>

#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

struct SimpleModel {
    prism::Field<int> count{0};
    prism::Field<std::string> name{"hi"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, name);
    }
};

struct NestedModel {
    SimpleModel inner;
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(inner, flag);
    }
};

TEST_CASE("WidgetTree creates one leaf per Field") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("WidgetTree recurses into nested components") {
    NestedModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);
}

TEST_CASE("WidgetTree nodes track dirty state from Field::set") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK_FALSE(tree.any_dirty());
    model.count.set(5);
    CHECK(tree.any_dirty());
}

TEST_CASE("WidgetTree clear_dirty resets all flags") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    model.count.set(5);
    CHECK(tree.any_dirty());
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("WidgetTree nodes have stable IDs") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    CHECK(ids.size() == 2);
    CHECK(ids == tree.leaf_ids());
}

TEST_CASE("WidgetTree builds SceneSnapshot from model") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 2);
    CHECK(snap->version == 1);
}

TEST_CASE("WidgetTree dispatch emits on correct widget on_input") {
    NestedModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 3);

    bool received = false;
    auto conn = tree.connect_input(ids[2], [&](const prism::InputEvent&) {
        received = true;
    });

    tree.dispatch(ids[2], prism::MouseButton{P(50, 15), 1, true});
    CHECK(received);
}

TEST_CASE("WidgetTree refresh_dirty re-records draws from field state") {
    SimpleModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    model.count.set(99);
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    REQUIRE(snap2 != nullptr);
    CHECK(snap2->geometry.size() == 2);
}

TEST_CASE("WidgetTree dispatch to unknown id is a no-op") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    // Should not crash
    tree.dispatch(9999, prism::MouseButton{P(0, 0), 1, true});
}

struct BoolModel {
    prism::Field<bool> flag{false};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(flag, count);
    }
};

TEST_CASE("Field<bool> toggles on MouseButton dispatch") {
    BoolModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 2);

    CHECK(model.flag.get() == false);
    tree.dispatch(ids[0], prism::MouseButton{P(50, 15), 1, true});
    CHECK(model.flag.get() == true);
    tree.dispatch(ids[0], prism::MouseButton{P(50, 15), 1, true});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> ignores mouse release") {
    BoolModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.dispatch(ids[0], prism::MouseButton{P(50, 15), 1, false});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> toggle produces different draws on re-record") {
    BoolModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    tree.dispatch(ids[0], prism::MouseButton{P(50, 15), 1, true});
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(800, 600, 2);
    REQUIRE(snap2 != nullptr);
    CHECK(snap2->draw_lists.size() == snap1->draw_lists.size());
}

#include <prism/core/state.hpp>


struct ModelWithState {
    prism::Field<int> visible{0};
    prism::State<int> hidden{0};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(visible, flag);
    }
};

TEST_CASE("WidgetTree skips State<T> members") {
    ModelWithState model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("State<T> change does not dirty the widget tree") {
    ModelWithState model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    model.hidden.set(999);
    CHECK_FALSE(tree.any_dirty());
}

struct SentinelModel {
    prism::Field<prism::Label<>> status{{"OK"}};
    prism::Field<prism::Slider<>> volume{{.value = 0.5}};
    prism::Field<bool> enabled{true};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(status, volume, enabled);
    }
};

TEST_CASE("WidgetTree with sentinel types creates correct leaf count") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 3);
}

TEST_CASE("WidgetTree builds snapshot with sentinel types") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE(snap != nullptr);
    CHECK(snap->geometry.size() == 3);
}

TEST_CASE("Slider click through WidgetTree dispatch updates value") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 3);

    tree.dispatch(ids[1], prism::MouseButton{P(100, 15), 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.5).epsilon(0.05));

    tree.dispatch(ids[1], prism::MouseButton{P(0, 15), 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("Slider drag updates value on MouseMove while pressed") {
    SentinelModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    REQUIRE(ids.size() == 3);
    auto slider_id = ids[1];

    // Press at 25%
    tree.set_pressed(slider_id, true);
    tree.dispatch(slider_id, prism::MouseButton{P(50, 15), 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.25).epsilon(0.05));
    CHECK(tree.captured_id() == slider_id);

    // Drag to 75%
    tree.dispatch(slider_id, prism::MouseMove{P(150, 15)});
    CHECK(model.volume.get().value == doctest::Approx(0.75).epsilon(0.05));

    // Release
    tree.set_pressed(slider_id, false);
    CHECK(tree.captured_id() == 0);
}

TEST_CASE("update_hover sets hovered state and marks dirty") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    tree.clear_dirty();
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    CHECK(tree.any_dirty());
}

TEST_CASE("update_hover clears previous hover") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    tree.clear_dirty();

    tree.update_hover(ids[1]);
    CHECK(tree.any_dirty());
}

TEST_CASE("update_hover with same id is a no-op") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    tree.clear_dirty();

    tree.update_hover(ids[0]);
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("update_hover with nullopt clears hover") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    tree.clear_dirty();

    tree.update_hover(std::nullopt);
    CHECK(tree.any_dirty());
}

TEST_CASE("set_pressed marks widget dirty") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    tree.clear_dirty();

    tree.set_pressed(ids[0], true);
    CHECK(tree.any_dirty());
}

struct ButtonModel {
    prism::Field<prism::Button> action{{"Click me"}};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(action, count);
    }
};

TEST_CASE("Button in WidgetTree creates leaf") {
    ButtonModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("Button click through WidgetTree dispatch increments count") {
    ButtonModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    CHECK(model.action.get().click_count == 0);
    tree.dispatch(ids[0], prism::MouseButton{P(10, 10), 1, true});
    CHECK(model.action.get().click_count == 1);
}

struct FocusModel {
    prism::Field<prism::Label<>> title{{"Hello"}};
    prism::Field<bool> toggle{false};
    prism::Field<prism::Slider<>> slider{{.value = 0.5}};
    prism::Field<prism::Button> btn{{"Click"}};
    prism::Field<int> count{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(title, toggle, slider, btn, count);
    }
};

TEST_CASE("focus_order contains only focusable widgets in struct order") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    // ids: [0]=Label, [1]=bool, [2]=Slider, [3]=Button, [4]=int
    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 3);
    CHECK(focus[0] == ids[1]);  // bool
    CHECK(focus[1] == ids[2]);  // Slider
    CHECK(focus[2] == ids[3]);  // Button
}

TEST_CASE("focused_id starts at 0") {
    FocusModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.focused_id() == 0);
}

TEST_CASE("set_focused on focusable widget sets focused_id") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);
    CHECK(tree.focused_id() == focus[0]);
}

TEST_CASE("set_focused on non-focusable widget is no-op") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();
    tree.set_focused(ids[0]);  // Label -- not focusable
    CHECK(tree.focused_id() == 0);
}

TEST_CASE("set_focused marks old and new widget dirty") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);
    tree.clear_dirty();

    tree.set_focused(focus[1]);
    CHECK(tree.any_dirty());
}

TEST_CASE("clear_focus resets focused_id and marks dirty") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);
    tree.clear_dirty();

    tree.clear_focus();
    CHECK(tree.focused_id() == 0);
    CHECK(tree.any_dirty());
}

TEST_CASE("focus_next cycles forward through focusable widgets") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.focus_next();
    CHECK(tree.focused_id() == focus[0]);
    tree.focus_next();
    CHECK(tree.focused_id() == focus[1]);
    tree.focus_next();
    CHECK(tree.focused_id() == focus[2]);
    tree.focus_next();  // wraps
    CHECK(tree.focused_id() == focus[0]);
}

TEST_CASE("focus_prev cycles backward through focusable widgets") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.focus_prev();  // no focus -> last
    CHECK(tree.focused_id() == focus[2]);
    tree.focus_prev();
    CHECK(tree.focused_id() == focus[1]);
    tree.focus_prev();
    CHECK(tree.focused_id() == focus[0]);
    tree.focus_prev();  // wraps
    CHECK(tree.focused_id() == focus[2]);
}

TEST_CASE("focus_next on model with no focusable widgets is no-op") {
    SimpleModel model;  // Field<int> + Field<string> -- both non-focusable
    prism::WidgetTree tree(model);
    tree.focus_next();
    CHECK(tree.focused_id() == 0);
}

TEST_CASE("Space dispatched to focused bool toggles it") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[0]);  // bool toggle
    tree.dispatch(tree.focused_id(), prism::KeyPress{prism::keys::space, 0});
    CHECK(model.toggle.get() == true);
}

TEST_CASE("Enter dispatched to focused Button increments click_count") {
    FocusModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();

    tree.set_focused(focus[2]);  // Button
    tree.dispatch(tree.focused_id(), prism::KeyPress{prism::keys::enter, 0});
    CHECK(model.btn.get().click_count == 1);
}

#include <prism/core/derived.hpp>
#include <prism/core/shared.hpp>

struct DerivedModel {
    prism::Field<int> x{5};
    prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(x);
        vb.widget(doubled);
    }
};

struct SharedModel {
    prism::core::Shared<float> temp{22.5f};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(temp);
    }
};

TEST_CASE("Derived<T> creates a read-only widget node") {
    DerivedModel m;
    prism::WidgetTree tree(m);
    CHECK(tree.leaf_count() == 2);
}

TEST_CASE("Shared<T> creates a read-only widget node") {
    SharedModel m;
    prism::WidgetTree tree(m);
    CHECK(tree.leaf_count() == 1);
}

TEST_CASE("Derived<T> widget re-renders when source changes") {
    DerivedModel m;
    prism::WidgetTree tree(m);
    tree.clear_dirty();
    CHECK_FALSE(tree.any_dirty());

    m.x.set(10);
    CHECK(tree.any_dirty());
}

struct CanvasDerivedModel {
    prism::Field<int> x{5};
    prism::core::Derived<int> doubled{[this] { return x.get() * 2; }, x};

    void canvas(prism::DrawList& dl, prism::Rect bounds, const prism::WidgetNode&) {
        dl.filled_rect(bounds, prism::Color::rgba(0, 0, 0));
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(x);
        vb.canvas(*this).depends_on(doubled);
    }
};

TEST_CASE("canvas depends_on accepts Derived<T>") {
    CanvasDerivedModel m;
    prism::WidgetTree tree(m);
    tree.clear_dirty();

    m.x.set(10);  // triggers doubled recomputation
    CHECK(tree.any_dirty());
}

struct TwoCanvasModel {
    void canvas(prism::DrawList&, prism::Rect, const prism::WidgetNode&) {}

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.canvas(*this).min_size(prism::Height{50});
            vb.canvas(*this).min_size(prism::Height{50});
        });
    }
};

TEST_CASE("canvas().min_size(Height) gives a fixed main-axis size instead of expand-sharing") {
    // Regression test: layout_measure's Canvas case unconditionally hardcoded
    // {preferred=0, expand=true}, so every canvas in a stack got an equal share of
    // leftover space regardless of its intended content size -- found via a real app
    // where a ~12px heartbeat icon ended up in a ~120px box because it shared space
    // equally with three full plots. min_size() opts a canvas out of expand-sharing.
    TwoCanvasModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(300, 200, 1);

    REQUIRE(snap->geometry.size() == 2);
    CHECK(snap->geometry[0].second.extent.h.raw() == doctest::Approx(50.0));
    CHECK(snap->geometry[1].second.extent.h.raw() == doctest::Approx(50.0));
    // Cross-axis (width) still stretches to fill the container regardless of min_size.
    CHECK(snap->geometry[0].second.extent.w.raw() == doctest::Approx(300.0));
}

struct MixedCanvasModel {
    void canvas(prism::DrawList&, prism::Rect, const prism::WidgetNode&) {}

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.canvas(*this).min_size(prism::Height{50});  // fixed
            vb.canvas(*this);                              // expand, gets the rest
        });
    }
};

TEST_CASE("canvas() without min_size still expands into remaining space") {
    MixedCanvasModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(300, 200, 1);

    REQUIRE(snap->geometry.size() == 2);
    CHECK(snap->geometry[0].second.extent.h.raw() == doctest::Approx(50.0));
    CHECK(snap->geometry[1].second.extent.h.raw() == doctest::Approx(150.0));  // 200 - 50
}

TEST_CASE("hovered_id() reflects the currently hovered widget") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.update_hover(ids[0]);
    CHECK(tree.hovered_id() == ids[0]);
}

TEST_CASE("hovered_id() is 0 when nothing is hovered") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.hovered_id() == 0);
}

TEST_CASE("set_debug_highlight injects a RectOutline at the target's rect on the next build_snapshot") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    prism::Rect target_rect;
    for (auto& [id, rect] : snap1->geometry)
        if (id == ids[0]) target_rect = rect;

    tree.set_debug_highlight(ids[0]);
    auto snap2 = tree.build_snapshot(400, 300, 2);

    bool found = false;
    for (auto& cmd : snap2->overlay.commands) {
        if (auto* ro = std::get_if<prism::RectOutline>(&cmd)) {
            if (ro->rect.origin.x.raw() == target_rect.origin.x.raw()
                && ro->rect.origin.y.raw() == target_rect.origin.y.raw())
                found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("set_debug_highlight(nullopt) clears the highlight") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());

    tree.set_debug_highlight(ids[0]);
    auto snap_highlighted = tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();
    bool found_when_set = false;
    for (auto& cmd : snap_highlighted->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found_when_set = true;
    REQUIRE(found_when_set);

    tree.set_debug_highlight(std::nullopt);
    auto snap_cleared = tree.build_snapshot(400, 300, 3);
    bool found_when_cleared = false;
    for (auto& cmd : snap_cleared->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found_when_cleared = true;
    CHECK_FALSE(found_when_cleared);
}

TEST_CASE("set_debug_highlight with a nonexistent id injects nothing and does not crash") {
    SimpleModel model;
    prism::WidgetTree tree(model);
    tree.set_debug_highlight(999999);
    auto snap = tree.build_snapshot(400, 300, 1);
    bool found = false;
    for (auto& cmd : snap->overlay.commands)
        if (std::holds_alternative<prism::RectOutline>(cmd)) found = true;
    CHECK_FALSE(found);
}
