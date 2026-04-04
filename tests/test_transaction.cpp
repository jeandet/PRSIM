#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>
#include <prism/core/transaction.hpp>
#include <string>

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

TEST_CASE("transaction with multiple fields fires each callback once") {
    prism::Field<int> a{0};
    prism::Field<std::string> b{""};
    int a_calls = 0, b_calls = 0;
    auto c1 = a.on_change().connect([&](const int&) { ++a_calls; });
    auto c2 = b.on_change().connect([&](const std::string&) { ++b_calls; });

    prism::transaction([&] {
        a.set(1);
        b.set("hello");
        CHECK(a_calls == 0);
        CHECK(b_calls == 0);
    });

    CHECK(a_calls == 1);
    CHECK(b_calls == 1);
    CHECK(a.get() == 1);
    CHECK(b.get() == "hello");
}

TEST_CASE("transaction: set to same value produces no callback") {
    prism::Field<int> f{5};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });

    prism::transaction([&] {
        f.set(5);
    });

    CHECK(calls == 0);
}

TEST_CASE("empty transaction does not crash") {
    prism::transaction([&] {
        // nothing
    });
}

TEST_CASE("without transaction, callbacks fire immediately") {
    prism::Field<int> f{0};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });

    f.set(1);
    CHECK(calls == 1);
    f.set(2);
    CHECK(calls == 2);
}

TEST_CASE("nested transactions: callbacks fire only at outermost commit") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    prism::transaction([&] {
        f.set(1);

        prism::transaction([&] {
            f.set(2);
            CHECK(calls == 0);
        });

        // inner transaction committed, but outer still active
        CHECK(calls == 0);
        f.set(3);
    });

    CHECK(calls == 1);
    CHECK(last_value == 3);
}

TEST_CASE("TransactionGuard works identically to transaction()") {
    prism::Field<int> f{0};
    int calls = 0;
    int last_value = -1;
    auto conn = f.on_change().connect([&](const int& v) {
        ++calls;
        last_value = v;
    });

    {
        prism::TransactionGuard tx;
        f.set(10);
        f.set(20);
        CHECK(calls == 0);
    }

    CHECK(calls == 1);
    CHECK(last_value == 20);
}
