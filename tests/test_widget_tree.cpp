#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <string>

struct SimpleModel {
    prism::Field<int> count{"Count", 0};
    prism::Field<std::string> name{"Name", "hi"};
};

struct NestedModel {
    SimpleModel inner;
    prism::Field<bool> flag{"Flag", false};
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
