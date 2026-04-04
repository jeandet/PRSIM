#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>
#include <prism/core/transaction.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("transaction defers callback until commit") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(42);
        CHECK(calls == 0);
    });

    CHECK(calls == 1);
    CHECK(last_value == 42);
}

TEST_CASE("transaction coalesces multiple sets to same field") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(1);
        f.set(2);
        f.set(3);
    });

    CHECK(calls == 1);
    CHECK(last_value == 3);
}
