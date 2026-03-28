#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/field.hpp>

#include <string>

TEST_CASE("Field stores value") {
    prism::Field<int> f{42};
    CHECK(f.get() == 42);
}

TEST_CASE("Field implicit conversion to const T&") {
    prism::Field<std::string> f{"hello"};
    const std::string& ref = f;
    CHECK(ref == "hello");
}

TEST_CASE("Field::set updates value") {
    prism::Field<int> f{0};
    f.set(10);
    CHECK(f.get() == 10);
}

TEST_CASE("Field::set notifies on_change receivers") {
    prism::Field<int> f{0};
    int received = -1;
    auto conn = f.on_change().connect([&](const int& v) { received = v; });
    f.set(7);
    CHECK(received == 7);
}

TEST_CASE("Field::set does not notify if value unchanged") {
    prism::Field<int> f{5};
    int calls = 0;
    auto conn = f.on_change().connect([&](const int&) { ++calls; });
    f.set(5);
    CHECK(calls == 0);
    f.set(6);
    CHECK(calls == 1);
}

TEST_CASE("Field multiple receivers") {
    prism::Field<std::string> f{"a"};
    std::string r1, r2;
    auto c1 = f.on_change().connect([&](const std::string& v) { r1 = v; });
    auto c2 = f.on_change().connect([&](const std::string& v) { r2 = v; });
    f.set("b");
    CHECK(r1 == "b");
    CHECK(r2 == "b");
}

TEST_CASE("Field connection RAII disconnect") {
    prism::Field<int> f{0};
    int received = 0;
    {
        auto conn = f.on_change().connect([&](const int& v) { received = v; });
        f.set(1);
        CHECK(received == 1);
    }
    f.set(2);
    CHECK(received == 1);
}

TEST_CASE("Field default-constructed value") {
    prism::Field<int> f{};
    CHECK(f.get() == 0);
}

TEST_CASE("Field::observe callback lives as long as field") {
    prism::Field<int> f{0};
    int received = 0;
    f.observe([&](const int& v) { received = v; });
    f.set(42);
    CHECK(received == 42);
    f.set(99);
    CHECK(received == 99);
}

TEST_CASE("Field::observe multiple observers") {
    prism::Field<int> f{0};
    int r1 = 0, r2 = 0;
    f.observe([&](const int& v) { r1 = v; });
    f.observe([&](const int& v) { r2 = v; });
    f.set(5);
    CHECK(r1 == 5);
    CHECK(r2 == 5);
}

TEST_CASE("Field pipe: on_change() | prism::then(f)") {
    prism::Field<int> f{0};
    int received = -1;
    auto conn = f.on_change() | prism::then([&](const int& v) { received = v; });
    f.set(42);
    CHECK(received == 42);
}

TEST_CASE("Field with bool") {
    prism::Field<bool> f{false};
    bool toggled = false;
    auto conn = f.on_change().connect([&](const bool& v) { toggled = v; });
    f.set(true);
    CHECK(toggled == true);
    CHECK(f.get() == true);
}
