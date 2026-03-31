#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/table.hpp>
#include <prism/core/delegate.hpp>

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
#endif // __cpp_impl_reflection
