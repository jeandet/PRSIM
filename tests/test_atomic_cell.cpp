#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/atomic_cell.hpp>

#include <string>
#include <thread>

TEST_CASE("atomic_cell store and load")
{
    prism::atomic_cell<int> cell;
    CHECK(cell.load() == nullptr);

    cell.store(42);
    CHECK(*cell.load() == 42);

    cell.store(99);
    CHECK(*cell.load() == 99);
}

TEST_CASE("atomic_cell concurrent access never tears")
{
    prism::atomic_cell<std::string> cell(std::string{"initial"});

    constexpr int iterations = 10000;

    std::jthread writer([&] {
        for (int i = 0; i < iterations; ++i)
            cell.store("value_" + std::to_string(i));
    });

    for (int i = 0; i < iterations; ++i) {
        auto snap = cell.load();
        if (snap)
            CHECK((snap->starts_with("value_") || *snap == "initial"));
    }
}
