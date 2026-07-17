#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/widgets/debug/tree_inspector.hpp>
#include <prism/core/field.hpp>

#include <algorithm>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {
struct Leaf { prism::Field<int> value{0}; };
struct FlatModel {
    prism::Field<int> a{0};
    prism::Field<int> b{0};
};

// leaf_ids() only covers true leaves; flatten_tree also needs every *container* id in the
// expanded set to descend into it (see WidgetTree::ViewBuilder::finalize()'s single-child
// hoisting, which can still leave multiple nesting levels for e.g. Tabs content).
void collect_all_ids(const prism::WidgetNode& n, std::set<prism::WidgetId>& ids) {
    ids.insert(n.id);
    for (auto& c : n.children) collect_all_ids(c, ids);
}
}

TEST_CASE("flatten_tree on an empty-of-children root produces just the root row") {
    struct Empty { void view(prism::WidgetTree::ViewBuilder&) {} };
    Empty model;
    prism::WidgetTree tree(model);
    auto rows = prism::debug::flatten_tree(tree, {});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].depth == 0);
}

TEST_CASE("flatten_tree depth-first order matches child order, depth increments per level") {
    FlatModel model;
    prism::WidgetTree tree(model);
    std::set<prism::WidgetId> all_expanded;
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    all_expanded.insert(tree.root().id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);
    REQUIRE(rows.size() >= 3); // root + a + b, at minimum
    CHECK(rows[0].depth == 0);
    for (size_t i = 1; i < rows.size(); ++i)
        CHECK(rows[i].depth >= 1);
}

TEST_CASE("flatten_tree skips children of a node not in the expanded set") {
    FlatModel model;
    prism::WidgetTree tree(model);
    auto rows_collapsed = prism::debug::flatten_tree(tree, {}); // nothing expanded
    CHECK(rows_collapsed.size() == 1); // just the root, no children shown

    std::set<prism::WidgetId> only_root{tree.root().id};
    auto rows_root_expanded = prism::debug::flatten_tree(tree, only_root);
    CHECK(rows_root_expanded.size() >= 3); // root + its direct children now visible
}

TEST_CASE("flatten_tree row fields reflect live tree state") {
    FlatModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1); // establish geometry
    std::set<prism::WidgetId> all_expanded{tree.root().id};
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);
    for (auto& row : rows) {
        CHECK(row.id != 0);
        CHECK(!row.layout_kind_name.empty());
    }
}

TEST_CASE("flatten_tree names the model root after the model's own type") {
    FlatModel model;
    prism::WidgetTree tree(model);
    std::set<prism::WidgetId> all_expanded{tree.root().id};
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);
    // The root wraps a Model with no view() (pure reflection path); it must be labeled
    // with the model's own type rather than falling back to "Default".
    CHECK(rows[0].name == "FlatModel");
}

// Regression test: nested component() sub-trees (PRISM's primary composition mechanism —
// "compose by nesting") each get their own wrapper root Node via build_node_tree(), whose
// layout_kind defaults to LayoutKind::Default and which historically never received a
// debug_name — so every component boundary showed up in the inspector as an unlabeled,
// indistinguishable "Default" row. It must instead be labeled after the component's type.
struct NamedSubComponent {
    prism::Field<int> x{0};
    prism::Field<int> y{0};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(x, y); }
};
struct ParentOfNamedSub {
    NamedSubComponent sub;
    prism::Field<bool> flag{false};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.component(sub);
            vb.widget(flag);
        });
    }
};

TEST_CASE("flatten_tree names a nested component() root after the component's own type") {
    ParentOfNamedSub model;
    prism::WidgetTree tree(model);
    std::set<prism::WidgetId> all_expanded{tree.root().id};
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);

    auto it = std::find_if(rows.begin(), rows.end(),
        [](const prism::debug::NodeRow& r) { return r.name == "NamedSubComponent"; });
    REQUIRE(it != rows.end());
    CHECK(it->name != "Default");
}

// Regression test: vb.widget(field) (the ordinary view()-driven placement path) only ever
// named its leaf after the Field's *value type* (e.g. "int") -- since two same-typed fields
// are then visually indistinguishable, this is exactly as uninformative as "Default" once
// there's more than one field of the same type on a component. It must be named after the
// enclosing member instead, same as the whole-model reflection-only path already does.
struct NamedFieldsModel {
    prism::Field<int> alpha{0};
    prism::Field<int> beta{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.widget(alpha);
        vb.widget(beta);
    }
};

TEST_CASE("flatten_tree names a view()-placed leaf after its field member name") {
    NamedFieldsModel model;
    prism::WidgetTree tree(model);
    std::set<prism::WidgetId> all_expanded{tree.root().id};
    for (auto id : tree.leaf_ids()) all_expanded.insert(id);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);

    auto has_name = [&](std::string_view name) {
        return std::find_if(rows.begin(), rows.end(), [&](const prism::debug::NodeRow& r) {
            return r.name == name;
        }) != rows.end();
    };
    CHECK(has_name("alpha"));
    CHECK(has_name("beta"));
}

// Regression test: tab() content builders run lazily (materialize_tabs, on first
// build_snapshot -- see comment there), constructing a fresh ViewBuilder well after the model's
// own build_node_tree() call has returned. Naming must survive that: it's resolved via
// WidgetTree::field_names_ (populated once, lives as long as the tree) rather than a per-
// ViewBuilder pointer that would dangle once the original build_node_tree() call unwound.
struct TabsNamedFieldModel {
    prism::Field<prism::TabBar<>> tabs;
    prism::Field<int> gamma{0};
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Only", [&](prism::WidgetTree::ViewBuilder& tvb) { tvb.widget(gamma); });
        });
    }
};

TEST_CASE("flatten_tree names a field placed inside a lazily-materialized tab") {
    TabsNamedFieldModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1); // triggers materialize_tabs
    REQUIRE(snap != nullptr);

    std::set<prism::WidgetId> all_expanded;
    collect_all_ids(tree.root(), all_expanded);
    auto rows = prism::debug::flatten_tree(tree, all_expanded);

    auto it = std::find_if(rows.begin(), rows.end(),
        [](const prism::debug::NodeRow& r) { return r.name == "gamma"; });
    CHECK(it != rows.end());
}
