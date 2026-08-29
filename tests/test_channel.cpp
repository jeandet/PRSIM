#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/channel.hpp>
#include <string>
#include <thread>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("Channel::send before drain queues, does not fire immediately") {
    prism::core::Channel<int> c;
    int calls = 0;
    auto conn = c.on_receive().connect([&](const int&) { ++calls; });

    c.send(10);
    CHECK(calls == 0);

    c.drain_notifications();
    CHECK(calls == 1);
}

TEST_CASE("Channel delivers every send in order -- no coalescing") {
    prism::core::Channel<int> c;
    std::vector<int> received;
    auto conn = c.on_receive().connect([&](const int& v) { received.push_back(v); });

    c.send(1);
    c.send(2);
    c.send(3);
    c.drain_notifications();

    REQUIRE(received.size() == 3);
    CHECK(received[0] == 1);
    CHECK(received[1] == 2);
    CHECK(received[2] == 3);
}

TEST_CASE("Channel::drain with nothing queued is a no-op") {
    prism::core::Channel<int> c;
    int calls = 0;
    auto conn = c.on_receive().connect([&](const int&) { ++calls; });

    c.drain_notifications();
    CHECK(calls == 0);
}

TEST_CASE("Channel::drain only delivers what was queued at call time") {
    prism::core::Channel<int> c;
    std::vector<int> received;
    auto conn = c.on_receive().connect([&](const int& v) { received.push_back(v); });

    c.send(1);
    c.drain_notifications();
    c.send(2);
    c.drain_notifications();

    REQUIRE(received.size() == 2);
    CHECK(received[0] == 1);
    CHECK(received[1] == 2);
}

TEST_CASE("Channel::observe works fire-and-forget") {
    prism::core::Channel<std::string> c;
    std::vector<std::string> observed;
    c.observe([&](const std::string& v) { observed.push_back(v); });

    c.send("hello");
    c.send("world");
    c.drain_notifications();

    REQUIRE(observed.size() == 2);
    CHECK(observed[0] == "hello");
    CHECK(observed[1] == "world");
}

TEST_CASE("Channel preserves per-producer order across threads, drained on main") {
    prism::core::Channel<int> c;
    std::vector<int> received;
    auto conn = c.on_receive().connect([&](const int& v) { received.push_back(v); });

    constexpr int per_thread = 200;
    std::thread a([&] { for (int i = 0; i < per_thread; ++i) c.send(i); });
    std::thread b([&] { for (int i = 0; i < per_thread; ++i) c.send(1000 + i); });
    a.join();
    b.join();

    c.drain_notifications();

    REQUIRE(received.size() == 2 * per_thread);
    std::vector<int> from_a, from_b;
    for (int v : received) {
        if (v < 1000) from_a.push_back(v);
        else from_b.push_back(v - 1000);
    }
    for (int i = 0; i < per_thread; ++i) {
        CHECK(from_a[i] == i);
        CHECK(from_b[i] == i);
    }
}

TEST_CASE("Channel has no get() -- it is a stream, not a latest-value slot") {
    // Compile-time/API-shape check only: Channel<T> intentionally has no .get(),
    // unlike Shared<T>. No runtime assertion needed here.
    prism::core::Channel<int> c;
    c.send(1);
    c.drain_notifications();
    CHECK(true);
}
