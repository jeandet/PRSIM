#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/app/widget_tree.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {} namespace prism::ui {} namespace prism::app {} namespace prism::plot {} namespace prism { using namespace core; using namespace render; using namespace input; using namespace ui; using namespace app; using namespace plot; }


enum class Color { Red, Green, Blue };
enum class Size { Small, Medium, Large, XLarge };

namespace {
prism::Theme test_theme;
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    node.theme = &test_theme;
    return node;
}
prism::Point P(float x, float y) { return {prism::X{x}, prism::Y{y}}; }
}

TEST_CASE("ScopedEnum concept matches scoped enums") {
    static_assert(prism::ScopedEnum<Color>);
    static_assert(prism::ScopedEnum<Size>);
    static_assert(!prism::ScopedEnum<int>);
    static_assert(!prism::ScopedEnum<std::string>);
}

TEST_CASE("enum_count returns number of enumerators") {
    CHECK(prism::enum_count<Color>() == 3);
    CHECK(prism::enum_count<Size>() == 4);
}

TEST_CASE("enum_label returns enumerator name") {
    CHECK(prism::enum_label<Color>(0) == "Red");
    CHECK(prism::enum_label<Color>(1) == "Green");
    CHECK(prism::enum_label<Color>(2) == "Blue");
}

TEST_CASE("enum_index returns index for enum value") {
    CHECK(prism::enum_index(Color::Red) == 0);
    CHECK(prism::enum_index(Color::Green) == 1);
    CHECK(prism::enum_index(Color::Blue) == 2);
}

TEST_CASE("enum_from_index returns enum value for index") {
    CHECK(prism::enum_from_index<Color>(0) == Color::Red);
    CHECK(prism::enum_from_index<Color>(1) == Color::Green);
    CHECK(prism::enum_from_index<Color>(2) == Color::Blue);
}

TEST_CASE("enum_index and enum_from_index round-trip") {
    for (size_t i = 0; i < prism::enum_count<Size>(); ++i) {
        CHECK(prism::enum_index(prism::enum_from_index<Size>(i)) == i);
    }
}

TEST_CASE("Dropdown sentinel default-constructs") {
    prism::Dropdown<Color> dd;
    CHECK(dd.value == Color::Red);
    CHECK(dd.labels.empty());
}

TEST_CASE("Dropdown sentinel equality") {
    prism::Dropdown<Color> a{.value = Color::Green};
    prism::Dropdown<Color> b{.value = Color::Green};
    prism::Dropdown<Color> c{.value = Color::Blue};
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("DropdownEditState defaults to closed") {
    prism::DropdownEditState es;
    CHECK(es.open == false);
    CHECK(es.highlighted == 0);
}

TEST_CASE("Delegate<ScopedEnum> has tab_and_click focus policy") {
    CHECK(prism::Delegate<Color>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<Dropdown<T>> has tab_and_click focus policy") {
    CHECK(prism::Delegate<prism::Dropdown<Color>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Enum delegate record produces background and current label") {
    prism::Field<Color> field{Color::Green};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);

    CHECK_FALSE(dl.empty());
    bool has_label = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Green") has_label = true;
        }
    }
    CHECK(has_label);
}

TEST_CASE("Enum delegate record shows arrow indicator") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);

    bool has_arrow = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "\xe2\x96\xbe") has_arrow = true;
        }
    }
    CHECK(has_arrow);
}

TEST_CASE("Enum delegate renders focus ring when focused") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::record(dl, field, node);

    bool has_outline = false;
    for (auto& cmd : dl.commands) {
        if (std::holds_alternative<prism::RectOutline>(cmd)) has_outline = true;
    }
    CHECK(has_outline);
}

TEST_CASE("Enum delegate open popup produces overlay draws") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open the dropdown
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);

    // Re-record to populate overlay
    prism::DrawList dl;
    node.overlay_draws.clear();
    prism::Delegate<Color>::record(dl, field, node);

    CHECK_FALSE(node.overlay_draws.empty());

    // Should have text for each enum option
    int option_count = 0;
    for (auto& cmd : node.overlay_draws.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Red" || t->text == "Green" || t->text == "Blue")
                option_count++;
        }
    }
    CHECK(option_count == 3);
}

TEST_CASE("Enum delegate closed popup has no overlay draws") {
    prism::Field<Color> field{Color::Red};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<Color>::record(dl, field, node);
    CHECK(node.overlay_draws.empty());
}

TEST_CASE("Enum delegate click opens popup") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == true);
    CHECK(es.highlighted == 0);  // Red is index 0
}

TEST_CASE("Enum delegate click opens with current selection highlighted") {
    prism::Field<Color> field{Color::Blue};
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.highlighted == 2);  // Blue is index 2
}

TEST_CASE("Enum delegate Space opens popup") {
    prism::Field<Color> field{Color::Green};
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == true);
    CHECK(es.highlighted == 1);
}

TEST_CASE("Enum delegate Enter opens popup") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == true);
}

TEST_CASE("Enum delegate Down navigates when open") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.highlighted == 0);

    // Down
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(es.highlighted == 1);

    // Down again
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(es.highlighted == 2);

    // Down wraps
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(es.highlighted == 0);
}

TEST_CASE("Enum delegate Up navigates when open") {
    prism::Field<Color> field{Color::Blue};
    auto node = make_node({.focused = true});

    // Open
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.highlighted == 2);

    // Up
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::up, 0}, node);
    CHECK(es.highlighted == 1);

    // Up wraps
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::up, 0}, node);
    CHECK(es.highlighted == 0);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::up, 0}, node);
    CHECK(es.highlighted == 2);
}

TEST_CASE("Enum delegate Enter selects highlighted option") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open, navigate to Blue (index 2), select
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::enter, 0}, node);

    CHECK(field.get() == Color::Blue);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == false);
}

TEST_CASE("Enum delegate Escape closes without changing") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::space, 0}, node);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::escape, 0}, node);

    CHECK(field.get() == Color::Red);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == false);
}

TEST_CASE("Enum delegate click on option selects and closes") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);

    // Click on second option (Green, index 1)
    // y = widget_h + 1 * option_h + half = 30 + 28 + 14 = mid of Green row
    float click_y = 30.f + 1 * 28.f + 14.f;
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, click_y), 1, true}, node);

    CHECK(field.get() == Color::Green);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == false);
}

TEST_CASE("Enum delegate click outside popup closes without changing") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    // Open
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);

    // Click outside popup area (below all options: y = 30 + 3*28 + 10 = 124)
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 124), 1, true}, node);

    CHECK(field.get() == Color::Red);
    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open == false);
}

TEST_CASE("Enum delegate Up/Down quick-select when closed") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});

    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(field.get() == Color::Green);

    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(field.get() == Color::Blue);

    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::down, 0}, node);
    CHECK(field.get() == Color::Red);  // wraps

    prism::Delegate<Color>::handle_input(field, prism::KeyPress{prism::keys::up, 0}, node);
    CHECK(field.get() == Color::Blue);  // wraps back
}

TEST_CASE("Dropdown<T> renders custom labels") {
    prism::Field<prism::Dropdown<Color>> field{{
        .value = Color::Green,
        .labels = {"Rouge", "Vert", "Bleu"}
    }};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Dropdown<Color>>::record(dl, field, node);

    bool has_custom_label = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Vert") has_custom_label = true;
        }
    }
    CHECK(has_custom_label);
}

TEST_CASE("Dropdown<T> popup shows custom labels") {
    prism::Field<prism::Dropdown<Color>> field{{
        .value = Color::Red,
        .labels = {"Rouge", "Vert", "Bleu"}
    }};
    auto node = make_node({.focused = true});

    // Open
    prism::Delegate<prism::Dropdown<Color>>::handle_input(
        field, prism::MouseButton{P(10, 10), 1, true}, node);

    // Re-record to populate overlay
    prism::DrawList dl;
    node.overlay_draws.clear();
    prism::Delegate<prism::Dropdown<Color>>::record(dl, field, node);

    bool has_rouge = false, has_vert = false, has_bleu = false;
    for (auto& cmd : node.overlay_draws.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Rouge") has_rouge = true;
            if (t->text == "Vert") has_vert = true;
            if (t->text == "Bleu") has_bleu = true;
        }
    }
    CHECK(has_rouge);
    CHECK(has_vert);
    CHECK(has_bleu);
}

TEST_CASE("Dropdown<T> falls back to reflection labels when labels empty") {
    prism::Field<prism::Dropdown<Color>> field{{.value = Color::Green}};
    prism::DrawList dl;
    auto node = make_node();
    prism::Delegate<prism::Dropdown<Color>>::record(dl, field, node);

    bool has_reflection_label = false;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<prism::TextCmd>(&cmd)) {
            if (t->text == "Green") has_reflection_label = true;
        }
    }
    CHECK(has_reflection_label);
}

TEST_CASE("Dropdown<T> selection updates value") {
    prism::Field<prism::Dropdown<Color>> field{{.value = Color::Red}};
    auto node = make_node({.focused = true});

    // Open, navigate to Blue, select
    prism::Delegate<prism::Dropdown<Color>>::handle_input(
        field, prism::KeyPress{prism::keys::space, 0}, node);
    prism::Delegate<prism::Dropdown<Color>>::handle_input(
        field, prism::KeyPress{prism::keys::down, 0}, node);
    prism::Delegate<prism::Dropdown<Color>>::handle_input(
        field, prism::KeyPress{prism::keys::down, 0}, node);
    prism::Delegate<prism::Dropdown<Color>>::handle_input(
        field, prism::KeyPress{prism::keys::enter, 0}, node);

    CHECK(field.get().value == Color::Blue);
}

struct DropdownModel {
    prism::Field<Color> color{Color::Red};
    prism::Field<bool> flag{false};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(color, flag);
    }
};

TEST_CASE("Enum dropdown in WidgetTree creates focusable leaf") {
    DropdownModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.leaf_count() == 2);

    auto focus = tree.focus_order();
    REQUIRE(focus.size() == 2);
}

TEST_CASE("Enum dropdown dispatch selects value") {
    DropdownModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    // Quick-select with Down key
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::down, 0});
    CHECK(model.color.get() == Color::Green);
}

TEST_CASE("Open dropdown produces overlay in snapshot") {
    DropdownModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    // Open dropdown
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::space, 0});

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK_FALSE(snap->overlay.empty());
}

TEST_CASE("Closed dropdown has empty overlay in snapshot") {
    DropdownModel model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap->overlay.empty());
}

TEST_CASE("hit_test on overlay popup returns dropdown widget, not widget below") {
    DropdownModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    auto dropdown_id = focus[0];  // Color dropdown
    auto checkbox_id = focus[1];  // bool checkbox

    tree.set_focused(dropdown_id);

    // Open the dropdown popup
    tree.dispatch(dropdown_id, prism::KeyPress{prism::keys::space, 0});

    auto snap = tree.build_snapshot(800, 600, 1);
    REQUIRE_FALSE(snap->overlay.empty());

    // Find the dropdown and checkbox geometry
    std::optional<prism::Rect> dd_rect, cb_rect;
    for (auto& [id, rect] : snap->geometry) {
        if (id == dropdown_id) dd_rect = rect;
        if (id == checkbox_id) cb_rect = rect;
    }
    REQUIRE(dd_rect.has_value());
    REQUIRE(cb_rect.has_value());

    // The popup starts at dd_rect.y + 30 (dd_widget_h) and extends down,
    // overlapping the checkbox below. Click in the popup area.
    float popup_y = dd_rect->origin.y.raw() + 30.f + 14.f;  // middle of first option
    float popup_x = dd_rect->origin.x.raw() + 10.f;

    // This point should be within the checkbox's geometry too
    CHECK(cb_rect->contains(P(popup_x, popup_y)));

    // But hit_test should return the dropdown (overlay takes priority)
    auto hit = prism::hit_test(*snap, P(popup_x, popup_y));
    REQUIRE(hit.has_value());
    CHECK(*hit == dropdown_id);
}

TEST_CASE("Dropdown popup flips upward when near bottom of viewport") {
    // Place dropdown near the bottom: viewport_height=100, widget at y=80
    // Popup for 3 items = 3*28 = 84px, would extend to 80+30+84 = 194 > 100
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});
    node.viewport_height = prism::Height{100};
    node.absolute_y = prism::Y{80};

    // Open dropdown
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);

    // Record to position popup
    prism::DrawList dl;
    prism::Delegate<Color>::record(dl, field, node);

    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.open);
    // Popup should flip above: y = -popup_h = -84
    CHECK(es.popup_rect.origin.y.raw() < 0);

    // Click on second option (Green) in flipped popup
    float click_y = es.popup_rect.origin.y.raw() + 1 * 28.f + 14.f;
    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, click_y), 1, true}, node);
    CHECK(field.get() == Color::Green);
}

TEST_CASE("Dropdown popup opens below when enough room") {
    prism::Field<Color> field{Color::Red};
    auto node = make_node({.focused = true});
    node.viewport_height = prism::Height{600};
    node.absolute_y = prism::Y{10};

    prism::Delegate<Color>::handle_input(field, prism::MouseButton{P(10, 10), 1, true}, node);

    prism::DrawList dl;
    prism::Delegate<Color>::record(dl, field, node);

    auto& es = std::get<prism::DropdownEditState>(node.edit_state);
    CHECK(es.popup_rect.origin.y.raw() > 0);  // below the widget
}

TEST_CASE("close_overlays resets open dropdown") {
    DropdownModel model;
    prism::WidgetTree tree(model);
    auto focus = tree.focus_order();
    tree.set_focused(focus[0]);

    // Open
    tree.dispatch(focus[0], prism::KeyPress{prism::keys::space, 0});
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK_FALSE(snap1->overlay.empty());

    // Close overlays
    tree.close_overlays();
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2->overlay.empty());
}
