#pragma once

#include <prism/core/widget_node.hpp>

#include <functional>
#include <string>

namespace prism {
namespace detail {

inline const TabBarEditState& get_tabs_state(const WidgetNode& node) {
    static const TabBarEditState default_state;
    auto* p = std::get_if<TabBarEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline TabBarEditState& ensure_tabs_state(WidgetNode& node) {
    if (!std::holds_alternative<TabBarEditState>(node.edit_state))
        node.edit_state = TabBarEditState{};
    return std::get<TabBarEditState>(node.edit_state);
}

constexpr float tab_h = 32.f;
constexpr float tab_padding = 16.f;
constexpr float tab_font_size = 14.f;
constexpr float tab_accent_h = 2.f;
constexpr float tab_char_width = 8.f;

inline void tabs_record(DrawList& dl, WidgetNode& node,
                        size_t selected, const std::vector<std::string>& names) {
    auto& vs = node_vs(node);
    auto& es = ensure_tabs_state(node);

    float total_w = 0;
    for (auto& name : names)
        total_w += tab_padding * 2 + static_cast<float>(name.size()) * tab_char_width;

    dl.filled_rect(make_rect(0, 0, total_w, tab_h), Color::rgba(42, 42, 58));

    es.header_x_ranges.clear();
    es.header_x_ranges.reserve(names.size());
    float x = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        float w = tab_padding * 2 + static_cast<float>(names[i].size()) * tab_char_width;
        es.header_x_ranges.push_back({x, x + w});

        bool is_selected = (i == selected);
        bool is_hovered = es.hovered_tab.has_value() && es.hovered_tab.value() == i;

        auto bg = is_selected ? Color::rgba(30, 30, 46)
                : is_hovered  ? Color::rgba(55, 55, 68)
                :               Color::rgba(42, 42, 58);
        dl.filled_rect(make_rect(x, 0, w, tab_h), bg);

        auto text_color = is_selected ? Color::rgba(220, 220, 240)
                                      : Color::rgba(140, 140, 160);
        dl.text(names[i], make_point(x + tab_padding, 8), tab_font_size, text_color);

        if (is_selected)
            dl.filled_rect(make_rect(x, tab_h - tab_accent_h, w, tab_accent_h),
                           Color::rgba(124, 111, 255));
        x += w;
    }

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, total_w + 2, tab_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);
}

inline bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                              size_t selected, size_t count,
                              std::function<void(size_t)> select) {
    auto& es = ensure_tabs_state(node);

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float mx = mb->position.x.raw();
        for (size_t i = 0; i < es.header_x_ranges.size(); ++i) {
            auto [x0, x1] = es.header_x_ranges[i];
            if (mx >= x0 && mx < x1 && i != selected) {
                select(i);
                return true;
            }
        }
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        std::optional<size_t> hover;
        float mx = mm->position.x.raw();
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

inline void Delegate<TabBar>::record(DrawList& dl, const Field<TabBar>& field,
                                     WidgetNode& node) {
    if (node.tab_names && !node.tab_names->empty())
        detail::tabs_record(dl, node, field.get().selected, *node.tab_names);
}

inline void Delegate<TabBar>::handle_input(Field<TabBar>& field, const InputEvent& ev,
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

} // namespace prism
