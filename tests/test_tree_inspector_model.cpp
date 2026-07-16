#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("TreeInspectorModel row click invokes on_click with the row's index and id") {
    prism::debug::TreeInspectorModel model;
    model.rows.push_back(prism::debug::NodeRow{.id = 42});

    std::vector<std::pair<size_t, prism::WidgetId>> clicks;
    model.on_click = [&](size_t index, const prism::debug::NodeRow& row) {
        clicks.emplace_back(index, row.id);
    };

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    prism::WidgetId row_leaf_id = 0;
    for (auto& [id, rect] : snap->geometry) {
        if (id != 0 && rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { row_leaf_id = id; break; }
    }
    REQUIRE(row_leaf_id != 0);

    tree.dispatch(row_leaf_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0] == std::make_pair(size_t{0}, prism::WidgetId{42}));
}

TEST_CASE("TreeInspectorModel with no on_click set does not crash on row click") {
    prism::debug::TreeInspectorModel model;
    model.rows.push_back(prism::debug::NodeRow{.id = 7});
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    prism::WidgetId row_leaf_id = 0;
    for (auto& [id, rect] : snap->geometry)
        if (id != 0 && rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { row_leaf_id = id; break; }
    REQUIRE(row_leaf_id != 0);

    tree.dispatch(row_leaf_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});
    CHECK(true); // must not crash — on_click is unset (default-constructed std::function)
}
