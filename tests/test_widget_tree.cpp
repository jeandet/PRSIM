#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <string>

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

    tree.dispatch(ids[2], prism::MouseButton{{50, 15}, 1, true});
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
    tree.dispatch(9999, prism::MouseButton{{0, 0}, 1, true});
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
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
    CHECK(model.flag.get() == true);
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> ignores mouse release") {
    BoolModel model;
    prism::WidgetTree tree(model);
    auto ids = tree.leaf_ids();

    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, false});
    CHECK(model.flag.get() == false);
}

TEST_CASE("Field<bool> toggle produces different draws on re-record") {
    BoolModel model;
    prism::WidgetTree tree(model);

    auto snap1 = tree.build_snapshot(800, 600, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    tree.dispatch(ids[0], prism::MouseButton{{50, 15}, 1, true});
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

    // ids[1] is the slider. Click at x=100 (middle of 200px track) → value ≈ 0.5
    tree.dispatch(ids[1], prism::MouseButton{{100, 15}, 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.5).epsilon(0.05));

    // Click at x=0 → value ≈ 0.0
    tree.dispatch(ids[1], prism::MouseButton{{0, 15}, 1, true});
    CHECK(model.volume.get().value == doctest::Approx(0.0).epsilon(0.05));
}
