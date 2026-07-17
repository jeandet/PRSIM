#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>
#include <fmt/format.h>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;
using namespace prism::ui;
using namespace prism::app;

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

#include <prism/app/widget_tree.hpp>

// Row leaves are the geometry entries with a small positive height (the
// VirtualList container's own entry is a full-viewport rect); this is the
// scan every row-dispatch test below relies on to resolve row ids safely
// (positional geometry[N] indexing is unreliable — see prior tasks).
std::vector<prism::WidgetId> row_ids(const prism::SceneSnapshot& snap, size_t count) {
    std::vector<prism::WidgetId> ids;
    for (auto& [id, rect] : snap.geometry) {
        if (id != 0 && rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) {
            ids.push_back(id);
            if (ids.size() == count) break;
        }
    }
    return ids;
}

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

TEST_CASE("scroll_row_into_view scrolls down to reveal an off-screen-below row, converges to a no-op") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    // Measure the real row height rather than assuming one — see this file's own
    // "VirtualList items at correct Y position after scroll" test for the same convention.
    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);

    // StringListModel::view() is a single vb.list(items) call — LayoutKind::VirtualList, not
    // Row/Column, so ViewBuilder::finalize()'s single-child hoist never fires here; root()'s
    // one child is the VirtualList container itself (same pattern already used in
    // tests/test_debug_name.cpp and tests/test_table.cpp).
    auto container_id = tree.root().children.front().id;

    tree.scroll_row_into_view(container_id, 30, prism::Height{item_h});
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    tree.scroll_row_into_view(container_id, 30, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty()); // already revealed — second identical call is a no-op
}

TEST_CASE("scroll_row_into_view scrolls up to reveal an off-screen-above row") {
    StringListModel model;
    for (int i = 0; i < 50; ++i)
        model.items.push_back(fmt::format("item {}", i));

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 100, 1);
    tree.clear_dirty();

    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);
    auto container_id = tree.root().children.front().id;

    tree.scroll_row_into_view(container_id, 40, prism::Height{item_h}); // scroll deep down first
    tree.clear_dirty();

    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h}); // now ask for row 0
    CHECK(tree.any_dirty());

    tree.clear_dirty();
    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("scroll_row_into_view is a no-op when the row already fits in the viewport") {
    StringListModel model;
    model.items.push_back("only one");

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    float item_h = 0;
    for (auto& [id, rect] : snap->geometry)
        if (rect.extent.h.raw() > 0 && rect.extent.h.raw() < 50) { item_h = rect.extent.h.raw(); break; }
    REQUIRE(item_h > 0);

    auto container_id = tree.root().children.front().id;
    tree.scroll_row_into_view(container_id, 0, prism::Height{item_h});
    CHECK_FALSE(tree.any_dirty());
}

TEST_CASE("scroll_row_into_view on an unknown container id is a safe no-op") {
    StringListModel model;
    model.items.push_back("only one");
    prism::WidgetTree tree(model);
    tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    tree.scroll_row_into_view(999999, 0, prism::Height{30.f});
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

#include <prism/ui/delegate.hpp> // for Checkbox

struct CheckboxListModel {
    prism::List<prism::Checkbox> items;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.list(items);
    }
};

TEST_CASE("clicking a VirtualList row writes the mutation back to the source List") {
    CheckboxListModel model;
    model.items.push_back({.checked = false, .label = "Enable feature"});

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    REQUIRE(!snap->geometry.empty());
    auto ids = row_ids(*snap, 1);
    REQUIRE(!ids.empty());
    auto row_id = ids[0];

    tree.dispatch(row_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    CHECK(model.items[0].checked == true);
}

struct ClickRow { int id; };

namespace prism::ui {
template <> struct Widget<ClickRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<ClickRow>&, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, detail::default_widget_h), bg);
    }

    static void handle_input(Field<ClickRow>&, const InputEvent&, WidgetNode&) {}
};
}

struct ClickRowListModel {
    prism::List<ClickRow> items;
    std::function<void(size_t, const ClickRow&)> on_row_click;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.list(items, on_row_click);
    }
};

TEST_CASE("clicking a VirtualList row invokes on_row_click with the index and value") {
    ClickRowListModel model;
    model.items.push_back({.id = 100});
    model.items.push_back({.id = 200});

    std::vector<std::pair<size_t, int>> clicks;
    model.on_row_click = [&](size_t index, const ClickRow& row) {
        clicks.emplace_back(index, row.id);
    };

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    // layout_flatten binds/pushes rows in ascending index order, so the first
    // two row ids correspond to items[0] and items[1] respectively.
    auto ids = row_ids(*snap, 2);
    REQUIRE(ids.size() == 2);

    tree.dispatch(ids[0], prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});
    tree.dispatch(ids[1], prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    REQUIRE(clicks.size() == 2);
    CHECK(clicks[0] == std::make_pair(size_t{0}, 100));
    CHECK(clicks[1] == std::make_pair(size_t{1}, 200));
}

TEST_CASE("a stale row closure does not crash if the list shrinks before the next event") {
    CheckboxListModel model;
    model.items.push_back({.checked = false, .label = "row 0"});
    model.items.push_back({.checked = false, .label = "row 1"});

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = row_ids(*snap, 2);
    REQUIRE(ids.size() == 2);
    auto row1_id = ids[1];

    // Shrink the list out from under the already-bound row 1 closure.
    model.items.erase(1);

    // Must not crash — the guard inside the write-back closure checks
    // index < items.size() before writing.
    tree.dispatch(row1_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    CHECK(model.items.size() == 1);
}

TEST_CASE("a stale row closure does not invoke on_row_click if the list shrinks before the next event") {
    ClickRowListModel model;
    model.items.push_back({.id = 100});
    model.items.push_back({.id = 200});

    std::vector<std::pair<size_t, int>> clicks;
    model.on_row_click = [&](size_t index, const ClickRow& row) {
        clicks.emplace_back(index, row.id);
    };

    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();

    auto ids = row_ids(*snap, 2);
    REQUIRE(ids.size() == 2);
    auto row1_id = ids[1];

    // Shrink the list out from under the already-bound row 1 closure.
    model.items.erase(1);

    // Must not crash, and must not invoke on_row_click for the erased index —
    // the guard inside the click connection checks index < items.size().
    tree.dispatch(row1_id, prism::MouseButton{prism::Point{prism::X{0}, prism::Y{0}}, 1, true});

    CHECK(clicks.empty());
}

TEST_CASE("existing .list() calls without on_row_click still compile and work") {
    prism::List<std::string> items;
    items.push_back("unchanged");
    struct M {
        prism::List<std::string>* items_ptr;
        void view(prism::WidgetTree::ViewBuilder& vb) { vb.list(*items_ptr); }
    };
    M model{&items};
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    CHECK(snap != nullptr);
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
