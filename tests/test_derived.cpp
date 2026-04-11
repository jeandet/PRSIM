#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/derived.hpp>
#include <prism/core/field.hpp>
#include <prism/core/state.hpp>
#include <string>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("Derived recomputes when source changes") {
    prism::Field<int> a{2};
    prism::Field<int> b{3};
    prism::core::Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    CHECK(sum.get() == 5);

    a.set(10);
    CHECK(sum.get() == 13);

    b.set(7);
    CHECK(sum.get() == 17);
}

TEST_CASE("Derived fires on_change when value changes") {
    prism::Field<int> x{1};
    prism::core::Derived<int> doubled{[&] { return x.get() * 2; }, x};

    int calls = 0;
    int last = -1;
    auto conn = doubled.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    x.set(5);
    CHECK(calls == 1);
    CHECK(last == 10);
}

TEST_CASE("Derived suppresses on_change when recomputed value unchanged") {
    prism::Field<int> x{5};
    prism::core::Derived<int> clamped{[&] { return std::min(x.get(), 10); }, x};

    int calls = 0;
    auto conn = clamped.on_change().connect([&](const int&) { ++calls; });

    // Setting x to 20 still clamps to 10, but the derived value changes from 5 to 10
    x.set(20);
    CHECK(calls == 1);
    CHECK(clamped.get() == 10);

    // Setting x to 30 still clamps to 10 — no change, no callback
    x.set(30);
    CHECK(calls == 1);
}

TEST_CASE("Derived observe() works fire-and-forget") {
    prism::Field<int> x{0};
    prism::core::Derived<int> d{[&] { return x.get() + 1; }, x};

    int observed = -1;
    d.observe([&](const int& v) { observed = v; });

    x.set(9);
    CHECK(observed == 10);
}

TEST_CASE("Derived implicit conversion") {
    prism::Field<int> x{42};
    prism::core::Derived<int> d{[&] { return x.get(); }, x};

    int val = d;
    CHECK(val == 42);
}

TEST_CASE("Derived from multiple source types (Field + State)") {
    prism::Field<int> a{1};
    prism::core::State<int> b{2};
    prism::core::Derived<int> sum{[&] { return a.get() + b.get(); }, a, b};

    CHECK(sum.get() == 3);

    a.set(10);
    CHECK(sum.get() == 12);

    b.set(20);
    CHECK(sum.get() == 30);
}

TEST_CASE("Derived chain: derived from derived") {
    prism::Field<int> x{2};
    prism::core::Derived<int> doubled{[&] { return x.get() * 2; }, x};
    prism::core::Derived<int> quadrupled{[&] { return doubled.get() * 2; }, doubled};

    CHECK(quadrupled.get() == 8);

    x.set(5);
    CHECK(doubled.get() == 10);
    CHECK(quadrupled.get() == 20);
}
