#pragma once

#include <prism/ui/widget_node.hpp>

#include <functional>
#include <string>

namespace prism::ui {

namespace detail {

inline const TabBarEditState& get_tabs_state(const WidgetNode& node) {
    static const TabBarEditState default_state;
    auto* p = std::any_cast<TabBarEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline TabBarEditState& ensure_tabs_state(WidgetNode& node) {
    return node.get_or_create<TabBarEditState>();
}

constexpr Height tab_h{32.f};
constexpr Width tab_padding{16.f};
constexpr float tab_font_size = 14.f;
constexpr Height tab_accent_h{2.f};
constexpr Width tab_char_width{8.f};

inline void tabs_record(DrawList& dl, WidgetNode& node,
                        size_t selected, const std::vector<std::string>& names) {
    auto& vs = node_vs(node);
    auto& es = ensure_tabs_state(node);
    auto& t = *node.theme;

    Width total_w{0};
    for (auto& name : names)
        total_w += tab_padding * 2.f + tab_char_width * static_cast<float>(name.size());

    dl.filled_rect(make_rect(X{0}, Y{0}, total_w, tab_h), t.tab_bar_bg);

    es.header_x_ranges.clear();
    es.header_x_ranges.reserve(names.size());
    X x{0};
    for (size_t i = 0; i < names.size(); ++i) {
        Width w = tab_padding * 2.f + tab_char_width * static_cast<float>(names[i].size());
        es.header_x_ranges.push_back({x, x + DX{w.raw()}});

        bool is_selected = (i == selected);
        bool is_hovered = es.hovered_tab.has_value() && es.hovered_tab.value() == i;

        auto bg = is_selected ? t.tab_active_bg
                : is_hovered  ? t.surface_hover
                :               t.tab_bar_bg;
        dl.filled_rect(make_rect(x, Y{0}, w, tab_h), bg);

        auto text_color = is_selected ? t.tab_text_active
                                      : t.tab_text;
        dl.text(names[i], make_point(x + DX{tab_padding.raw()}, Y{8}), tab_font_size, text_color);

        if (is_selected)
            dl.filled_rect(make_rect(x, Y{(tab_h - tab_accent_h).raw()}, w, tab_accent_h),
                           t.tab_accent);
        x += DX{w.raw()};
    }

    if (vs.focused)
        dl.rect_outline(make_rect(X{-1}, Y{-1}, total_w + Width{2.f}, tab_h + Height{2.f}),
                        t.focus_ring, 2.0f);
}

inline bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                              size_t selected, size_t count,
                              std::function<void(size_t)> select) {
    auto& es = ensure_tabs_state(node);

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        X mx = mb->position.x;
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1 && i != selected) {
                select(i);
                return true;
            }
        }
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        std::optional<size_t> hover;
        X mx = mm->position.x;
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1) { hover = i; break; }
        }
        if (hover != es.hovered_tab) {
            es.hovered_tab = hover;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::right) {
            select((selected + 1) % count);
            return true;
        } else if (kp->key == keys::left) {
            select((selected + count - 1) % count);
            return true;
        }
    }
    return false;
}

} // namespace detail

inline void Widget<TabBar<>>::record(DrawList& dl, const Field<TabBar<>>& field,
                                       WidgetNode& node) {
    if (node.tab_names && !node.tab_names->empty())
        detail::tabs_record(dl, node, field.get().selected, *node.tab_names);
}

inline void Widget<TabBar<>>::handle_input(Field<TabBar<>>& field, const InputEvent& ev,
                                              WidgetNode& node) {
    size_t count = node.tab_names ? node.tab_names->size() : 0;
    if (count == 0) return;
    detail::tabs_handle_input(ev, node, field.get().selected, count,
        [&](size_t idx) {
            auto tb = field.get();
            tb.selected = idx;
            field.set(tb);
        });
}

} // namespace prism::ui
