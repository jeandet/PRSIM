#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/null_backend.hpp>
#include <prism/core/backend.hpp>
#include <prism/core/input_event.hpp>

#include <vector>

TEST_CASE("NullBackend fires WindowClose immediately") {
    prism::NullBackend nb;
    std::vector<prism::InputEvent> received;

    nb.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend submit and wake are no-ops") {
    prism::NullBackend nb;
    nb.submit(nullptr);
    nb.wake();
    nb.quit();
    // No crash = pass
}

TEST_CASE("NullBackend works through Backend wrapper") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::InputEvent& ev) {
        received.push_back(ev);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}
