#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/widget_tree.hpp>


TEST_CASE("TabBar default constructs with selected=0") {
    prism::TabBar tb;
    CHECK(tb.selected == 0);
}

TEST_CASE("TabBar equality") {
    prism::TabBar a{.selected = 0};
    prism::TabBar b{.selected = 1};
    CHECK(a == a);
    CHECK(a != b);
}

TEST_CASE("TabBarEditState default constructs") {
    prism::TabBarEditState es;
    CHECK(!es.hovered_tab.has_value());
    CHECK(es.header_x_ranges.empty());
}

TEST_CASE("LayoutKind::Tabs exists") {
    auto kind = prism::LayoutKind::Tabs;
    CHECK(kind != prism::LayoutKind::Default);
}

namespace {
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    return node;
}
}

TEST_CASE("Delegate<TabBar> focus policy is tab_and_click") {
    static_assert(prism::Delegate<prism::TabBar>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<TabBar> records header text") {
    prism::Field<prism::TabBar> field{{.selected = 0}};
    auto node = make_node();
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"Alpha", "Beta"});
    node.tab_names = names;
    prism::DrawList dl;
    prism::Delegate<prism::TabBar>::record(dl, field, node);

    CHECK(!dl.commands.empty());

    auto* es = std::get_if<prism::TabBarEditState>(&node.edit_state);
    REQUIRE(es);
    CHECK(es->header_x_ranges.size() == 2);
}

TEST_CASE("tabs_handle_input: Left/Right switches tabs") {
    prism::Field<prism::TabBar> field{{.selected = 0}};
    auto node = make_node({.focused = true});
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"A", "B", "C"});
    node.tab_names = names;

    prism::DrawList dl;
    prism::Delegate<prism::TabBar>::record(dl, field, node);

    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(field.get().selected == 1);

    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 0);

    // Left wraps to last
    prism::Delegate<prism::TabBar>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 2);
}

TEST_CASE("ViewBuilder tabs() creates tree with two tabs") {
    struct Model {
        prism::Field<prism::TabBar> tabs;
        prism::Field<std::string> page_a{"hello"};
        prism::Field<bool> page_b{true};

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.tabs(tabs, [&] {
                vb.tab("Alpha", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page_a); });
                vb.tab("Beta", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page_b); });
            });
        }
    };

    Model model;
    prism::WidgetTree tree(model);

    CHECK(!tree.focus_order().empty());

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() >= 2);
}
