#pragma once

#include <prism/core/field.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/types.hpp>
#include <prism/core/widget_node.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism {

struct TableSource {
    std::function<size_t()> column_count;
    std::function<size_t()> row_count;
    std::function<std::string(size_t row, size_t col)> cell_text;
    std::function<std::string_view(size_t col)> header;
};

template <typename T>
concept ColumnStorage = requires(const T& t, size_t r, size_t c) {
    { t.column_count() } -> std::convertible_to<size_t>;
    { t.row_count() } -> std::convertible_to<size_t>;
    { t.cell_text(r, c) } -> std::convertible_to<std::string>;
    { t.header(c) } -> std::convertible_to<std::string_view>;
};

// Intentionally minimal: only gates overload selection. The actual row-element
// constraints are enforced by wrap_row_storage (Task 3) via reflection.
template <typename T>
concept RowStorage = requires {
    requires is_list_v<std::remove_cvref_t<T>>;
};

struct TableState {
    size_t column_count = 0;

    DY scroll_y{0};
    Height viewport_h{0};
    Height row_height{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};

    DX scroll_x{0};
    Width viewport_w{0};
    Width total_columns_w{0};

    Field<std::optional<size_t>> selected_row{std::nullopt};

    TableSource source;

    std::vector<WidgetNode> pool;

    std::vector<std::string> header_overrides;

    size_t row_count() const {
        return source.row_count ? source.row_count() : 0;
    }

    std::string_view column_header(size_t col) const {
        if (col < header_overrides.size() && !header_overrides[col].empty())
            return header_overrides[col];
        return source.header ? source.header(col) : "";
    }
};

} // namespace prism
