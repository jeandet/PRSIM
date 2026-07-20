#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>
#include <prism/input/input_event.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
// root(1) -> {child(2), child(3)}; child(2) -> {grandchild(4)}
struct FixtureTree {
    size_t root_count() const { return 1; }
    prism::TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(prism::TreeNodeId id) const {
        if (id == 1) return 2;
        if (id == 2) return 1;
        return 0;
    }
    prism::TreeNodeId child_at(prism::TreeNodeId id, size_t i) const {
        if (id == 1) return i == 0 ? 2 : 3;
        if (id == 2) return 4;
        return 0;
    }
    std::string label(prism::TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(prism::TreeNodeId id) const { return id == 1 || id == 2; }
    std::vector<std::pair<std::string, std::string>> attributes(prism::TreeNodeId id) const {
        return {{"id", std::to_string(id)}};
    }
};
}

TEST_CASE("TreeController starts with only the root visible, collapsed") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    REQUIRE(ctrl.rows.size() == 1);
    CHECK(ctrl.rows[0].id == 1);
    CHECK(ctrl.rows[0].expanded == false);
    CHECK(ctrl.selected.get() == std::nullopt);
}

TEST_CASE("clicking a row with children selects it and toggles expand") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]);

    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
    REQUIRE(ctrl.rows.size() == 3); // root + its two children now visible
    CHECK(ctrl.rows[0].expanded == true);
    REQUIRE(ctrl.detail.get().has_value());
    CHECK(ctrl.detail.get()->label == "n1");
    CHECK(ctrl.detail.get()->attributes == std::vector<std::pair<std::string,std::string>>{{"id", "1"}});
}

TEST_CASE("clicking an already-expanded row collapses it again") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand
    ctrl.on_row_clicked(0, ctrl.rows[0]); // collapse
    REQUIRE(ctrl.rows.size() == 1);
    CHECK(ctrl.rows[0].expanded == false);
}

TEST_CASE("clicking a leaf row selects it without changing row count") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand root -> rows = [1, 2, 3]
    REQUIRE(ctrl.rows.size() == 3);
    ctrl.on_row_clicked(2, ctrl.rows[2]); // click leaf id 3
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{3});
    CHECK(ctrl.rows.size() == 3);
}

TEST_CASE("Down arrow moves selection to the next visible row and returns its index") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.on_row_clicked(0, ctrl.rows[0]); // expand root -> rows = [1, 2, 3]
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::down, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 1);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("Up arrow at the first row does not move selection and returns nullopt") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::up, 0});
    CHECK_FALSE(idx.has_value());
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Right arrow expands a collapsed selected node without moving selection") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::right, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
    CHECK(ctrl.rows[0].expanded == true);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1}); // unchanged
}

TEST_CASE("Right arrow on an already-expanded node moves to its first child") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::right, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 1);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{2});
}

TEST_CASE("Left arrow collapses an expanded selected node without moving selection") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::left, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
    CHECK(ctrl.rows[0].expanded == false);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Left arrow on a collapsed child moves selection to its parent") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});
    ctrl.on_key(prism::KeyPress{prism::keys::right, 0}); // expand root -> rows = [1, 2, 3]
    ctrl.selected.set(prism::TreeNodeId{3});             // select leaf child, no children of its own

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::left, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
    CHECK(ctrl.selected.get() == std::optional<prism::TreeNodeId>{1});
}

TEST_CASE("Enter on the selected row has the same effect as clicking it") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    ctrl.selected.set(prism::TreeNodeId{1});

    auto idx = ctrl.on_key(prism::KeyPress{prism::keys::enter, 0});
    REQUIRE(idx.has_value());
    CHECK(*idx == 0);
    CHECK(ctrl.rows[0].expanded == true);
}

TEST_CASE("on_key ignores non-KeyPress events") {
    FixtureTree data;
    prism::TreeController ctrl(prism::wrap_tree_storage(data));
    auto idx = ctrl.on_key(prism::MouseMove{});
    CHECK_FALSE(idx.has_value());
}
