#pragma once

#include <prism/core/field.hpp>
#include <prism/core/list.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/types.hpp>
#include <prism/core/widget_node.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#endif

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

template <ColumnStorage T>
TableSource wrap_column_storage(T& data) {
    return TableSource{
        .column_count = [&data] { return data.column_count(); },
        .row_count = [&data] { return data.row_count(); },
        .cell_text = [&data](size_t r, size_t c) -> std::string {
            return std::string(data.cell_text(r, c));
        },
        .header = [&data](size_t c) -> std::string_view {
            return data.header(c);
        },
    };
}

// Intentionally minimal: only gates overload selection. The actual row-element
// constraints are enforced by wrap_row_storage (Task 3) via reflection.
template <typename T>
concept RowStorage = requires {
    requires is_list_v<std::remove_cvref_t<T>>;
};

#if __cpp_impl_reflection

namespace detail {
template <typename T>
std::string field_to_string(const Field<T>& f) {
    if constexpr (std::is_same_v<T, std::string>)
        return f.get();
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(f.get());
    else
        return "?";
}
} // namespace detail

template <RowStorage L>
TableSource wrap_row_storage(L& list) {
    using Row = typename std::remove_cvref_t<L>::value_type;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));

    // Build header names at static-init time
    static const auto headers = [] {
        std::vector<std::string> h;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (is_field_v<M>) {
                h.emplace_back(std::meta::identifier_of(m));
            }
        }
        return h;
    }();

    return TableSource{
        .column_count = [&list] { return headers.size(); },
        .row_count = [&list] { return list.size(); },
        .cell_text = [&list](size_t row, size_t col) -> std::string {
            size_t idx = 0;
            std::string result;
            const auto& r = list[row];
            template for (constexpr auto m : members) {
                auto& member = r.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (is_field_v<M>) {
                    if (idx == col)
                        result = detail::field_to_string(member);
                    ++idx;
                }
            }
            return result;
        },
        .header = [](size_t col) -> std::string_view {
            return headers[col];
        },
    };
}

#endif // __cpp_impl_reflection

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
