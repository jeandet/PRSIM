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
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
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
            if (fr->rect.extent.w.raw() <= 3.f) thin_rects++;
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

// --- handle_input() tests ---

TEST_CASE("TextField TextInput inserts text at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "aXb");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField TextInput appends at end") {
    prism::Field<prism::TextField<>> field{{.value = "hi"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"!"}, node);
    CHECK(field.get().value == "hi!");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField backspace deletes char before cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField backspace at position 0 is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value == "abc");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField delete removes char at cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ac");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField delete at end is no-op") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value == "ab");
}

TEST_CASE("TextField left arrow moves cursor left") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 1);
}

TEST_CASE("TextField left arrow at 0 stays at 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField right arrow moves cursor right") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 1;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField right arrow at end stays at end") {
    prism::Field<prism::TextField<>> field{{.value = "ab"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 2);
}

TEST_CASE("TextField Home moves cursor to 0") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField End moves cursor to end") {
    prism::Field<prism::TextField<>> field{{.value = "abc"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length enforced on insert") {
    prism::Field<prism::TextField<>> field{{.value = "ab", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 2;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"XY"}, node);
    CHECK(field.get().value == "abX");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 3);
}

TEST_CASE("TextField max_length blocks insert when full") {
    prism::Field<prism::TextField<>> field{{.value = "abc", .max_length = 3}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 3;

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"X"}, node);
    CHECK(field.get().value == "abc");
}

// --- WidgetTree integration tests ---

struct TextFieldModel {
    prism::Field<prism::TextField<>> name{{.value = "hi"}};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(name, flag);
    }
};

TEST_CASE("TextField in WidgetTree creates focusable leaf") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 2);
}

TEST_CASE("TextField dispatch TextInput updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::TextInput{"X"});
    CHECK(model.name.get().value == "hiX");
}

TEST_CASE("TextField dispatch KeyPress backspace updates field value") {
    TextFieldModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    tree.dispatch(focus[0], prism::KeyPress{prism::keys::end, 0});
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::backspace, 0});
    CHECK(model.name.get().value == "h");
}

// --- click/cursor tests ---

TEST_CASE("TextField click positions cursor") {
    prism::Field<prism::TextField<>> field{{.value = "abcdef"}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    float cw = prism::char_width(14.f);
    float click_x = 4.f + 2.5f * cw;
    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::MouseButton{P(click_x, 15), 1, true}, node);
    auto cursor = std::any_cast<prism::TextEditState>(node.edit_state).cursor;
    CHECK((cursor == 2 || cursor == 3));
}

TEST_CASE("TextField scroll_offset adjusts when cursor moves past right edge") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::end, 0}, node);

    float cw = prism::char_width(14.f);
    float text_area_w = 200.f - 2 * 4.f;
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset > 0.f);
    float cursor_px = 36 * cw; // end key moves cursor to len=36
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset ==
          doctest::Approx(cursor_px - text_area_w));
}

TEST_CASE("TextField scroll_offset resets when cursor moves to beginning") {
    prism::Field<prism::TextField<>> field{{.value = "abcdefghijklmnopqrstuvwxyz0123456789"}};
    auto node = make_node({.focused = true});
    auto& es = prism::Delegate<prism::TextField<>>::ensure_edit_state(node);
    es.cursor = 35;
    es.scroll_offset = 100.f;

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::home, 0}, node);

    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).scroll_offset == 0.f);
}

TEST_CASE("TextField operations on empty string are safe") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    // All of these should be no-ops on empty string
    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::backspace, 0}, node);
    CHECK(field.get().value.empty());

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::delete_, 0}, node);
    CHECK(field.get().value.empty());

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);

    prism::Delegate<prism::TextField<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 0);
}

TEST_CASE("TextField TextInput on empty field inserts text") {
    prism::Field<prism::TextField<>> field{{.value = ""}};
    auto node = make_node({.focused = true});
    prism::Delegate<prism::TextField<>>::ensure_edit_state(node);

    prism::Delegate<prism::TextField<>>::handle_input(field, prism::TextInput{"hello"}, node);
    CHECK(field.get().value == "hello");
    CHECK(std::any_cast<prism::TextEditState>(node.edit_state).cursor == 5);
}
