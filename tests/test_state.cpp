#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/state.hpp>

#include <string>

TEST_CASE("State holds and returns a value") {
    prism::State<int> s{42};
    CHECK(s.get() == 42);
}

TEST_CASE("State set updates value") {
    prism::State<int> s{0};
    s.set(10);
    CHECK(s.get() == 10);
}

TEST_CASE("State set triggers on_change") {
    prism::State<std::string> s{"hello"};
    std::string received;
    auto conn = s.on_change().connect([&](const std::string& v) { received = v; });
    s.set("world");
    CHECK(received == "world");
}

TEST_CASE("State set with equal value does not emit") {
    prism::State<int> s{5};
    int call_count = 0;
    auto conn = s.on_change().connect([&](const int&) { ++call_count; });
    s.set(5);
    CHECK(call_count == 0);
}

TEST_CASE("State implicit conversion to const T&") {
    prism::State<int> s{10};
    const int& ref = s;
    CHECK(ref == 10);
}
