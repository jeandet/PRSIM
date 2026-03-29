#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <string>

namespace {
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

struct SimpleModel {
    prism::Field<int> count{0};
    prism::Field<std::string> name{"hi"};
};

struct NestedModel {
    SimpleModel inner;
    prism::Field<bool> flag{false};
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
