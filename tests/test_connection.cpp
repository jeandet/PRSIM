#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/connection.hpp>

TEST_CASE("SenderHub notifies connected receiver") {
    prism::SenderHub<int> hub;
    int received = -1;
    auto conn = hub.connect([&](int v) { received = v; });
    hub.emit(42);
    CHECK(received == 42);
}

TEST_CASE("SenderHub notifies multiple receivers") {
    prism::SenderHub<int> hub;
    int a = 0, b = 0;
    auto c1 = hub.connect([&](int v) { a = v; });
    auto c2 = hub.connect([&](int v) { b = v; });
    hub.emit(7);
    CHECK(a == 7);
    CHECK(b == 7);
}

TEST_CASE("Connection disconnects on destruction") {
    prism::SenderHub<int> hub;
    int received = 0;
    {
        auto conn = hub.connect([&](int v) { received = v; });
        hub.emit(1);
        CHECK(received == 1);
    }
    hub.emit(2);
    CHECK(received == 1);
}

TEST_CASE("Connection is move-only") {
    prism::SenderHub<int> hub;
    int received = 0;
    auto c1 = hub.connect([&](int v) { received = v; });
    auto c2 = std::move(c1);
    hub.emit(5);
    CHECK(received == 5);
}

TEST_CASE("Connection disconnect is idempotent") {
    prism::SenderHub<int> hub;
    auto conn = hub.connect([](int) {});
    conn.disconnect();
    conn.disconnect();
}

TEST_CASE("SenderHub with no args") {
    prism::SenderHub<> hub;
    int calls = 0;
    auto conn = hub.connect([&] { ++calls; });
    hub.emit();
    hub.emit();
    CHECK(calls == 2);
}

TEST_CASE("pipe: hub | prism::then(f) fires on emit") {
    prism::SenderHub<int> hub;
    int received = -1;
    auto conn = hub | prism::then([&](int v) { received = v; });
    hub.emit(42);
    CHECK(received == 42);
}

TEST_CASE("pipe: Connection disconnects on destruction") {
    prism::SenderHub<int> hub;
    int received = 0;
    {
        auto conn = hub | prism::then([&](int v) { received = v; });
        hub.emit(1);
        CHECK(received == 1);
    }
    hub.emit(2);
    CHECK(received == 1);
}

TEST_CASE("pipe: multiple pipes on same hub") {
    prism::SenderHub<int> hub;
    int a = 0, b = 0;
    auto c1 = hub | prism::then([&](int v) { a = v; });
    auto c2 = hub | prism::then([&](int v) { b = v; });
    hub.emit(7);
    CHECK(a == 7);
    CHECK(b == 7);
}

TEST_CASE("Receiver can disconnect itself during emit") {
    prism::SenderHub<> hub;
    int calls = 0;
    prism::Connection conn;
    conn = hub.connect([&] {
        ++calls;
        conn.disconnect();
    });
    hub.emit();
    hub.emit();
    CHECK(calls == 1);
}
