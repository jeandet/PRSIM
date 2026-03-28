#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>

#include <string>

TEST_CASE("Delegate<bool> record produces draws") {
    prism::Field<bool> field{false};
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Delegate<bool> record changes with value") {
    prism::Field<bool> field{false};

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
    prism::Field<bool> field{false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> handle_input ignores release") {
    prism::Field<bool> field{false};
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, false});
    CHECK(field.get() == false);
}

TEST_CASE("Default Delegate record produces draws for int") {
    prism::Field<int> field{42};
    prism::DrawList dl;
    prism::Delegate<int>::record(dl, field);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Default Delegate handle_input is a no-op for int") {
    prism::Field<int> field{42};
    prism::Delegate<int>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get() == 42);
}

TEST_CASE("Label sentinel renders as read-only text") {
    prism::Field<prism::Label<>> field{{"All systems go"}};
    prism::DrawList dl;
    prism::Delegate<prism::Label<>>::record(dl, field);
    CHECK(dl.size() >= 1);
    // Should contain a TextCmd with the label's value
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "All systems go") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("Label sentinel ignores input") {
    prism::Field<prism::Label<>> field{{"OK"}};
    prism::Delegate<prism::Label<>>::handle_input(field, prism::MouseButton{{0, 0}, 1, true});
    CHECK(field.get().value == "OK");  // unchanged
}

TEST_CASE("Slider sentinel renders track and thumb") {
    prism::Field<prism::Slider<>> field{{.value = 0.5}};
    prism::DrawList dl;
    prism::Delegate<prism::Slider<>>::record(dl, field);
    // Expect at least: track rect, thumb rect
    CHECK(dl.size() >= 2);
}

TEST_CASE("Slider sentinel renders thumb position proportional to value") {
    prism::Field<prism::Slider<>> field_lo{{.value = 0.0}};
    prism::Field<prism::Slider<>> field_hi{{.value = 1.0}};

    prism::DrawList dl_lo, dl_hi;
    prism::Delegate<prism::Slider<>>::record(dl_lo, field_lo);
    prism::Delegate<prism::Slider<>>::record(dl_hi, field_hi);

    // Thumb is the second FilledRect — its x position should differ
    auto thumb_lo = std::get<prism::FilledRect>(dl_lo.commands[1]).rect.x;
    auto thumb_hi = std::get<prism::FilledRect>(dl_hi.commands[1]).rect.x;
    CHECK(thumb_hi > thumb_lo);
}

TEST_CASE("Slider<int> with step snaps value") {
    prism::Field<prism::Slider<int>> field{{.value = 3, .min = 1, .max = 5, .step = 1}};
    prism::DrawList dl;
    prism::Delegate<prism::Slider<int>>::record(dl, field);
    CHECK(dl.size() >= 2);
}
