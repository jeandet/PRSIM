#pragma once

#include <prism/ui/widget_node.hpp>

#include <functional>
#include <string>

namespace prism::ui {

namespace detail {

struct DropdownLabelResolver {
    std::function<std::string(size_t)> label_at;
    size_t count;
};

inline const DropdownEditState& get_dropdown_state(const WidgetNode& node) {
    static const DropdownEditState default_state;
    auto* p = std::any_cast<DropdownEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline DropdownEditState& ensure_dropdown_state(WidgetNode& node) {
    return node.get_or_create<DropdownEditState>();
}

constexpr Height dd_widget_h{30.f};
constexpr Width dd_padding{8.f}; // used only as a horizontal inset below
constexpr float dd_font_size = 14.f;
constexpr Height dd_option_h{28.f};

inline void dropdown_record(DrawList& dl, WidgetNode& node,
                            const std::string& current_label,
                            const DropdownLabelResolver& resolver) {
    auto& vs = node_vs(node);
    auto& es = ensure_dropdown_state(node);
    Width w = detail::widget_w(node);

    auto& t = *node.theme;
    auto bg = es.open    ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(make_rect(X{0}, Y{0}, w, dd_widget_h), bg);

    dl.text(current_label, make_point(X{dd_padding.raw()}, Y{7}), dd_font_size, t.text);

    dl.text("\xe2\x96\xbe", make_point(X{w.raw() - 20.f}, Y{7}), dd_font_size, t.text_muted);

    if (vs.focused)
        dl.rect_outline(make_rect(X{-1}, Y{-1}, w + Width{2.f}, dd_widget_h + Height{2.f}),
                        t.focus_ring, 2.0f);

    node.overlay_draws.clear();
    if (es.open) {
        Height popup_h{static_cast<float>(resolver.count) * dd_option_h.raw()};

        // Flip upward if popup would extend past viewport bottom
        Y popup_y{dd_widget_h.raw()};
        Y abs_bottom{node.absolute_y.raw() + dd_widget_h.raw() + popup_h.raw()};
        if (node.viewport_height.raw() > 0 && abs_bottom.raw() > node.viewport_height.raw())
            popup_y = Y{-popup_h.raw()};

        node.overlay_draws.filled_rect(
            make_rect(X{0}, popup_y, w, popup_h),
            t.popup_bg);
        node.overlay_draws.rect_outline(
            make_rect(X{0}, popup_y, w, popup_h),
            t.popup_border, 1.0f);

        for (size_t i = 0; i < resolver.count; ++i) {
            Y y{popup_y.raw() + static_cast<float>(i) * dd_option_h.raw()};
            if (i == es.highlighted) {
                node.overlay_draws.filled_rect(
                    make_rect(X{0}, y, w, dd_option_h),
                    t.popup_highlight);
            }
            node.overlay_draws.text(
                resolver.label_at(i),
                make_point(X{dd_padding.raw()}, y + DY{6.f}), dd_font_size,
                i == es.highlighted ? t.text_on_primary
                                    : t.text);
        }

        es.popup_rect = make_rect(X{0}, popup_y, w, popup_h);
    }
}

inline bool dropdown_handle_input(const InputEvent& ev, WidgetNode& node,
                                  size_t current_index, size_t count,
                                  std::function<void(size_t)> select) {
    auto& es = ensure_dropdown_state(node);
    bool changed = false;

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        if (es.open) {
            Y popup_y = es.popup_rect.origin.y;
            DY rel_y = mb->position.y - popup_y;
            if (rel_y.raw() >= 0 && rel_y.raw() < static_cast<float>(count) * dd_option_h.raw()
                && mb->position.x.raw() >= 0 && mb->position.x.raw() < detail::widget_w(node).raw()) {
                size_t idx = static_cast<size_t>(rel_y.raw() / dd_option_h.raw());
                select(idx);
                changed = true;
            }
            es.open = false;
            node.dirty = true;
        } else {
            es.open = true;
            es.highlighted = current_index;
            Height popup_h{static_cast<float>(count) * dd_option_h.raw()};
            // Default: open below; record() may flip upward
            es.popup_rect = make_rect(X{0}, Y{dd_widget_h.raw()}, detail::widget_w(node), popup_h);
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (es.open) {
            if (kp->key == keys::down) {
                es.highlighted = (es.highlighted + 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::up) {
                es.highlighted = (es.highlighted + count - 1) % count;
                node.dirty = true;
            } else if (kp->key == keys::enter) {
                select(es.highlighted);
                es.open = false;
                node.dirty = true;
                changed = true;
            } else if (kp->key == keys::escape) {
                es.open = false;
                node.dirty = true;
            }
        } else {
            if (kp->key == keys::space || kp->key == keys::enter) {
                es.open = true;
                es.highlighted = current_index;
                node.dirty = true;
            } else if (kp->key == keys::down) {
                size_t next = (current_index + 1) % count;
                select(next);
                changed = true;
            } else if (kp->key == keys::up) {
                size_t prev = (current_index + count - 1) % count;
                select(prev);
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace detail

// --- Widget<ScopedEnum T> method bodies ---

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Widget<T>::record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
    size_t idx = enum_index(field.get());
    std::string label = enum_label<T>(idx);
    detail::DropdownLabelResolver resolver{
        .label_at = [](size_t i) { return enum_label<T>(i); },
        .count = enum_count<T>()
    };
    detail::dropdown_record(dl, node, label, resolver);
}

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Widget<T>::handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node) {
    size_t current = enum_index(field.get());
    constexpr size_t count = enum_count<T>();
    detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) { field.set(enum_from_index<T>(idx)); });
}

// --- Widget<Dropdown<T>> method bodies ---

template <ScopedEnum T>
void Widget<Dropdown<T>>::record(DrawList& dl, const Field<Dropdown<T>>& field,
                                    WidgetNode& node) {
    auto& dd = field.get();
    size_t idx = enum_index(dd.value);
    bool has_custom = !dd.labels.empty();
    std::string label = has_custom ? dd.labels[idx] : enum_label<T>(idx);
    detail::DropdownLabelResolver resolver{
        .label_at = [&dd, has_custom](size_t i) -> std::string {
            return has_custom ? dd.labels[i] : enum_label<T>(i);
        },
        .count = enum_count<T>()
    };
    detail::dropdown_record(dl, node, label, resolver);
}

template <ScopedEnum T>
void Widget<Dropdown<T>>::handle_input(Field<Dropdown<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    auto dd = field.get();
    size_t current = enum_index(dd.value);
    constexpr size_t count = enum_count<T>();
    detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) {
            dd.value = enum_from_index<T>(idx);
            field.set(dd);
        });
}

} // namespace prism::ui
