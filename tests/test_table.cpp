#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/core/table.hpp>
#include <prism/core/delegate.hpp>

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
