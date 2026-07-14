// Regression test: table rows are intentionally hit-test transparent (all
// share WidgetId 0, so hit_test() skips them and the table container handles
// input directly). But WidgetTree::index_ is a flat map keyed by id, so every
// row materialized in the same pass aliases index_[0] to whichever row was
// inserted last. update_hover(nullopt) — fired whenever the mouse moves off
// every real widget — looks up index_[hovered_id_=0] with no id!=0 guard,
// which spuriously marks that aliased row hovered+dirty.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/table.hpp>
#include <prism/app/widget_tree.hpp>
#include <fmt/format.h>
#include <array>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;

namespace {

const WidgetNode* find_table_container(const WidgetNode& node) {
    if (node.layout_kind == LayoutKind::Table) return &node;
    for (auto& c : node.children)
        if (auto* found = find_table_container(c)) return found;
    return nullptr;
}

} // namespace

struct HoverAliasColumns {
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<std::string> name = {"a", "b", "c"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 2> h = {"X", "Name"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        if (c == 0) return fmt::to_string(x[r]);
        return name[r];
    }
};

struct HoverAliasModel {
    HoverAliasColumns table_data;
    Field<int> real_widget{0};

    void view(WidgetTree::ViewBuilder& vb) {
        vb.table(table_data);
        vb.widget(real_widget);
    }
};

TEST_CASE("update_hover(nullopt) does not alias into a table row when a Table exists") {
    HoverAliasModel model;
    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    snap = tree.build_snapshot(400, 300, 2); // second frame: table pool stabilizes

    const auto* table = find_table_container(tree.root());
    REQUIRE(table != nullptr);
    REQUIRE(!table->children.empty());

    auto ids = tree.leaf_ids();
    auto real_it = std::find_if(ids.begin(), ids.end(), [](WidgetId id) { return id != 0; });
    REQUIRE(real_it != ids.end());
    WidgetId real_id = *real_it;

    // Hover a real widget, then move off everything (id=0 == "nothing").
    tree.update_hover(real_id);
    tree.clear_dirty();
    tree.update_hover(std::nullopt);

    for (auto& row : table->children) {
        CHECK_FALSE(row.visual_state.hovered);
        CHECK_FALSE(row.dirty);
    }
}
