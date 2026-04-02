#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/atomic_cell.hpp>

#include <fmt/format.h>
#include <string>
#include <thread>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


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
            cell.store(fmt::format("value_{}", i));
    });

    for (int i = 0; i < iterations; ++i) {
        auto snap = cell.load();
        if (snap)
            CHECK((snap->starts_with("value_") || *snap == "initial"));
    }
}
