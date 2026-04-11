#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/shared.hpp>
#include <string>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("Shared stores and retrieves value") {
    prism::core::Shared<int> s{42};
    CHECK(s.get() == 42);
}

TEST_CASE("Shared::set updates value atomically") {
    prism::core::Shared<int> s{0};
    s.set(99);
    CHECK(s.get() == 99);
}

TEST_CASE("Shared::drain_notifications fires on_change on UI thread") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    int last = -1;
    auto conn = s.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    s.set(10);
    // Callback has not fired yet — notification is queued
    CHECK(calls == 0);

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(last == 10);
}

TEST_CASE("Shared coalesces multiple sets into one notification") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    auto conn = s.on_change().connect([&](const int&) { ++calls; });

    s.set(1);
    s.set(2);
    s.set(3);

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(s.get() == 3);
}

TEST_CASE("Shared::drain with no pending notifications is a no-op") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    auto conn = s.on_change().connect([&](const int&) { ++calls; });

    s.drain_notifications();
    CHECK(calls == 0);
}

TEST_CASE("Shared::observe works fire-and-forget") {
    prism::core::Shared<int> s{0};
    int observed = -1;
    s.observe([&](const int& v) { observed = v; });

    s.set(7);
    s.drain_notifications();
    CHECK(observed == 7);
}

TEST_CASE("Shared set from another thread, drain on main") {
    prism::core::Shared<int> s{0};
    int calls = 0;
    int last = -1;
    auto conn = s.on_change().connect([&](const int& v) {
        ++calls;
        last = v;
    });

    std::thread writer([&] {
        s.set(42);
    });
    writer.join();

    s.drain_notifications();
    CHECK(calls == 1);
    CHECK(last == 42);
}

TEST_CASE("Shared has no implicit conversion operator") {
    prism::core::Shared<int> s{5};
    // Must use .get() — no implicit conversion
    // int x = s;  // should not compile
    int x = s.get();
    CHECK(x == 5);
}
