#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>

#include <string>

TEST_CASE("Delegate<bool> record produces draws") {
    prism::Field<bool> field{"Toggle", false};
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Delegate<bool> record changes with value") {
    prism::Field<bool> field{"Toggle", false};

    prism::DrawList dl1;
    prism::Delegate<bool>::record(dl1, field);

    field.set(true);
    prism::DrawList dl2;
    prism::Delegate<bool>::record(dl2, field);

    CHECK(dl1.size() == dl2.size());
    auto color1 = std::get<prism::FilledRect>(dl1.commands[0]).color;
    auto color2 = std::get<prism::FilledRect>(dl2.commands[0]).color;
    CHECK(color1.r != color2.r);  // false=grey, true=teal
}

TEST_CASE("Delegate<bool> handle_input toggles on press") {
    prism::Field<bool> field{"Toggle", false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> handle_input ignores release") {
    prism::Field<bool> field{"Toggle", false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, false});
    CHECK(field.get() == false);
}

TEST_CASE("Default Delegate record produces draws for int") {
    prism::Field<int> field{"Count", 42};
    prism::DrawList dl;
    prism::Delegate<int>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Default Delegate handle_input is a no-op for int") {
    prism::Field<int> field{"Count", 42};
    prism::Delegate<int>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == 42);
}
