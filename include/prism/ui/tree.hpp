#pragma once

#include <prism/core/field.hpp>
#include <prism/core/list.hpp>
#include <prism/core/types.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/delegate.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace prism::ui {
using namespace prism::core;

using TreeNodeId = uint64_t;

struct TreeSource {
    std::function<size_t()> root_count;
    std::function<TreeNodeId(size_t)> root_at;
    std::function<size_t(TreeNodeId)> child_count;
    std::function<TreeNodeId(TreeNodeId, size_t)> child_at;
    std::function<std::string(TreeNodeId)> label;
    std::function<bool(TreeNodeId)> has_children;
    std::function<std::optional<std::string>(TreeNodeId)> icon;
    std::function<std::vector<std::pair<std::string, std::string>>(TreeNodeId)> attributes;
};

template <typename T>
concept TreeStorage = requires(const T& t, TreeNodeId id, size_t i) {
    { t.root_count() } -> std::convertible_to<size_t>;
    { t.root_at(i) } -> std::convertible_to<TreeNodeId>;
    { t.child_count(id) } -> std::convertible_to<size_t>;
    { t.child_at(id, i) } -> std::convertible_to<TreeNodeId>;
    { t.label(id) } -> std::convertible_to<std::string>;
    { t.has_children(id) } -> std::convertible_to<bool>;
};

template <TreeStorage T>
TreeSource wrap_tree_storage(T& data) {
    TreeSource src;
    src.root_count = [&data] { return data.root_count(); };
    src.root_at = [&data](size_t i) { return data.root_at(i); };
    src.child_count = [&data](TreeNodeId id) { return data.child_count(id); };
    src.child_at = [&data](TreeNodeId id, size_t i) { return data.child_at(id, i); };
    src.label = [&data](TreeNodeId id) { return data.label(id); };
    src.has_children = [&data](TreeNodeId id) { return data.has_children(id); };
    if constexpr (requires(const T& t, TreeNodeId id) { t.icon(id); }) {
        src.icon = [&data](TreeNodeId id) { return data.icon(id); };
    }
    if constexpr (requires(const T& t, TreeNodeId id) { t.attributes(id); }) {
        src.attributes = [&data](TreeNodeId id) { return data.attributes(id); };
    }
    return src;
}

struct TreeRow {
    TreeNodeId id = 0;
    std::string label;
    std::optional<std::string> icon;
    int depth = 0;
    bool has_children = false;
    bool expanded = false;
    bool selected = false;

    bool operator==(const TreeRow&) const = default;
};

struct TreeDetail {
    std::string label;
    std::vector<std::pair<std::string, std::string>> attributes;

    bool operator==(const TreeDetail&) const = default;
};

template <>
struct Widget<TreeRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height row_h{22.f};

    static void record(DrawList& dl, const Field<TreeRow>& field, WidgetNode& node) {
        auto& row = field.get();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        bool highlight = vs.hovered || row.selected;
        auto bg = highlight ? t.surface_hover : t.surface;
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, row_h), bg);

        DX indent{static_cast<float>(row.depth) * 16.f};
        std::string marker = row.has_children ? (row.expanded ? "v " : "> ") : "  ";
        std::string icon_part = row.icon ? (*row.icon + " ") : std::string{};
        dl.text(marker + icon_part + row.label,
                 detail::make_point(X{8.f} + indent, Y{4.f}), 13, t.text);
    }

    // Selection/expand-toggle/keyboard nav are handled centrally by TreeController via the
    // container's row-click and container-level keyboard wiring (see ViewBuilder::tree(),
    // Task 5/6) -- an individual row has nothing of its own to mutate on input.
    static void handle_input(Field<TreeRow>&, const InputEvent&, WidgetNode&) {}
};

template <>
struct Widget<std::optional<TreeDetail>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height panel_h{200.f};

    static void record(DrawList& dl, const Field<std::optional<TreeDetail>>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        auto w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, panel_h), t.surface);

        auto& value = field.get();
        if (!value) {
            dl.text("No selection", detail::make_point(X{8.f}, Y{8.f}), 13, t.text);
            return;
        }
        Y y{8.f};
        dl.text(value->label, detail::make_point(X{8.f}, y), 13, t.text);
        y = y + DY{18.f};
        for (auto& [k, v] : value->attributes) {
            dl.text(k + ": " + v, detail::make_point(X{8.f}, y), 13, t.text);
            y = y + DY{18.f};
        }
    }

    static void handle_input(Field<std::optional<TreeDetail>>&, const InputEvent&, WidgetNode&) {}
};

namespace detail_tree {
inline void flatten_node(const TreeSource& source, TreeNodeId id, int depth,
                          const std::set<TreeNodeId>& expanded,
                          std::optional<TreeNodeId> selected,
                          std::vector<TreeRow>& out) {
    TreeRow row;
    row.id = id;
    row.label = source.label(id);
    row.icon = source.icon ? source.icon(id) : std::nullopt;
    row.depth = depth;
    row.has_children = source.has_children(id);
    row.expanded = expanded.contains(id);
    row.selected = selected.has_value() && *selected == id;
    out.push_back(row);

    if (row.has_children && row.expanded) {
        size_t n = source.child_count(id);
        for (size_t i = 0; i < n; ++i)
            flatten_node(source, source.child_at(id, i), depth + 1, expanded, selected, out);
    }
}
} // namespace detail_tree

inline std::vector<TreeRow> visible_rows(const TreeSource& source,
                                          const std::set<TreeNodeId>& expanded,
                                          std::optional<TreeNodeId> selected = std::nullopt) {
    std::vector<TreeRow> rows;
    size_t n = source.root_count();
    for (size_t i = 0; i < n; ++i)
        detail_tree::flatten_node(source, source.root_at(i), 0, expanded, selected, rows);
    return rows;
}

class TreeController {
public:
    explicit TreeController(TreeSource source) : source_(std::move(source)) {
        refresh();
    }

    List<TreeRow> rows;
    Field<std::optional<TreeNodeId>> selected;
    Field<std::optional<TreeDetail>> detail;

    void refresh() {
        auto fresh = visible_rows(source_, expanded_, selected.get());
        while (!rows.empty()) rows.erase(rows.size() - 1);
        for (auto& r : fresh) rows.push_back(r);
    }

    void on_row_clicked(size_t, const TreeRow& row) {
        selected.set(row.id);
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
        populate_detail(row.id);
        refresh();
    }

    // Returns the index in `rows` that should be scrolled into view, if selection moved or
    // an expand/collapse happened; nullopt if `ev` wasn't a handled KeyPress.
    std::optional<size_t> on_key(const InputEvent& ev) {
        auto* kp = std::get_if<KeyPress>(&ev);
        if (!kp) return std::nullopt;

        std::vector<TreeRow> snapshot;
        for (size_t i = 0; i < rows.size(); ++i) snapshot.push_back(rows[i]);
        if (snapshot.empty()) return std::nullopt;

        auto sel = selected.get();
        size_t cur = 0;
        bool has_cur = false;
        if (sel) {
            for (size_t i = 0; i < snapshot.size(); ++i)
                if (snapshot[i].id == *sel) { cur = i; has_cur = true; break; }
        }

        if (kp->key == keys::down) {
            size_t next = has_cur ? cur + 1 : 0;
            if (next >= snapshot.size()) return std::nullopt;
            select_row(snapshot[next].id);
            return next;
        }
        if (kp->key == keys::up) {
            if (!has_cur || cur == 0) return std::nullopt;
            select_row(snapshot[cur - 1].id);
            return cur - 1;
        }
        if (kp->key == keys::right) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.has_children && !row.expanded) {
                expanded_.insert(row.id);
                refresh();
                return cur;
            }
            if (row.expanded && cur + 1 < snapshot.size()) {
                select_row(snapshot[cur + 1].id);
                return cur + 1;
            }
            return std::nullopt;
        }
        if (kp->key == keys::left) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.expanded) {
                expanded_.erase(row.id);
                refresh();
                return cur;
            }
            for (size_t i = cur; i-- > 0; ) {
                if (snapshot[i].depth < row.depth) {
                    select_row(snapshot[i].id);
                    return i;
                }
            }
            return std::nullopt;
        }
        if (kp->key == keys::enter || kp->key == keys::space) {
            if (!has_cur) return std::nullopt;
            on_row_clicked(cur, snapshot[cur]);
            return cur;
        }
        return std::nullopt;
    }

private:
    void select_row(TreeNodeId id) {
        selected.set(id);
        populate_detail(id);
    }

    void populate_detail(TreeNodeId id) {
        TreeDetail d;
        d.label = source_.label(id);
        d.attributes = source_.attributes ? source_.attributes(id)
                                           : std::vector<std::pair<std::string, std::string>>{};
        detail.set(d);
    }

    TreeSource source_;
    std::set<TreeNodeId> expanded_;
};

} // namespace prism::ui
