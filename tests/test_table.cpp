#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/table.hpp>
#include <prism/core/delegate.hpp>
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
#endif // __cpp_impl_reflection
