#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/null_backend.hpp>
#include <prism/app/backend.hpp>
#include <prism/input/input_event.hpp>

#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {} namespace prism::ui {} namespace prism::app {} namespace prism::plot {} namespace prism { using namespace core; using namespace render; using namespace input; using namespace ui; using namespace app; using namespace plot; }


TEST_CASE("NullBackend fires WindowClose immediately") {
    prism::NullBackend nb;
    nb.create_window({});
    std::vector<prism::InputEvent> received;

    nb.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend submit and wake are no-ops") {
    prism::NullBackend nb;
    auto& window = nb.create_window({});
    nb.submit(window.id(), nullptr);
    nb.wake();
    nb.quit();
}

TEST_CASE("NullBackend works through Backend wrapper") {
    auto backend = prism::Backend{std::make_unique<prism::NullBackend>()};
    backend.create_window({});
    std::vector<prism::InputEvent> received;

    backend.run([&](const prism::WindowEvent& we) {
        received.push_back(we.event);
    });

    REQUIRE(received.size() == 1);
    CHECK(std::holds_alternative<prism::WindowClose>(received[0]));
}

TEST_CASE("NullBackend create_window returns valid window") {
    prism::NullBackend nb;
    auto& window = nb.create_window({.title = "Test", .width = 320, .height = 240});
    CHECK(window.id() == 1);
    auto [w, h] = window.size();
    CHECK(w == 320);
    CHECK(h == 240);
}
