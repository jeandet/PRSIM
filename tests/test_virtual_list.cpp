#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/types.hpp>

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
