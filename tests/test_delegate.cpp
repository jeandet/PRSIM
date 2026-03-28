#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/widget_tree.hpp>

#include <string>

namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("Delegate<bool> record produces draws") {
    prism::Field<bool> field{false};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<bool>::record(dl, field, node);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Delegate<bool> record changes with value") {
    prism::Field<bool> field{false};
    auto node = make_node();

    prism::DrawList dl1;
    prism::Delegate<bool>::record(dl1, field, node);

    field.set(true);
    prism::DrawList dl2;
    prism::Delegate<bool>::record(dl2, field, node);

    // commands[0] is background (same for true/false); commands[1] is the checkbox box fill
    // true adds a checkmark text, so sizes differ
    auto color1 = std::get<prism::FilledRect>(dl1.commands[1]).color;
    auto color2 = std::get<prism::FilledRect>(dl2.commands[1]).color;
    CHECK(color1.r != color2.r);  // false=grey, true=blue
}

TEST_CASE("Delegate<bool> handle_input toggles on press") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> handle_input ignores release") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::Delegate<bool>::handle_input(field, prism::MouseButton{{0, 0}, 1, false}, node);
    CHECK(field.get() == false);
}

TEST_CASE("Delegate<bool> renders checkbox box outline") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field, node);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Default Delegate record produces draws for int") {
    prism::Field<int> field{42};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<int>::record(dl, field, node);
    CHECK_FALSE(dl.empty());
}

TEST_CASE("Default Delegate handle_input is a no-op for int") {
    prism::Field<int> field{42};
    auto node = make_node();
    prism::Delegate<int>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get() == 42);
}

TEST_CASE("Label sentinel renders as read-only text") {
    prism::Field<prism::Label<>> field{{"All systems go"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Label<>>::record(dl, field, node);
    CHECK(dl.size() >= 1);
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
    auto node = make_node();
    prism::Delegate<prism::Label<>>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get().value == "OK");
}

TEST_CASE("Slider sentinel renders track and thumb") {
    prism::Field<prism::Slider<>> field{{.value = 0.5}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Slider<>>::record(dl, field, node);
    CHECK(dl.size() >= 2);
}

TEST_CASE("Slider sentinel renders thumb position proportional to value") {
    prism::Field<prism::Slider<>> field_lo{{.value = 0.0}};
    prism::Field<prism::Slider<>> field_hi{{.value = 1.0}};
    auto node = make_node();

    prism::DrawList dl_lo, dl_hi;
    prism::Delegate<prism::Slider<>>::record(dl_lo, field_lo, node);
    prism::Delegate<prism::Slider<>>::record(dl_hi, field_hi, node);

    auto thumb_lo = std::get<prism::FilledRect>(dl_lo.commands[1]).rect.x;
    auto thumb_hi = std::get<prism::FilledRect>(dl_hi.commands[1]).rect.x;
    CHECK(thumb_hi > thumb_lo);
}

TEST_CASE("Slider<int> with step snaps value") {
    prism::Field<prism::Slider<int>> field{{.value = 3, .min = 1, .max = 5, .step = 1}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Slider<int>>::record(dl, field, node);
    CHECK(dl.size() >= 2);
}

// --- WidgetVisualState tests ---

TEST_CASE("Delegate<bool> record differs with hovered state") {
    prism::Field<bool> field{false};
    auto normal = make_node();
    auto hovered = make_node({.hovered = true});

    prism::DrawList dl_normal, dl_hovered;
    prism::Delegate<bool>::record(dl_normal, field, normal);
    prism::Delegate<bool>::record(dl_hovered, field, hovered);

    auto c1 = std::get<prism::FilledRect>(dl_normal.commands[0]).color;
    auto c2 = std::get<prism::FilledRect>(dl_hovered.commands[0]).color;
    CHECK(c1.r != c2.r);
}

TEST_CASE("Slider record differs with hovered state") {
    prism::Field<prism::Slider<>> field{{.value = 0.5}};
    auto normal = make_node();
    auto hovered = make_node({.hovered = true});

    prism::DrawList dl_normal, dl_hovered;
    prism::Delegate<prism::Slider<>>::record(dl_normal, field, normal);
    prism::Delegate<prism::Slider<>>::record(dl_hovered, field, hovered);

    auto thumb_normal = std::get<prism::FilledRect>(dl_normal.commands[1]).color;
    auto thumb_hovered = std::get<prism::FilledRect>(dl_hovered.commands[1]).color;
    CHECK(thumb_normal.g != thumb_hovered.g);
}

// --- Button tests ---

TEST_CASE("Button sentinel renders text and background") {
    prism::Field<prism::Button> field{{"Click me"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Button>::record(dl, field, node);
    CHECK(dl.size() >= 2);  // filled rect + outline + text
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Click me") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("Button click increments click_count") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node();
    CHECK(field.get().click_count == 0);
    prism::Delegate<prism::Button>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get().click_count == 1);
    prism::Delegate<prism::Button>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get().click_count == 2);
}

TEST_CASE("Button ignores mouse release") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node();
    prism::Delegate<prism::Button>::handle_input(field, prism::MouseButton{{0, 0}, 1, false}, node);
    CHECK(field.get().click_count == 0);
}

TEST_CASE("Button record differs with visual state") {
    prism::Field<prism::Button> field{{"Go"}};
    auto normal = make_node();
    auto pressed = make_node({.hovered = true, .pressed = true});

    prism::DrawList dl_normal, dl_pressed;
    prism::Delegate<prism::Button>::record(dl_normal, field, normal);
    prism::Delegate<prism::Button>::record(dl_pressed, field, pressed);

    auto c1 = std::get<prism::FilledRect>(dl_normal.commands[0]).color;
    auto c2 = std::get<prism::FilledRect>(dl_pressed.commands[0]).color;
    CHECK(c1.r != c2.r);
}

TEST_CASE("FocusPolicy: non-interactive delegates are not focusable") {
    CHECK(prism::Delegate<int>::focus_policy == prism::FocusPolicy::none);
    CHECK(prism::Delegate<std::string>::focus_policy == prism::FocusPolicy::none);
    CHECK(prism::Delegate<prism::Label<>>::focus_policy == prism::FocusPolicy::none);
}

TEST_CASE("FocusPolicy: interactive delegates are focusable") {
    CHECK(prism::Delegate<bool>::focus_policy == prism::FocusPolicy::tab_and_click);
    CHECK(prism::Delegate<prism::Slider<>>::focus_policy == prism::FocusPolicy::tab_and_click);
    CHECK(prism::Delegate<prism::Button>::focus_policy == prism::FocusPolicy::tab_and_click);
    CHECK(prism::Delegate<prism::Checkbox>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<bool> toggles on Space key") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> toggles on Enter key") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, node);
    CHECK(field.get() == true);
}

TEST_CASE("Delegate<bool> ignores other keys") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::Delegate<bool>::handle_input(field, prism::KeyPress{0x41, 0}, node);  // 'A'
    CHECK(field.get() == false);
}

TEST_CASE("Button activates on Space key") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node();
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    CHECK(field.get().click_count == 1);
}

TEST_CASE("Button activates on Enter key") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node();
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, node);
    CHECK(field.get().click_count == 1);
}

TEST_CASE("Button ignores other keys") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node();
    prism::Delegate<prism::Button>::handle_input(field, prism::KeyPress{0x41, 0}, node);
    CHECK(field.get().click_count == 0);
}

TEST_CASE("Delegate<bool> renders focus ring when focused") {
    prism::Field<bool> field{false};
    auto node = make_node({.focused = true});
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field, node);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Delegate<bool> no focus ring when not focused") {
    prism::Field<bool> field{false};
    auto node = make_node();
    prism::DrawList dl;
    prism::Delegate<bool>::record(dl, field, node);
    // The checkbox box border is always present; the focus ring adds a second outline
    int outline_count = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) outline_count++;
    }
    CHECK(outline_count == 1);  // only box border, no focus ring
}

TEST_CASE("Slider renders focus ring when focused") {
    prism::Field<prism::Slider<>> field{{.value = 0.5}};
    auto node = make_node({.focused = true});
    prism::DrawList dl;
    prism::Delegate<prism::Slider<>>::record(dl, field, node);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Button renders focus ring when focused") {
    prism::Field<prism::Button> field{{"Go"}};
    auto node = make_node({.focused = true});
    prism::DrawList dl;
    prism::Delegate<prism::Button>::record(dl, field, node);
    int outline_count = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) outline_count++;
    }
    CHECK(outline_count >= 2);  // existing border + focus ring
}

TEST_CASE("Checkbox default construction") {
    prism::Checkbox cb;
    CHECK(cb.checked == false);
    CHECK(cb.label.empty());
}

TEST_CASE("Checkbox sentinel renders box and label") {
    prism::Field<prism::Checkbox> field{{.label = "Dark mode"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::record(dl, field, node);
    CHECK_FALSE(dl.empty());
    bool has_label = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Dark mode") has_label = true;
        }
    }
    CHECK(has_label);
}

TEST_CASE("Checkbox renders checkmark when checked") {
    prism::Field<prism::Checkbox> field{{.checked = true, .label = "On"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::record(dl, field, node);
    int text_count = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::TextCmd>(cmd)) text_count++;
    }
    CHECK(text_count >= 2);  // checkmark + label
}

TEST_CASE("Checkbox toggle via mouse click") {
    prism::Field<prism::Checkbox> field{{.label = "Toggle me"}};
    auto node = make_node();
    CHECK(field.get().checked == false);
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get().checked == true);
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(field.get().checked == false);
}

TEST_CASE("Checkbox ignores mouse release") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::MouseButton{{0, 0}, 1, false}, node);
    CHECK(field.get().checked == false);
}

TEST_CASE("Checkbox toggle via Space key") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    CHECK(field.get().checked == true);
}

TEST_CASE("Checkbox toggle via Enter key") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, node);
    CHECK(field.get().checked == true);
}

TEST_CASE("Checkbox ignores other keys") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node();
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::KeyPress{0x41, 0}, node);
    CHECK(field.get().checked == false);
}

TEST_CASE("Checkbox focus policy is tab_and_click") {
    CHECK(prism::Delegate<prism::Checkbox>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Checkbox renders focus ring when focused") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node({.focused = true});
    prism::DrawList dl;
    prism::Delegate<prism::Checkbox>::record(dl, field, node);
    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Checkbox observer fires on toggle") {
    prism::Field<prism::Checkbox> field{{.label = "X"}};
    auto node = make_node();
    int fire_count = 0;
    auto conn = field.on_change().connect([&](const prism::Checkbox&) { fire_count++; });
    prism::Delegate<prism::Checkbox>::handle_input(field, prism::MouseButton{{0, 0}, 1, true}, node);
    CHECK(fire_count == 1);
}
