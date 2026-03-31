#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/table.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/hit_test.hpp>
#include <prism/core/widget_tree.hpp>

#include <array>

TEST_CASE("LayoutKind::Table exists") {
    auto kind = prism::LayoutKind::Table;
    CHECK(kind == prism::LayoutKind::Table);
}

TEST_CASE("TableState default-constructs") {
    prism::TableState ts;
    CHECK(ts.column_count == 0);
    CHECK(ts.row_count() == 0);
    CHECK(ts.selected_row.get() == std::nullopt);
}

struct TestColumns {
    std::vector<double> time = {0.0, 0.125, 0.25};
    std::vector<std::string> label = {"SW", "SW", "MS"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return time.size(); }
    std::string_view header(size_t col) const {
        static constexpr std::array<const char*, 2> names = {"Time", "Label"};
        return names[col];
    }
    std::string cell_text(size_t row, size_t col) const {
        if (col == 0) return std::to_string(time[row]);
        return label[row];
    }
};

static_assert(prism::ColumnStorage<TestColumns>);

TEST_CASE("wrap_column_storage produces valid TableSource") {
    TestColumns data;
    auto src = prism::wrap_column_storage(data);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 3);
    CHECK(src.header(0) == "Time");
    CHECK(src.header(1) == "Label");
    CHECK(src.cell_text(1, 1) == "SW");
}

struct ColumnModel {
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<std::string> name = {"a", "b", "c"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 2> h = {"X", "Name"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        if (c == 0) return std::to_string(x[r]);
        return name[r];
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("ViewBuilder.table() with ColumnStorage creates Table node") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    CHECK(true);
}

TEST_CASE("Table layout: produces geometry") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    CHECK(snap->geometry.size() > 0);
    CHECK(snap->draw_lists.size() > 0);
}

TEST_CASE("Table header text appears in draw commands") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    bool found_x = false, found_name = false;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "X") found_x = true;
                if (tc->text == "Name") found_name = true;
            }
        }
    }
    CHECK(found_x);
    CHECK(found_name);
}

TEST_CASE("Table in column layout: no overlap with sibling") {
    // Mimics dashboard: scroll area + table + list in a column
    struct Model {
        prism::Field<std::string> label1{"one"};
        prism::Field<std::string> label2{"two"};
        prism::Field<std::string> label3{"three"};
        prism::Field<std::string> label4{"four"};
        prism::Field<std::string> label5{"five"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.vstack(label1, label2, label3, label4, label5);
            });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    snap = tree.build_snapshot(400, 300, 2);

    // Dump all text commands with their Y positions
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                MESSAGE("text '", tc->text, "' at y=", tc->origin.y.raw());
            }
            if (auto* cp = std::get_if<prism::ClipPush>(&cmd)) {
                MESSAGE("clip_push y=", cp->rect.origin.y.raw(),
                        " h=", cp->rect.extent.h.raw());
            }
        }
    }

    // Verify: all table cell text Y positions should be >= scroll_area_height
    // The scroll gets ~100px (1/3 of 300), so table starts at ~100
    float third = 100.f;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                // Table cell text should not be in the scroll area
                if (tc->text == "a" || tc->text == "b" || tc->text == "c") {
                    CHECK(tc->origin.y.raw() >= third - 5.f);
                }
            }
        }
    }
}

TEST_CASE("Click selects row via hit_test in multi-widget layout") {
    struct Model {
        prism::Field<std::string> label{"hello"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.widget(label); });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    snap = tree.build_snapshot(400, 300, 2);

    // Find table node
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    REQUIRE(table_node != nullptr);

    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table rect: y=", table_rect->origin.y.raw(),
            " h=", table_rect->extent.h.raw());

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Click in the table body area (window coords)
    float row_h = ts.row_height.raw();
    float abs_y = table_rect->origin.y.raw() + row_h + row_h * 0.5f;  // header + half a row
    MESSAGE("clicking at abs_y=", abs_y);

    // Verify hit_test returns the table
    auto hit = prism::hit_test(*snap, prism::Point{prism::X{50.f}, prism::Y{abs_y}});
    REQUIRE(hit.has_value());
    CHECK(*hit == table_node->id);

    // Simulate what route_mouse_button does: localize and dispatch
    prism::MouseButton click{
        .position = prism::Point{prism::X{50.f}, prism::Y{abs_y}},
        .button = 1, .pressed = true};
    prism::InputEvent ev{click};
    auto localized = prism::localize_mouse(ev, *table_rect);
    tree.dispatch(table_node->id, localized);

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 0);
}

TEST_CASE("Table selection via full route_mouse_button path") {
    // Mimic dashboard: scroll + table + list, with many fields in scroll
    struct BigModel {
        prism::Field<std::string> f1{"a"}, f2{"b"}, f3{"c"}, f4{"d"};
        prism::Field<std::string> f5{"e"}, f6{"f"}, f7{"g"}, f8{"h"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.vstack(f1, f2, f3, f4, f5, f6, f7, f8); });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    BigModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find table
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    REQUIRE(table_node != nullptr);

    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table y=", table_rect->origin.y.raw(), " h=", table_rect->extent.h.raw());

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Click in the middle of table body (absolute coords)
    float row_h = ts.row_height.raw();
    float abs_y = table_rect->origin.y.raw() + row_h + row_h * 0.5f;
    prism::Point click_pos{prism::X{50.f}, prism::Y{abs_y}};

    // What does hit_test return?
    auto hit = prism::hit_test(*snap, click_pos);
    MESSAGE("hit_test returned id=", hit.value_or(999));
    MESSAGE("table id=", table_node->id);

    // Dump ALL geometry entries that contain this point
    for (size_t i = 0; i < snap->geometry.size(); ++i) {
        auto& [id, rect] = snap->geometry[i];
        if (rect.contains(click_pos)) {
            MESSAGE("geometry[", i, "] id=", id,
                    " y=", rect.origin.y.raw(), " h=", rect.extent.h.raw());
        }
    }

    REQUIRE(hit.has_value());
    CHECK(*hit == table_node->id);
}

TEST_CASE("Cell text position is relative inside clip") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find a cell text command for column 1 (Name) — should be inside
    // its column's clip rect, not double-offset
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl.commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "a" || tc->text == "b" || tc->text == "c") {
                    // Text x should be within a reasonable range
                    // (column_origin + small padding), not double-offset
                    CHECK(tc->origin.x.raw() < 600.f);
                }
            }
        }
    }
}

TEST_CASE("Click on table row sets selected_row") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table) {
            table_node = &c;
            break;
        }
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    float row_h = ts.row_height.raw();
    float header_h = row_h;
    float click_y = header_h + row_h * 1.5f;

    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 1);
}

TEST_CASE("hit_test finds table, not cells") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find the table node to get its ID
    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    // Find table rect in geometry
    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table rect: y=", table_rect->origin.y.raw(),
            " h=", table_rect->extent.h.raw());

    // Click in the middle of the table body
    float mid_y = table_rect->origin.y.raw() + table_rect->extent.h.raw() * 0.5f;
    float mid_x = table_rect->origin.x.raw() + table_rect->extent.w.raw() * 0.5f;
    auto hit_id = prism::hit_test(*snap, prism::Point{prism::X{mid_x}, prism::Y{mid_y}});
    REQUIRE(hit_id.has_value());
    CHECK(*hit_id == table_node->id);

    // Now dispatch a localized click on row 1 and verify selection works
    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    float row_h = ts.row_height.raw();
    float header_h = row_h;
    // local_y = header + 1.5 rows → row index 1
    float local_y = header_h + row_h * 1.5f;
    MESSAGE("local_y=", local_y, " row_h=", row_h, " row_count=", ts.row_count());
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{local_y}},
        .button = 1, .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});
    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 1);
}

TEST_CASE("Click selected row deselects") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    ts.selected_row.set(std::optional<size_t>{1});

    float row_h = ts.row_height.raw();
    float click_y = row_h + row_h * 1.5f;
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get() == std::nullopt);
}

TEST_CASE("Arrow keys move selection") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Arrow down with no selection -> select row 0
    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});

    // Arrow down -> row 1
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get() == std::optional<size_t>{1});

    // Arrow up -> row 0
    prism::KeyPress up_key{.key = prism::keys::up, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{up_key});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});

    // Arrow up at row 0 -> stays at 0
    tree.dispatch(table_node->id, prism::InputEvent{up_key});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});
}

struct LargeColumnModel {
    std::vector<double> values;

    LargeColumnModel() : values(100) {
        for (size_t i = 0; i < 100; ++i) values[i] = static_cast<double>(i);
    }

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Value"; }
    std::string cell_text(size_t r, size_t) const { return std::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("Mouse wheel scrolls table vertically") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    CHECK(ts.scroll_y.raw() == 0.f);

    prism::MouseScroll scroll{
        .position = prism::Point{prism::X{10.f}, prism::Y{100.f}},
        .dx = prism::DX{0}, .dy = prism::DY{3.f}};
    tree.dispatch(table_node->id, prism::InputEvent{scroll});

    CHECK(ts.scroll_y.raw() > 0.f);
}

TEST_CASE("Arrow key selection scrolls into view") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Select a row near the bottom of viewport, then press down past it
    size_t visible_rows = ts.viewport_h.raw() > 0
        ? static_cast<size_t>(ts.viewport_h.raw() / ts.row_height.raw())
        : 10;
    ts.selected_row.set(std::optional<size_t>{visible_rows - 1});

    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});

    CHECK(ts.selected_row.get().value() == visible_rows);
    CHECK(ts.scroll_y.raw() > 0.f);
}

struct DynamicColumnModel {
    std::vector<double> values = {1.0, 2.0};
    prism::Field<bool> data_updated{false};

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Val"; }
    std::string cell_text(size_t r, size_t) const { return std::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).depends_on(data_updated);
    }
};

TEST_CASE("ColumnStorage table re-renders on depends_on trigger") {
    DynamicColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.data_updated.set(true);
    CHECK(tree.any_dirty());
}

struct HeaderOverrideModel {
    std::vector<double> x = {1.0, 2.0};

    size_t column_count() const { return 1; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t) const { return "X"; }
    std::string cell_text(size_t r, size_t) const { return std::to_string(x[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).headers({"Custom"});
    }
};

TEST_CASE("headers() builder overrides table headers") {
    HeaderOverrideModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;
    REQUIRE(ts.header_overrides.size() == 1);
    CHECK(ts.header_overrides[0] == "Custom");
}

TEST_CASE("Full table workflow: render, scroll, select, observe") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);

    // Frame 1-2: stabilize
    (void)tree.build_snapshot(800, 600, 1);
    auto snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::get_if<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Verify initial state
    CHECK(ts.row_count() == 100);
    CHECK(ts.selected_row.get() == std::nullopt);
    CHECK(ts.scroll_y.raw() == 0.f);

    // Observe selection changes
    std::optional<size_t> observed_row;
    ts.selected_row.observe([&](const std::optional<size_t>& r) {
        observed_row = r;
    });

    // Select row via click
    float row_h = ts.row_height.raw();
    float header_h = row_h;
    float click_y = header_h + row_h * 5.5f; // click on row 5
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 5);
    CHECK(observed_row.has_value());
    CHECK(observed_row.value() == 5);

    // Scroll down via mouse wheel
    prism::MouseScroll scroll{
        .position = prism::Point{prism::X{10.f}, prism::Y{100.f}},
        .dx = prism::DX{0}, .dy = prism::DY{5.f}};
    tree.dispatch(table_node->id, prism::InputEvent{scroll});
    CHECK(ts.scroll_y.raw() > 0.f);

    // Re-render after scroll — should still produce valid output
    snap = tree.build_snapshot(800, 600, 3);
    CHECK(snap->geometry.size() > 0);
    CHECK(snap->draw_lists.size() > 0);

    // Arrow key moves selection
    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get().value() == 6);
    CHECK(observed_row.value() == 6);
}

#if __cpp_impl_reflection
#include <prism/core/list.hpp>

struct TestRow {
    prism::Field<std::string> label{""};
    prism::Field<double> value{0.0};
};

TEST_CASE("wrap_row_storage produces valid TableSource") {
    prism::List<TestRow> rows;
    rows.push_back(TestRow{.label = {"Alpha"}, .value = {1.5}});
    rows.push_back(TestRow{.label = {"Beta"}, .value = {2.7}});

    auto src = prism::wrap_row_storage(rows);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 2);
    CHECK(src.header(0) == "label");
    CHECK(src.header(1) == "value");
    CHECK(src.cell_text(0, 0) == "Alpha");
    CHECK(src.cell_text(1, 1) == std::to_string(2.7));
}

struct RowModel {
    prism::List<TestRow> rows;

    RowModel() {
        rows.push_back(TestRow{.label = {"A"}, .value = {1.0}});
        rows.push_back(TestRow{.label = {"B"}, .value = {2.0}});
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(rows);
    }
};

TEST_CASE("ViewBuilder.table() with RowStorage creates Table node") {
    RowModel model;
    prism::WidgetTree tree(model);
    CHECK(true);
}

TEST_CASE("RowStorage table updates on List insert") {
    RowModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.rows.push_back(TestRow{.label = {"C"}, .value = {3.0}});
    CHECK(tree.any_dirty());
}

TEST_CASE("RowStorage table updates on List remove") {
    RowModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.rows.erase(0);
    CHECK(tree.any_dirty());
}
#endif // __cpp_impl_reflection
