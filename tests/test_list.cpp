#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/list.hpp>

#include <string>

TEST_CASE("List starts empty") {
    prism::List<int> list;
    CHECK(list.size() == 0);
    CHECK(list.empty());
}

TEST_CASE("List::push_back adds and notifies") {
    prism::List<int> list;
    size_t inserted_idx = 999;
    int inserted_val = -1;
    auto conn = list.on_insert().connect([&](size_t i, const int& v) {
        inserted_idx = i;
        inserted_val = v;
    });
    list.push_back(42);
    CHECK(list.size() == 1);
    CHECK(list[0] == 42);
    CHECK(inserted_idx == 0);
    CHECK(inserted_val == 42);
}

TEST_CASE("List::erase removes and notifies") {
    prism::List<std::string> list;
    list.push_back("a");
    list.push_back("b");
    list.push_back("c");

    size_t removed_idx = 999;
    auto conn = list.on_remove().connect([&](size_t i) { removed_idx = i; });

    list.erase(1);
    CHECK(list.size() == 2);
    CHECK(list[0] == "a");
    CHECK(list[1] == "c");
    CHECK(removed_idx == 1);
}

TEST_CASE("List::set updates in place and notifies") {
    prism::List<int> list;
    list.push_back(1);
    list.push_back(2);

    size_t updated_idx = 999;
    int updated_val = -1;
    auto conn = list.on_update().connect([&](size_t i, const int& v) {
        updated_idx = i;
        updated_val = v;
    });

    list.set(1, 20);
    CHECK(list[1] == 20);
    CHECK(updated_idx == 1);
    CHECK(updated_val == 20);
}

TEST_CASE("List iteration") {
    prism::List<int> list;
    list.push_back(10);
    list.push_back(20);
    list.push_back(30);

    int sum = 0;
    for (const auto& v : list) sum += v;
    CHECK(sum == 60);
}

TEST_CASE("List connection RAII") {
    prism::List<int> list;
    int calls = 0;
    {
        auto conn = list.on_insert().connect([&](size_t, const int&) { ++calls; });
        list.push_back(1);
        CHECK(calls == 1);
    }
    list.push_back(2);
    CHECK(calls == 1);
}
