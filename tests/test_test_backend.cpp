#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/test_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

namespace {
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

TEST_CASE("TestBackend fires events then WindowClose") {
    std::vector<prism::InputEvent> events = {
        prism::MouseButton{P(100, 50), 1, true},
        prism::KeyPress{42, 0},
    };
    prism::TestBackend tb{events};
    tb.create_window({});
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 3);
    CHECK(std::holds_alternative<prism::MouseButton>(received[0]));
    CHECK(std::holds_alternative<prism::KeyPress>(received[1]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[2]));
}

TEST_CASE("TestBackend with no events fires only WindowClose") {
    prism::TestBackend tb{{}};
    tb.create_window({});
    std::vector<prism::InputEvent> received;

    tb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("TestBackend works through Backend wrapper") {
    std::vector<prism::InputEvent> events = {
        prism::MouseMove{P(10, 20)},
    };
    auto backend = prism::Backend{std::make_unique<prism::TestBackend>(events)};
    backend.create_window({});
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 2);
    CHECK(std::holds_alternative<prism::MouseMove>(received[0]));
    CHECK(std::holds_alternative<prism::WindowClose>(received[1]));
}
