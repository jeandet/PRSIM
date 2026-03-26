#include <prism/core/atomic_cell.hpp>

#include <cassert>
#include <string>
#include <thread>

void test_store_load()
{
    prism::atomic_cell<int> cell;
    assert(cell.load() == nullptr);

    cell.store(42);
    assert(*cell.load() == 42);

    cell.store(99);
    assert(*cell.load() == 99);
}

void test_concurrent_access()
{
    prism::atomic_cell<std::string> cell(std::string{"initial"});

    constexpr int iterations = 10000;

    std::jthread writer([&] {
        for (int i = 0; i < iterations; ++i)
            cell.store("value_" + std::to_string(i));
    });

    // Reader should never see a torn value.
    for (int i = 0; i < iterations; ++i) {
        auto snap = cell.load();
        if (snap)
            assert(snap->starts_with("value_") || *snap == "initial");
    }
}

int main()
{
    test_store_load();
    test_concurrent_access();
}
