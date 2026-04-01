#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>
#include <fmt/format.h>

using namespace prism;

TEST_CASE("IntScalar default constructs to zero") {
    ItemIndex idx;
    CHECK(idx.raw() == 0);
}

TEST_CASE("IntScalar explicit construction and raw access") {
    ItemIndex idx{42};
    CHECK(idx.raw() == 42);
}

TEST_CASE("IntScalar comparison") {
    CHECK(ItemIndex{1} < ItemIndex{2});
    CHECK(ItemIndex{3} == ItemIndex{3});
    CHECK(ItemCount{5} != ItemCount{6});
}

TEST_CASE("IntScalar arithmetic: ItemIndex + ItemCount = ItemIndex") {
    auto result = ItemIndex{3} + ItemCount{2};
    static_assert(std::is_same_v<decltype(result), ItemIndex>);
    CHECK(result.raw() == 5);
}

TEST_CASE("IntScalar arithmetic: ItemIndex - ItemIndex = ItemCount") {
    auto result = ItemIndex{7} - ItemIndex{3};
    static_assert(std::is_same_v<decltype(result), ItemCount>);
    CHECK(result.raw() == 4);
}

TEST_CASE("IntScalar arithmetic: ItemCount + ItemCount = ItemCount") {
    auto result = ItemCount{2} + ItemCount{3};
    static_assert(std::is_same_v<decltype(result), ItemCount>);
    CHECK(result.raw() == 5);
}

#include <prism/core/widget_tree.hpp>

TEST_CASE("VirtualListState default construction") {
    prism::VirtualListState state;
    CHECK(state.item_count.raw() == 0);
    CHECK(state.scroll_offset.raw() == doctest::Approx(0));
    CHECK(state.viewport_h.raw() == doctest::Approx(0));
    CHECK(state.visible_start.raw() == 0);
    CHECK(state.visible_end.raw() == 0);
    CHECK(state.overscan.raw() == 2);
}

TEST_CASE("compute_visible_range — basic viewport") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{100}, Height{30}, DY{0}, Height{100}, ItemCount{2});
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 6);
}

TEST_CASE("compute_visible_range — scrolled down") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{100}, Height{30}, DY{90}, Height{100}, ItemCount{2});
    CHECK(start.raw() == 1);
    CHECK(end.raw() == 9);
}

TEST_CASE("compute_visible_range — clamp to item count") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{5}, Height{30}, DY{0}, Height{300}, ItemCount{2});
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 5);
}

TEST_CASE("compute_visible_range — empty list") {
    auto [start, end] = prism::compute_visible_range(
        ItemCount{0}, Height{30}, DY{0}, Height{100}, ItemCount{2});
    CHECK(start.raw() == 0);
    CHECK(end.raw() == 0);
}

#include <prism/core/list.hpp>

struct StringListModel {
    prism::List<std::string> items;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.list(items);
    }
};

TEST_CASE("ViewBuilder::list creates VirtualList node") {
    StringListModel model;
    model.items.push_back("alpha");
    model.items.push_back("beta");
    model.items.push_back("gamma");
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() > 0);
}

TEST_CASE("VirtualList materializes visible items") {
    StringListModel model;
    for (int i = 0; i < 20; ++i)
        model.items.push_back(fmt::format("item {}", i));
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() < 20);
    CHECK(snap->geometry.size() > 0);
}

TEST_CASE("VirtualList stabilizes after two frames") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));
    prism::WidgetTree tree(model);
    // First frame: viewport_h unknown, second frame: viewport_h set by layout
    auto snap1 = tree.build_snapshot(400, 100, 1);
    auto snap2 = tree.build_snapshot(400, 100, 2);
    auto snap3 = tree.build_snapshot(400, 100, 3);
    CHECK(snap2->geometry.size() > 0);
    // After two frames, visible range is stable
    CHECK(snap3->geometry.size() == snap2->geometry.size());
}

TEST_CASE("scroll_at works on VirtualList") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    prism::WidgetId leaf_id = 0;
    for (auto& [id, rect] : snap->geometry) {
        if (rect.extent.w.raw() > 0 && rect.extent.h.raw() > 0
            && rect.extent.h.raw() < 50) {
            leaf_id = id;
            break;
        }
    }
    REQUIRE(leaf_id != 0);

    tree.scroll_at(leaf_id, prism::DY{50});
    CHECK(tree.any_dirty());
}

TEST_CASE("scroll_at clamps VirtualList to bounds") {
    StringListModel model;
    model.items.push_back("only one");

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], prism::DY{100});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("VirtualList reacts to List push_back") {
    StringListModel model;
    model.items.push_back("initial");

    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.push_back("added");
    CHECK(tree.any_dirty());

    auto snap2 = tree.build_snapshot(400, 300, 2);
    CHECK(snap2->geometry.size() >= snap1->geometry.size());
}

TEST_CASE("VirtualList reacts to List erase") {
    StringListModel model;
    model.items.push_back("a");
    model.items.push_back("b");
    model.items.push_back("c");

    prism::WidgetTree tree(model);
    [[maybe_unused]] auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.erase(1);
    CHECK(tree.any_dirty());
}

TEST_CASE("VirtualList reacts to List set (update)") {
    StringListModel model;
    model.items.push_back("original");

    prism::WidgetTree tree(model);
    [[maybe_unused]] auto snap1 = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    model.items.set(0, "updated");
    CHECK(tree.any_dirty());
}

TEST_CASE("VirtualList full scroll workflow") {
    StringListModel model;
    for (int i = 0; i < 100; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 100, 1);
    REQUIRE(snap1 != nullptr);
    size_t initial_count = snap1->geometry.size();
    CHECK(initial_count > 0);
    CHECK(initial_count < 100);

    prism::WidgetId leaf_id = 0;
    for (auto& [id, rect] : snap1->geometry) {
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) {
            leaf_id = id;
            break;
        }
    }
    if (leaf_id != 0) {
        tree.scroll_at(leaf_id, prism::DY{200});
        CHECK(tree.any_dirty());
    }

    auto snap2 = tree.build_snapshot(400, 100, 2);
    CHECK(snap2->geometry.size() > 0);

    CHECK(!snap2->overlay_geometry.empty());
}

TEST_CASE("VirtualList items at correct Y position after scroll") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(400, 200, 1);

    // Measure item height from first item's geometry
    float item_h = 0;
    for (auto& [id, rect] : snap1->geometry) {
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) {
            item_h = rect.extent.h.raw();
            break;
        }
    }
    REQUIRE(item_h > 0);

    // Scroll down by exactly 5 items
    DY scroll_amount{item_h * 5};
    auto ids = tree.leaf_ids();
    REQUIRE(!ids.empty());
    tree.scroll_at(ids[0], scroll_amount);

    auto snap2 = tree.build_snapshot(400, 200, 2);

    // After scrolling, the first visible leaf should be near the top of viewport
    // (within overscan buffer tolerance)
    bool found_near_top = false;
    for (auto& [id, rect] : snap2->geometry) {
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) {
            // At least one item should be within the viewport
            if (rect.origin.y.raw() >= -item_h && rect.origin.y.raw() < 200) {
                found_near_top = true;
                break;
            }
        }
    }
    CHECK(found_near_top);
}
