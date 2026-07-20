#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

#include <set>
#include <string>
#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// A tiny in-memory tree: root(1) -> {child(2), child(3)}; child(2) -> {grandchild(4)}
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
};

static_assert(prism::TreeStorage<FixtureTree>);

TEST_CASE("wrap_tree_storage produces a valid TreeSource") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    CHECK(src.root_count() == 1);
    CHECK(src.root_at(0) == 1);
    CHECK(src.child_count(1) == 2);
    CHECK(src.child_at(1, 0) == 2);
    CHECK(src.child_at(1, 1) == 3);
    CHECK(src.label(4) == "n4");
    CHECK(src.has_children(1) == true);
    CHECK(src.has_children(3) == false);
    CHECK_FALSE(src.icon);        // optional, unset when the source has no icon() method
    CHECK_FALSE(src.attributes);  // optional, unset when the source has no attributes() method
}

struct FixtureTreeWithIcon : FixtureTree {
    std::optional<std::string> icon(prism::TreeNodeId id) const {
        return id == 1 ? std::optional<std::string>{"root-icon"} : std::nullopt;
    }
};

TEST_CASE("wrap_tree_storage forwards icon() when the source provides it") {
    FixtureTreeWithIcon data;
    auto src = prism::wrap_tree_storage(data);
    REQUIRE(src.icon);
    CHECK(src.icon(1) == std::optional<std::string>{"root-icon"});
    CHECK(src.icon(2) == std::nullopt);
}

TEST_CASE("visible_rows shows only roots when nothing is expanded") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded;
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == 1);
    CHECK(rows[0].label == "n1");
    CHECK(rows[0].depth == 0);
    CHECK(rows[0].has_children == true);
    CHECK(rows[0].expanded == false);
}

TEST_CASE("visible_rows descends into expanded nodes, depth-first") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded{1, 2};
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].id == 1); CHECK(rows[0].depth == 0);
    CHECK(rows[1].id == 2); CHECK(rows[1].depth == 1);
    CHECK(rows[2].id == 4); CHECK(rows[2].depth == 2); // child(2)'s grandchild, visited before sibling 3
    CHECK(rows[3].id == 3); CHECK(rows[3].depth == 1);
}

TEST_CASE("visible_rows never queries children of a collapsed node") {
    // The core lazy-loading guarantee: child_count/child_at must never be invoked
    // for a node that isn't in `expanded`, regardless of how many children it has.
    prism::TreeSource src;
    src.root_count = [] { return 1; };
    src.root_at = [](size_t) -> prism::TreeNodeId { return 1; };
    src.has_children = [](prism::TreeNodeId) { return true; };
    src.label = [](prism::TreeNodeId id) { return "n" + std::to_string(id); };
    src.child_count = [](prism::TreeNodeId) -> size_t {
        FAIL("child_count queried for a collapsed node");
        return 0;
    };
    src.child_at = [](prism::TreeNodeId, size_t) -> prism::TreeNodeId {
        FAIL("child_at queried for a collapsed node");
        return 0;
    };
    std::set<prism::TreeNodeId> expanded; // empty -- root stays collapsed
    auto rows = prism::visible_rows(src, expanded);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].has_children == true);
}

TEST_CASE("visible_rows marks the selected node's row") {
    FixtureTree data;
    auto src = prism::wrap_tree_storage(data);
    std::set<prism::TreeNodeId> expanded{1};
    auto rows = prism::visible_rows(src, expanded, prism::TreeNodeId{3});
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].selected == false); // id 1
    CHECK(rows[1].selected == false); // id 2
    CHECK(rows[2].selected == true);  // id 3
}
