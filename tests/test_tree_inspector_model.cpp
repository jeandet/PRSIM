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

namespace {
prism::Theme row_test_theme;

prism::WidgetNode make_row_node() {
    prism::WidgetNode node;
    node.theme = &row_test_theme;
    return node;
}
}

// Closes the "main -> debug" highlighting gap: row.hovered mirrors whether this row's
// widget is the one currently hovered in the *main* window (populated by flatten_node
// from WidgetTree::hovered_id()), independent of vs.hovered (mouse-over-this-debug-row
// itself). Widget<NodeRow>::record must render a visible difference for it, or hovering
// a widget in the main window has no observable effect in the debug window.
TEST_CASE("Widget<NodeRow> record highlights the background when row.hovered is true") {
    prism::debug::NodeRow unhovered_row;
    unhovered_row.id = 1;
    unhovered_row.name = "leaf";
    unhovered_row.hovered = false;
    prism::Field<prism::debug::NodeRow> unhovered_field{unhovered_row};
    auto node = make_row_node();

    prism::DrawList dl_unhovered;
    prism::Widget<prism::debug::NodeRow>::record(dl_unhovered, unhovered_field, node);

    prism::debug::NodeRow hovered_row = unhovered_row;
    hovered_row.hovered = true;
    prism::Field<prism::debug::NodeRow> hovered_field{hovered_row};
    prism::DrawList dl_hovered;
    prism::Widget<prism::debug::NodeRow>::record(dl_hovered, hovered_field, node);

    REQUIRE(!dl_unhovered.commands.empty());
    REQUIRE(!dl_hovered.commands.empty());
    auto bg_unhovered = std::get<prism::FilledRect>(dl_unhovered.commands[0]).color;
    auto bg_hovered = std::get<prism::FilledRect>(dl_hovered.commands[0]).color;
    CHECK(bg_unhovered.r != bg_hovered.r);
}
