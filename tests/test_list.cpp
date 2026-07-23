#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/list.hpp>

#include <string>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


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

TEST_CASE("List::replace_all shrinks: overlapping indices update, extra rows removed") {
    prism::List<int> list;
    list.push_back(1);
    list.push_back(2);
    list.push_back(3);

    std::vector<size_t> updated_indices;
    std::vector<size_t> removed_indices;
    auto conn_u = list.on_update().connect([&](size_t i, const int&) { updated_indices.push_back(i); });
    auto conn_r = list.on_remove().connect([&](size_t i) { removed_indices.push_back(i); });

    list.replace_all(std::vector<int>{10, 20});

    CHECK(list.size() == 2);
    CHECK(list[0] == 10);
    CHECK(list[1] == 20);
    CHECK(updated_indices == std::vector<size_t>{0, 1});
    CHECK(removed_indices == std::vector<size_t>{2}); // only the size delta
}

TEST_CASE("List::replace_all grows: overlapping indices update, extra rows inserted") {
    prism::List<int> list;
    list.push_back(1);

    std::vector<size_t> updated_indices;
    std::vector<size_t> inserted_indices;
    auto conn_u = list.on_update().connect([&](size_t i, const int&) { updated_indices.push_back(i); });
    auto conn_i = list.on_insert().connect([&](size_t i, const int&) { inserted_indices.push_back(i); });

    list.replace_all(std::vector<int>{100, 200, 300});

    CHECK(list.size() == 3);
    CHECK(list[0] == 100);
    CHECK(list[1] == 200);
    CHECK(list[2] == 300);
    CHECK(updated_indices == std::vector<size_t>{0});
    CHECK(inserted_indices == std::vector<size_t>{1, 2});
}

TEST_CASE("List::replace_all with same size only updates, never inserts or removes") {
    prism::List<std::string> list;
    list.push_back("a");
    list.push_back("b");

    int inserts = 0, removes = 0, updates = 0;
    auto conn_i = list.on_insert().connect([&](size_t, const std::string&) { ++inserts; });
    auto conn_r = list.on_remove().connect([&](size_t) { ++removes; });
    auto conn_u = list.on_update().connect([&](size_t, const std::string&) { ++updates; });

    list.replace_all(std::vector<std::string>{"x", "y"});

    CHECK(list[0] == "x");
    CHECK(list[1] == "y");
    CHECK(inserts == 0);
    CHECK(removes == 0);
    CHECK(updates == 2);
}
