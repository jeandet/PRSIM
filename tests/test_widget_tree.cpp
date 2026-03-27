#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/widget_tree.hpp>
#include <prism/core/field.hpp>

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
