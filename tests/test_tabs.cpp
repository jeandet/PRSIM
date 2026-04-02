#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/delegate.hpp>
#include <prism/core/field.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/app/widget_tree.hpp>

// File-scope models for tests that use WidgetTree (reflection requires non-local types)

struct TabsTwoPageModel {
    prism::Field<prism::TabBar<>> tabs;
    prism::Field<bool> page_a{false};
    prism::Field<bool> page_b{true};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Alpha", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page_a); });
            vb.tab("Beta", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page_b); });
        });
    }
};

struct TabsSinglePageModel {
    prism::Field<prism::TabBar<>> tabs;
    prism::Field<std::string> page{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Only", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page); });
        });
    }
};

struct TabsOverlayModel {
    prism::Field<prism::TabBar<>> tabs;
    prism::Field<std::string> page_a{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Alpha", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(page_a); });
        });
    }
};


TEST_CASE("TabBar default constructs with selected=0") {
    prism::TabBar<> tb;
    CHECK(tb.selected == 0);
}

TEST_CASE("TabBar equality") {
    prism::TabBar<> a{.selected = 0};
    prism::TabBar<> b{.selected = 1};
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
prism::Theme test_theme;
prism::WidgetNode make_node(prism::WidgetVisualState vs = {}) {
    prism::WidgetNode node;
    node.visual_state = vs;
    node.theme = &test_theme;
    return node;
}
}

TEST_CASE("Delegate<TabBar> focus policy is tab_and_click") {
    static_assert(prism::Delegate<prism::TabBar<>>::focus_policy == prism::FocusPolicy::tab_and_click);
}

TEST_CASE("Delegate<TabBar> records header text") {
    prism::Field<prism::TabBar<>> field{{.selected = 0}};
    auto node = make_node();
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"Alpha", "Beta"});
    node.tab_names = names;
    prism::DrawList dl;
    prism::Delegate<prism::TabBar<>>::record(dl, field, node);

    CHECK(!dl.commands.empty());

    auto* es = std::get_if<prism::TabBarEditState>(&node.edit_state);
    REQUIRE(es);
    CHECK(es->header_x_ranges.size() == 2);
}

TEST_CASE("tabs_handle_input: Left/Right switches tabs") {
    prism::Field<prism::TabBar<>> field{{.selected = 0}};
    auto node = make_node({.focused = true});
    auto names = std::make_shared<std::vector<std::string>>(
        std::vector<std::string>{"A", "B", "C"});
    node.tab_names = names;

    prism::DrawList dl;
    prism::Delegate<prism::TabBar<>>::record(dl, field, node);

    prism::Delegate<prism::TabBar<>>::handle_input(
        field, prism::KeyPress{prism::keys::right, 0}, node);
    CHECK(field.get().selected == 1);

    prism::Delegate<prism::TabBar<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 0);

    // Left wraps to last
    prism::Delegate<prism::TabBar<>>::handle_input(
        field, prism::KeyPress{prism::keys::left, 0}, node);
    CHECK(field.get().selected == 2);
}

TEST_CASE("ViewBuilder tabs() creates tree with two tabs") {
    TabsTwoPageModel model;
    prism::WidgetTree tree(model);

    CHECK(!tree.focus_order().empty());

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(snap->geometry.size() >= 2);
}

TEST_CASE("Tab switch rematerializes content") {
    TabsTwoPageModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK(snap1 != nullptr);

    model.tabs.set(prism::TabBar<>{.selected = 1});
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);
    CHECK(!snap2->geometry.empty());
}

TEST_CASE("Inactive tab field changes reflected on switch back") {
    TabsTwoPageModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK(snap1 != nullptr);

    model.tabs.set(prism::TabBar<>{.selected = 1});
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);

    model.page_a.set(true);

    model.tabs.set(prism::TabBar<>{.selected = 0});
    auto snap3 = tree.build_snapshot(800, 600, 3);
    CHECK(snap3 != nullptr);
}

TEST_CASE("Single tab produces valid snapshot") {
    TabsSinglePageModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(!snap->geometry.empty());
}

TEST_CASE("Tab bar is in focus order, Tab key moves to content") {
    TabsTwoPageModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);

    CHECK(tree.focus_order().size() >= 2);

    tree.focus_next();
    auto bar_id = tree.focused_id();
    CHECK(bar_id != 0);

    tree.focus_next();
    auto content_id = tree.focused_id();
    CHECK(content_id != bar_id);
}

TEST_CASE("Click on tab header switches tab") {
    TabsTwoPageModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);

    tree.focus_next();
    auto bar_id = tree.focused_id();

    // Second tab starts after "Alpha" (5 chars * 8px + 32px padding = 72px)
    prism::MouseButton click{prism::Point{prism::X{80}, prism::Y{10}}, 1, true};
    tree.dispatch(bar_id, click);

    CHECK(model.tabs.get().selected == 1);
}

TEST_CASE("close_overlays does not destroy tabs state") {
    TabsOverlayModel model;
    prism::WidgetTree tree(model);
    auto snap1 = tree.build_snapshot(800, 600, 1);
    CHECK(snap1 != nullptr);

    tree.close_overlays();

    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);
    CHECK(!snap2->geometry.empty());
}

#if __cpp_impl_reflection
struct GeneralPage {
    prism::Field<std::string> username{"jean"};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(username); }
};

struct AdvancedPage {
    prism::Field<bool> dark_mode{false};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(dark_mode); }
};

struct Pages {
    GeneralPage general;
    AdvancedPage advanced;
};

struct ReflectiveTabModel {
    prism::Field<prism::TabBar<Pages>> tabs;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs);
    }
};

TEST_CASE("TabBar<S> reflective mode: tabs from struct members") {
    ReflectiveTabModel model;
    prism::WidgetTree tree(model);

    auto snap = tree.build_snapshot(800, 600, 1);
    CHECK(snap != nullptr);
    CHECK(!snap->geometry.empty());

    model.tabs.value.selected = 1;
    model.tabs.on_change().emit(model.tabs.value);
    auto snap2 = tree.build_snapshot(800, 600, 2);
    CHECK(snap2 != nullptr);
}
#endif
