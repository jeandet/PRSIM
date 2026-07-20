#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>
#include <prism/core/field.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/input/input_event.hpp>
#include <prism/app/widget_tree.hpp>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
prism::Theme test_theme;
prism::WidgetNode make_node() {
    prism::WidgetNode node;
    node.theme = &test_theme;
    return node;
}
}

TEST_CASE("Widget<TreeRow> renders the row's label") {
    prism::TreeRow row;
    row.label = "hello";
    prism::Field<prism::TreeRow> field{row};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<prism::TreeRow>::record(dl, field, node);

    bool found = false;
    for (auto& cmd : dl.commands)
        if (auto* t = std::get_if<prism::TextCmd>(&cmd))
            if (t->text.find("hello") != std::string_view::npos) found = true;
    CHECK(found);
}

TEST_CASE("Widget<TreeRow> shows an expand marker only when has_children is true") {
    prism::TreeRow leaf;
    leaf.label = "leaf";
    leaf.has_children = false;
    prism::Field<prism::TreeRow> leaf_field{leaf};
    prism::DrawList leaf_dl;
    auto leaf_node = make_node();
    prism::Widget<prism::TreeRow>::record(leaf_dl, leaf_field, leaf_node);

    prism::TreeRow branch;
    branch.label = "branch";
    branch.has_children = true;
    branch.expanded = false;
    prism::Field<prism::TreeRow> branch_field{branch};
    prism::DrawList branch_dl;
    auto branch_node = make_node();
    prism::Widget<prism::TreeRow>::record(branch_dl, branch_field, branch_node);

    auto text_of = [](const prism::DrawList& dl) {
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd)) return std::string(t->text);
        return std::string{};
    };
    CHECK(text_of(leaf_dl).find(">") == std::string::npos);
    CHECK(text_of(branch_dl).find(">") != std::string::npos);
}

TEST_CASE("Widget<std::optional<TreeDetail>> shows 'No selection' when empty") {
    prism::Field<std::optional<prism::TreeDetail>> field{std::nullopt};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<std::optional<prism::TreeDetail>>::record(dl, field, node);

    bool found = false;
    for (auto& cmd : dl.commands)
        if (auto* t = std::get_if<prism::TextCmd>(&cmd))
            if (t->text.find("No selection") != std::string_view::npos) found = true;
    CHECK(found);
}

TEST_CASE("Widget<std::optional<TreeDetail>> renders label and attributes when set") {
    prism::TreeDetail detail;
    detail.label = "my-node";
    detail.attributes = {{"size", "42"}, {"kind", "file"}};
    prism::Field<std::optional<prism::TreeDetail>> field{detail};
    prism::DrawList dl;
    auto node = make_node();
    prism::Widget<std::optional<prism::TreeDetail>>::record(dl, field, node);

    auto has_text = [&dl](std::string_view needle) {
        for (auto& cmd : dl.commands)
            if (auto* t = std::get_if<prism::TextCmd>(&cmd))
                if (t->text.find(needle) != std::string_view::npos) return true;
        return false;
    };
    CHECK(has_text("my-node"));
    CHECK(has_text("size: 42"));
    CHECK(has_text("kind: file"));
}
