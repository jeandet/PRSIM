#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/widget_tree.hpp>

namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("TextField default-constructs with empty value") {
    prism::TextField<> tf;
    CHECK(tf.value.empty());
    CHECK(tf.placeholder.empty());
    CHECK(tf.max_length == 0);
}

TEST_CASE("TextField equality comparison") {
    prism::TextField<> a{.value = "hello"};
    prism::TextField<> b{.value = "hello"};
    prism::TextField<> c{.value = "world"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches TextField") {
    static_assert(prism::TextEditable<prism::TextField<>>);
    static_assert(!prism::TextEditable<std::string>);
    static_assert(!prism::TextEditable<int>);
}

TEST_CASE("char_width returns positive value") {
    CHECK(prism::char_width(14.f) > 0.f);
    CHECK(prism::char_width(14.f) == doctest::Approx(0.6f * 14.f));
}

TEST_CASE("Delegate<TextField<>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::TextField<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

// --- record() tests ---

TEST_CASE("TextField record produces background and text") {
    prism::Field<prism::TextField<>> field{{.value = "hello"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    CHECK(dl.size() >= 3);
    bool has_text = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "hello") has_text = true;
        }
    }
    CHECK(has_text);
}

TEST_CASE("TextField record shows placeholder when empty and unfocused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK(has_placeholder);
}

TEST_CASE("TextField record hides placeholder when focused") {
    prism::Field<prism::TextField<>> field{{.placeholder = "Enter name"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter name") has_placeholder = true;
        }
    }
    CHECK_FALSE(has_placeholder);
}

TEST_CASE("TextField record shows cursor when focused") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    int thin_rects = 0;
    for (auto& cmd : dl.commands) {
        if (auto* fr = std::get_if<prism::FilledRect>(&cmd)) {
            if (fr->rect.w <= 3.f) thin_rects++;
        }
    }
    CHECK(thin_rects >= 1);
}

TEST_CASE("TextField record uses clip_push/pop") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    int clips = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::ClipPush>(cmd)) clips++;
        if (std::holds_alternative<prism::ClipPop>(cmd)) clips++;
    }
    CHECK(clips == 2);
}

TEST_CASE("TextField record renders focus ring when focused") {
    prism::Field<prism::TextField<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

