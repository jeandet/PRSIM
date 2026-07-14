// Regression test: bounce-at-bounds bubbling from scroll_at() must work for
// every container kind that get_scroll_view() unifies (Scroll, VirtualList,
// Table), not just plain Scroll. Kept in its own file since it needs a
// deliberately unusual nesting (a VirtualList inside an outer Scroll) that
// none of test_scroll.cpp / test_virtual_list.cpp / test_table.cpp cover.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/core/list.hpp>
#include <fmt/format.h>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;

namespace {

const WidgetNode* find_scroll_container(const WidgetNode& node) {
    if (node.layout_kind == LayoutKind::Scroll) return &node;
    for (auto& c : node.children)
        if (auto* found = find_scroll_container(c)) return found;
    return nullptr;
}

} // namespace

struct InnerListModel {
    prism::List<std::string> items;
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.list(items); }
};

struct OuterScrollWithInnerList {
    InnerListModel inner;
    Field<int> c{0};
    Field<int> d{0};
    Field<int> e{0};
    Field<int> f{0};
    Field<int> g{0};
    Field<int> h{0};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.component(inner);
            vb.widget(c);
            vb.widget(d);
            vb.widget(e);
            vb.widget(f);
            vb.widget(g);
            vb.widget(h);
        });
    }
};

TEST_CASE("BubbleAtBounds: VirtualList nested in outer Scroll bubbles at its limit") {
    OuterScrollWithInnerList model;
    for (int i = 0; i < 50; ++i)
        model.inner.items.push_back(fmt::format("item {}", i));

    WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 80, 1);
    tree.clear_dirty();

    const auto* outer = find_scroll_container(tree.root());
    REQUIRE(outer != nullptr);
    auto outer_offset = [&] {
        auto* ss = std::any_cast<ScrollState>(&outer->edit_state);
        REQUIRE(ss != nullptr);
        return ss->offset_y.raw();
    };

    // Give the outer container a non-zero offset so a bubbled delta is
    // observable (starting from 0 would clamp to 0 either way).
    tree.scroll_to(outer->id, DY{50});
    REQUIRE(outer_offset() == doctest::Approx(50));

    // The inner VirtualList starts at offset 0 (its minimum bound). Scrolling
    // it further up can't move it — the delta should bubble to the outer
    // Scroll instead of being silently dropped.
    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], DY{-30});

    CHECK(outer_offset() == doctest::Approx(20));
}
