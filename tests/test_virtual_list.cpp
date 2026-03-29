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
