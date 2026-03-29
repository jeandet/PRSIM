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

TEST_CASE("Password default-constructs with empty value") {
    prism::Password<> pw;
    CHECK(pw.value.empty());
    CHECK(pw.placeholder.empty());
    CHECK(pw.max_length == 0);
}

TEST_CASE("Password equality comparison") {
    prism::Password<> a{.value = "secret"};
    prism::Password<> b{.value = "secret"};
    prism::Password<> c{.value = "other"};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("TextEditable concept matches Password") {
    static_assert(prism::TextEditable<prism::Password<>>);
}

TEST_CASE("Delegate<Password<>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::Password<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

// --- record() tests ---

TEST_CASE("Password record renders masked text, not actual value") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_actual_value = false;
    bool has_bullets = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "abc") has_actual_value = true;
            if (t->text.find("\xe2\x97\x8f") != std::string::npos) has_bullets = true;
        }
    }
    CHECK_FALSE(has_actual_value);
    CHECK(has_bullets);
}

TEST_CASE("Password record shows placeholder when empty and unfocused") {
    prism::Field<prism::Password<>> field{{.placeholder = "Enter password"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter password") has_placeholder = true;
        }
    }
    CHECK(has_placeholder);
}

TEST_CASE("Password record hides placeholder when focused") {
    prism::Field<prism::Password<>> field{{.placeholder = "Enter password"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_placeholder = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Enter password") has_placeholder = true;
        }
    }
    CHECK_FALSE(has_placeholder);
}

TEST_CASE("Password record shows cursor when focused") {
    prism::Field<prism::Password<>> field{{.value = "hi"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    int thin_rects = 0;
    for (auto& cmd : dl.commands) {
        if (auto* fr = std::get_if<prism::FilledRect>(&cmd)) {
            if (fr->rect.extent.w.raw() <= 3.f) thin_rects++;
        }
    }
    CHECK(thin_rects >= 1);
}

TEST_CASE("Password record uses clip_push/pop") {
    prism::Field<prism::Password<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    int clips = 0;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::ClipPush>(cmd)) clips++;
        if (std::holds_alternative<prism::ClipPop>(cmd)) clips++;
    }
    CHECK(clips == 2);
}

TEST_CASE("Password record renders focus ring when focused") {
    prism::Field<prism::Password<>> field{{.value = "test"}};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<prism::Password<>>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

// --- handle_input() tests ---

TEST_CASE("Password TextInput inserts text") {
    prism::Field<prism::Password<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "aXb");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("Password backspace deletes char before cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("Password arrow keys move cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("Password Home/End move cursor") {
    prism::Field<prism::Password<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);

    prism::Delegate<prism::Password<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("Password max_length enforced") {
    prism::Field<prism::Password<>> field{{.value = "ab", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::Password<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::Password<>>::handle_input(field, prism::TextInput{"XY"}, node);
    CHECK(field.get().value == "abX");
}

// --- WidgetTree integration ---

struct PasswordModel {
    prism::Field<prism::Password<>> secret{{.value = "pass"}};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(secret);
        vb.widget(flag);
    }
};

TEST_CASE("Password in WidgetTree creates focusable leaf") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    CHECK(focus.size() == 2);
}

TEST_CASE("Password dispatch TextInput updates field value") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::TextInput{"!"});
    CHECK(model.secret.get().value == "pass!");
}

TEST_CASE("Password snapshot renders masked text") {
    PasswordModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);

    bool has_actual = false;
    bool has_bullets = false;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
                if (t->text == "pass") has_actual = true;
                if (t->text.find("\xe2\x97\x8f") != std::string::npos) has_bullets = true;
            }
        }
    }
    CHECK_FALSE(has_actual);
    CHECK(has_bullets);
}
