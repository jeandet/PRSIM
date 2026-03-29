#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/delegate.hpp>
#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>
#include <prism/core/layout.hpp>
#include <prism/core/reflect.hpp>
#include <prism/core/scene_snapshot.hpp>
#include <prism/core/state.hpp>

#include <any>
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace prism {

enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas };

struct WidgetNode {
    WidgetId id = 0;
    bool dirty = false;
    bool is_container = false;
    FocusPolicy focus_policy = FocusPolicy::none;
    WidgetVisualState visual_state;
    std::any edit_state;
    DrawList draws;
    DrawList overlay_draws;
    std::vector<Connection> connections;
    std::vector<WidgetNode> children;
    SenderHub<const InputEvent&> on_input;
    std::function<void(WidgetNode&)> wire;
    std::function<void(WidgetNode&)> record;
    LayoutKind layout_kind = LayoutKind::Default;
    Rect canvas_bounds{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
};

// Defined here (after WidgetNode is complete) for use in delegate.hpp bodies.
inline const WidgetVisualState& node_vs(const WidgetNode& n) { return n.visual_state; }

// --- Shared text field helpers (used by TextField and Password delegates) ---

namespace detail {

inline const TextEditState& get_text_edit_state(const WidgetNode& node) {
    static const TextEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const TextEditState&>(node.edit_state);
}

inline TextEditState& ensure_text_edit_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = TextEditState{};
    return std::any_cast<TextEditState&>(node.edit_state);
}

inline std::string mask_string(size_t len) {
    std::string result;
    result.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
        result += "\xe2\x97\x8f";
    return result;
}

constexpr float tf_widget_w = 200.f;
constexpr float tf_widget_h = 30.f;
constexpr float tf_padding = 4.f;
constexpr float tf_font_size = 14.f;
constexpr float tf_cursor_w = 2.f;

template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node,
                       DisplayFn display_fn) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    auto& es = get_text_edit_state(node);
    float cw = char_width(tf_font_size);

    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect(make_rect(0, 0, tf_widget_w, tf_widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, tf_widget_w + 2, tf_widget_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);

    float text_area_w = tf_widget_w - 2 * tf_padding;
    dl.clip_push(make_point(tf_padding, 0), Size{Width{text_area_w}, Height{tf_widget_h}});

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, make_point(0, tf_padding + 2), tf_font_size,
                Color::rgba(120, 120, 130));
    } else {
        float text_x = -es.scroll_offset;
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, make_point(text_x, tf_padding + 2), tf_font_size,
                Color::rgba(220, 220, 220));
    }

    if (vs.focused) {
        float cursor_x = static_cast<float>(es.cursor) * cw - es.scroll_offset;
        dl.filled_rect(make_rect(cursor_x, tf_padding, tf_cursor_w, tf_widget_h - 2 * tf_padding),
                       Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
}

template <typename Sentinel>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = ensure_text_edit_state(node);
    auto sf = field.get();
    auto len = sf.value.size();

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = ti->text;
        if (sf.max_length > 0 && len + to_insert.size() > sf.max_length) {
            size_t room = (len < sf.max_length) ? sf.max_length - len : 0;
            to_insert = to_insert.substr(0, room);
        }
        if (!to_insert.empty()) {
            std::string v(sf.value.data(), sf.value.size());
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::backspace && es.cursor > 0) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor - 1, 1);
            es.cursor--;
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor, 1);
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        } else if (kp->key == keys::left && es.cursor > 0) {
            es.cursor--;
            node.dirty = true;
        } else if (kp->key == keys::right && es.cursor < len) {
            es.cursor++;
            node.dirty = true;
        } else if (kp->key == keys::home && es.cursor > 0) {
            es.cursor = 0;
            node.dirty = true;
        } else if (kp->key == keys::end && es.cursor < len) {
            es.cursor = len;
            node.dirty = true;
        }
    } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float cw = char_width(tf_font_size);
        float rel_x = mb->position.x.raw() - tf_padding + es.scroll_offset;
        size_t pos = static_cast<size_t>(
            std::clamp(rel_x / cw + 0.5f, 0.f, static_cast<float>(len)));
        if (pos != es.cursor) {
            es.cursor = pos;
            node.dirty = true;
        }
    }

    float cw = char_width(tf_font_size);
    float text_area_w = tf_widget_w - 2 * tf_padding;
    float cursor_px = static_cast<float>(es.cursor) * cw;
    if (cursor_px - es.scroll_offset > text_area_w)
        es.scroll_offset = cursor_px - text_area_w;
    if (cursor_px < es.scroll_offset)
        es.scroll_offset = cursor_px;
}

// --- TextArea helpers ---

struct LineSpan {
    size_t start;
    size_t length;
};

struct LineCol {
    size_t line;
    size_t col;
};

inline std::vector<LineSpan> wrap_lines(std::string_view text, float text_area_w, float char_w) {
    std::vector<LineSpan> result;
    size_t max_chars = static_cast<size_t>(text_area_w / char_w);
    if (max_chars == 0) max_chars = 1;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        size_t logical_end = (nl == std::string_view::npos) ? text.size() : nl;
        size_t logical_len = logical_end - pos;

        if (logical_len == 0) {
            result.push_back({pos, 0});
        } else {
            size_t offset = 0;
            while (offset < logical_len) {
                size_t chunk = std::min(max_chars, logical_len - offset);
                result.push_back({pos + offset, chunk});
                offset += chunk;
            }
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    if (result.empty()) result.push_back({0, 0});
    return result;
}

inline LineCol cursor_to_line_col(size_t cursor, std::span<const LineSpan> lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t line_end = lines[i].start + lines[i].length;
        bool is_last = (i + 1 == lines.size());
        if (cursor < line_end || (cursor == line_end && is_last))
            return {i, cursor - lines[i].start};
        if (!is_last && lines[i + 1].start > line_end) {
            if (cursor <= line_end)
                return {i, cursor - lines[i].start};
        }
    }
    auto& last = lines.back();
    return {lines.size() - 1, last.length};
}

inline size_t line_col_to_cursor(size_t line, size_t col, std::span<const LineSpan> lines) {
    if (line >= lines.size()) line = lines.size() - 1;
    size_t clamped_col = std::min(col, lines[line].length);
    return lines[line].start + clamped_col;
}

inline const TextAreaEditState& get_text_area_edit_state(const WidgetNode& node) {
    static const TextAreaEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const TextAreaEditState&>(node.edit_state);
}

inline TextAreaEditState& ensure_text_area_edit_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = TextAreaEditState{};
    return std::any_cast<TextAreaEditState&>(node.edit_state);
}

constexpr float ta_widget_w = 200.f;
constexpr float ta_padding = 4.f;
constexpr float ta_font_size = 14.f;
constexpr float ta_line_height = ta_font_size * 1.4f;
constexpr float ta_cursor_w = 2.f;

template <typename Sentinel>
void text_area_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    auto& es = get_text_area_edit_state(node);
    float cw = char_width(ta_font_size);
    float text_area_w = ta_widget_w - 2 * ta_padding;
    float text_area_h = static_cast<float>(sf.rows) * ta_line_height;
    float widget_h = ta_padding * 2 + text_area_h;

    auto bg = vs.focused ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect(make_rect(0, 0, ta_widget_w, widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, ta_widget_w + 2, widget_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);

    dl.clip_push(make_point(ta_padding, ta_padding), Size{Width{text_area_w}, Height{text_area_h}});

    auto wrapped = wrap_lines(std::string_view(sf.value.data(), sf.value.size()),
                              text_area_w, cw);

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, make_point(0, 2), ta_font_size, Color::rgba(120, 120, 130));
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            float y = static_cast<float>(i) * ta_line_height - es.scroll_y;
            if (y + ta_line_height < 0) continue;
            if (y > text_area_h) break;
            if (wrapped[i].length > 0) {
                std::string line_text(sf.value.data() + wrapped[i].start, wrapped[i].length);
                dl.text(line_text, make_point(0, y + 2), ta_font_size, Color::rgba(220, 220, 220));
            }
        }
    }

    if (vs.focused) {
        auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
        float cx = static_cast<float>(col) * cw;
        float cy = static_cast<float>(line) * ta_line_height - es.scroll_y;
        dl.filled_rect(make_rect(cx, cy, ta_cursor_w, ta_line_height), Color::rgba(220, 220, 240));
    }

    dl.clip_pop();
}

template <typename Sentinel>
void text_area_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = ensure_text_area_edit_state(node);
    auto sf = field.get();
    auto len = sf.value.size();
    es.cursor = std::min(es.cursor, len);  // clamp if value changed externally
    float cw = char_width(ta_font_size);
    float text_area_w = ta_widget_w - 2 * ta_padding;
    float text_area_h = static_cast<float>(sf.rows) * ta_line_height;

    auto wrapped = wrap_lines(std::string_view(sf.value.data(), sf.value.size()),
                              text_area_w, cw);

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = ti->text;
        if (sf.max_length > 0 && len + to_insert.size() > sf.max_length) {
            size_t room = (len < sf.max_length) ? sf.max_length - len : 0;
            to_insert = to_insert.substr(0, room);
        }
        if (!to_insert.empty()) {
            std::string v(sf.value.data(), sf.value.size());
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::enter) {
            if (sf.max_length == 0 || len < sf.max_length) {
                std::string v(sf.value.data(), sf.value.size());
                v.insert(es.cursor, 1, '\n');
                es.cursor++;
                sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
                field.set(sf);
            }
        } else if (kp->key == keys::backspace && es.cursor > 0) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor - 1, 1);
            es.cursor--;
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            std::string v(sf.value.data(), sf.value.size());
            v.erase(es.cursor, 1);
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        } else if (kp->key == keys::left && es.cursor > 0) {
            es.cursor--;
            node.dirty = true;
        } else if (kp->key == keys::right && es.cursor < len) {
            es.cursor++;
            node.dirty = true;
        } else if (kp->key == keys::home) {
            auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
            size_t new_cursor = line_col_to_cursor(line, 0, wrapped);
            if (new_cursor != es.cursor) {
                es.cursor = new_cursor;
                node.dirty = true;
            }
        } else if (kp->key == keys::end) {
            auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
            size_t new_cursor = line_col_to_cursor(line, wrapped[line].length, wrapped);
            if (new_cursor != es.cursor) {
                es.cursor = new_cursor;
                node.dirty = true;
            }
        } else if (kp->key == keys::up) {
            auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
            if (line > 0) {
                es.cursor = line_col_to_cursor(line - 1, col, wrapped);
                node.dirty = true;
            }
        } else if (kp->key == keys::down) {
            auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
            if (line + 1 < wrapped.size()) {
                es.cursor = line_col_to_cursor(line + 1, col, wrapped);
                node.dirty = true;
            }
        }
    } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float rel_y = mb->position.y.raw() - ta_padding + es.scroll_y;
        size_t click_line = static_cast<size_t>(
            std::clamp(rel_y / ta_line_height, 0.f, static_cast<float>(wrapped.size() - 1)));
        float rel_x = mb->position.x.raw() - ta_padding;
        size_t click_col = static_cast<size_t>(
            std::clamp(rel_x / cw + 0.5f, 0.f, static_cast<float>(wrapped[click_line].length)));
        size_t new_cursor = line_col_to_cursor(click_line, click_col, wrapped);
        if (new_cursor != es.cursor) {
            es.cursor = new_cursor;
            node.dirty = true;
        }
    }

    // Recompute wrapped lines after possible value change
    auto new_wrapped = wrap_lines(
        std::string_view(field.get().value.data(), field.get().value.size()),
        text_area_w, cw);
    auto [cur_line, cur_col] = cursor_to_line_col(es.cursor, new_wrapped);
    float cursor_y = static_cast<float>(cur_line) * ta_line_height;

    if (cursor_y - es.scroll_y > text_area_h - ta_line_height)
        es.scroll_y = cursor_y - text_area_h + ta_line_height;
    if (cursor_y < es.scroll_y)
        es.scroll_y = cursor_y;
}

} // namespace detail

// --- Delegate<TextField<T>> method bodies ---

template <StringLike T>
const TextEditState& Delegate<TextField<T>>::get_edit_state(const WidgetNode& node) {
    return detail::get_text_edit_state(node);
}

template <StringLike T>
TextEditState& Delegate<TextField<T>>::ensure_edit_state(WidgetNode& node) {
    return detail::ensure_text_edit_state(node);
}

template <StringLike T>
void Delegate<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field,
                                    const WidgetNode& node) {
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return v; });
}

template <StringLike T>
void Delegate<TextField<T>>::handle_input(Field<TextField<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    detail::text_field_handle_input(field, ev, node);
}

// --- Delegate<Password<T>> method bodies ---

template <StringLike T>
const TextEditState& Delegate<Password<T>>::get_edit_state(const WidgetNode& node) {
    return detail::get_text_edit_state(node);
}

template <StringLike T>
TextEditState& Delegate<Password<T>>::ensure_edit_state(WidgetNode& node) {
    return detail::ensure_text_edit_state(node);
}

template <StringLike T>
void Delegate<Password<T>>::record(DrawList& dl, const Field<Password<T>>& field,
                                    const WidgetNode& node) {
    detail::text_field_record(dl, field, node,
        [](const std::string& v) { return detail::mask_string(v.size()); });
}

template <StringLike T>
void Delegate<Password<T>>::handle_input(Field<Password<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    detail::text_field_handle_input(field, ev, node);
}

// --- Delegate<TextArea<T>> method bodies ---

template <StringLike T>
const TextAreaEditState& Delegate<TextArea<T>>::get_edit_state(const WidgetNode& node) {
    return detail::get_text_area_edit_state(node);
}

template <StringLike T>
TextAreaEditState& Delegate<TextArea<T>>::ensure_edit_state(WidgetNode& node) {
    return detail::ensure_text_area_edit_state(node);
}

template <StringLike T>
void Delegate<TextArea<T>>::record(DrawList& dl, const Field<TextArea<T>>& field,
                                   const WidgetNode& node) {
    detail::text_area_record(dl, field, node);
}

template <StringLike T>
void Delegate<TextArea<T>>::handle_input(Field<TextArea<T>>& field, const InputEvent& ev,
                                         WidgetNode& node) {
    detail::text_area_handle_input(field, ev, node);
}

// --- Shared dropdown rendering/input helpers ---

namespace detail {

struct DropdownLabelResolver {
    std::function<std::string(size_t)> label_at;
    size_t count;
};

inline const DropdownEditState& get_dropdown_state(const WidgetNode& node) {
    static const DropdownEditState default_state;
    if (!node.edit_state.has_value()) return default_state;
    return std::any_cast<const DropdownEditState&>(node.edit_state);
}

inline DropdownEditState& ensure_dropdown_state(WidgetNode& node) {
    if (!node.edit_state.has_value())
        node.edit_state = DropdownEditState{};
    return std::any_cast<DropdownEditState&>(node.edit_state);
}

constexpr float dd_widget_w = 200.f;
constexpr float dd_widget_h = 30.f;
constexpr float dd_padding = 8.f;
constexpr float dd_font_size = 14.f;
constexpr float dd_option_h = 28.f;

inline void dropdown_record(DrawList& dl, WidgetNode& node,
                            const std::string& current_label,
                            const DropdownLabelResolver& resolver) {
    auto& vs = node_vs(node);
    auto& es = get_dropdown_state(node);

    auto bg = es.open    ? Color::rgba(65, 65, 78)
            : vs.hovered ? Color::rgba(55, 55, 68)
            : Color::rgba(45, 45, 55);
    dl.filled_rect(make_rect(0, 0, dd_widget_w, dd_widget_h), bg);

    dl.text(current_label, make_point(dd_padding, 7), dd_font_size, Color::rgba(220, 220, 220));

    dl.text("\xe2\x96\xbe", make_point(dd_widget_w - 20, 7), dd_font_size, Color::rgba(160, 160, 170));

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, dd_widget_w + 2, dd_widget_h + 2),
                        Color::rgba(80, 160, 240), 2.0f);

    node.overlay_draws.clear();
    if (es.open) {
        float popup_h = static_cast<float>(resolver.count) * dd_option_h;
        node.overlay_draws.filled_rect(
            make_rect(0, dd_widget_h, dd_widget_w, popup_h),
            Color::rgba(50, 50, 62));
        node.overlay_draws.rect_outline(
            make_rect(0, dd_widget_h, dd_widget_w, popup_h),
            Color::rgba(80, 80, 95), 1.0f);

        for (size_t i = 0; i < resolver.count; ++i) {
            float y = dd_widget_h + static_cast<float>(i) * dd_option_h;
            if (i == es.highlighted) {
                node.overlay_draws.filled_rect(
                    make_rect(0, y, dd_widget_w, dd_option_h),
                    Color::rgba(60, 100, 180));
            }
            node.overlay_draws.text(
                resolver.label_at(i),
                make_point(dd_padding, y + 6), dd_font_size,
                i == es.highlighted ? Color::rgba(255, 255, 255)
                                    : Color::rgba(200, 200, 210));
        }
    }
}

inline bool dropdown_handle_input(const InputEvent& ev, WidgetNode& node,
                                  size_t current_index, size_t count,
                                  std::function<void(size_t)> select) {
    auto& es = ensure_dropdown_state(node);
    bool changed = false;

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        if (es.open) {
            float rel_y = mb->position.y.raw() - dd_widget_h;
            if (rel_y >= 0 && rel_y < static_cast<float>(count) * dd_option_h
                && mb->position.x.raw() >= 0 && mb->position.x.raw() < dd_widget_w) {
                size_t idx = static_cast<size_t>(rel_y / dd_option_h);
                select(idx);
                changed = true;
            }
            es.open = false;
            node.dirty = true;
        } else {
            es.open = true;
            es.highlighted = current_index;
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

// --- Delegate<ScopedEnum T> method bodies ---

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Delegate<T>::record(DrawList& dl, const Field<T>& field, const WidgetNode& node) {
    size_t idx = enum_index(field.get());
    std::string label = enum_label<T>(idx);
    detail::DropdownLabelResolver resolver{
        .label_at = [](size_t i) { return enum_label<T>(i); },
        .count = enum_count<T>()
    };
    detail::dropdown_record(dl, const_cast<WidgetNode&>(node), label, resolver);
}

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Delegate<T>::handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node) {
    size_t current = enum_index(field.get());
    constexpr size_t count = enum_count<T>();
    detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) { field.set(enum_from_index<T>(idx)); });
}

// --- Delegate<Dropdown<T>> method bodies ---

template <ScopedEnum T>
void Delegate<Dropdown<T>>::record(DrawList& dl, const Field<Dropdown<T>>& field,
                                    const WidgetNode& node) {
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
    detail::dropdown_record(dl, const_cast<WidgetNode&>(node), label, resolver);
}

template <ScopedEnum T>
void Delegate<Dropdown<T>>::handle_input(Field<Dropdown<T>>& field, const InputEvent& ev,
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

// index_ stores raw pointers into the tree -- valid only because the tree
// is fully built before build_index runs and never mutated after construction.
class WidgetTree {
public:
    class ViewBuilder {
        WidgetTree& tree_;
        WidgetNode& target_;
        std::vector<WidgetNode*> stack_;
        std::set<const void*> placed_;

        WidgetNode& current_parent() {
            return stack_.empty() ? target_ : *stack_.back();
        }

    public:
        struct CanvasHandle {
            WidgetNode& node_ref;
            WidgetTree& tree_ref;

            template <typename U>
            CanvasHandle& depends_on(Field<U>& field) {
                auto id = node_ref.id;
                node_ref.connections.push_back(
                    field.on_change().connect([&tree_ref = tree_ref, id](const U&) {
                        tree_ref.mark_dirty_by_id(id);
                    })
                );
                return *this;
            }
        };

    public:
        ViewBuilder(WidgetTree& tree, WidgetNode& target)
            : tree_(tree), target_(target) {}

        template <typename T>
        void widget(Field<T>& field) {
            placed_.insert(&field);
            current_parent().children.push_back(tree_.build_leaf(field));
        }

        [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }

        template <typename C>
        void component(C& comp) {
            current_parent().children.push_back(tree_.build_container(comp));
        }

        void row(std::invocable auto&& fn)    { push_container(LayoutKind::Row, fn); }
        void column(std::invocable auto&& fn) { push_container(LayoutKind::Column, fn); }

    private:
        void push_container(LayoutKind kind, std::invocable auto&& fn) {
            WidgetNode container;
            container.id = tree_.next_id_++;
            container.is_container = true;
            container.layout_kind = kind;
            auto& parent = current_parent();
            parent.children.push_back(std::move(container));
            stack_.push_back(&parent.children.back());
            fn();
            stack_.pop_back();
        }

    public:

        void spacer() {
            WidgetNode s;
            s.id = tree_.next_id_++;
            s.is_container = false;
            s.layout_kind = LayoutKind::Spacer;
            current_parent().children.push_back(std::move(s));
        }

        template <typename T>
            requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
                t.canvas(dl, r, n);
            }
        auto canvas(T& model) {
            WidgetNode node;
            node.id = tree_.next_id_++;
            node.is_container = false;
            node.layout_kind = LayoutKind::Canvas;

            node.record = [&model](WidgetNode& n) {
                n.draws.clear();
                model.canvas(n.draws, n.canvas_bounds, n);
            };
            node.record(node);

            if constexpr (requires(T& t, const InputEvent& ev, WidgetNode& n, Rect r) {
                               t.handle_canvas_input(ev, n, r);
                           }) {
                node.focus_policy = FocusPolicy::tab_and_click;
                node.wire = [&model](WidgetNode& n) {
                    n.connections.push_back(
                        n.on_input.connect([&model, &n](const InputEvent& ev) {
                            model.handle_canvas_input(ev, n, n.canvas_bounds);
                        })
                    );
                };
            }

            current_parent().children.push_back(std::move(node));
            return CanvasHandle{current_parent().children.back(), tree_};
        }

        void finalize() {
            if (target_.children.size() > 1) {
                WidgetNode wrapper;
                wrapper.id = tree_.next_id_++;
                wrapper.is_container = true;
                wrapper.layout_kind = LayoutKind::Column;
                wrapper.children = std::move(target_.children);
                target_.children.clear();
                target_.children.push_back(std::move(wrapper));
            }
            // Hoist single Row/Column child: avoids an unnecessary nesting level
            // when view() produces exactly one container (the common case).
            if (target_.children.size() == 1) {
                auto lk = target_.children[0].layout_kind;
                if (lk == LayoutKind::Row || lk == LayoutKind::Column) {
                    target_.layout_kind = lk;
                    target_.children = std::move(target_.children[0].children);
                }
            }
        }
    };

    template <typename Model>
    explicit WidgetTree(Model& model) {
        root_ = build_container(model);
        build_index(root_);
        clear_dirty();
    }

    WidgetTree(const WidgetTree&) = delete;
    WidgetTree& operator=(const WidgetTree&) = delete;

    [[nodiscard]] size_t leaf_count() const { return count_leaves(root_); }
    [[nodiscard]] bool any_dirty() const { return check_dirty(root_); }

    void clear_dirty() { clear_dirty_impl(root_); }

    void close_overlays() { close_overlays_impl(root_); }

    [[nodiscard]] std::vector<WidgetId> leaf_ids() const {
        std::vector<WidgetId> ids;
        collect_leaf_ids(root_, ids);
        return ids;
    }

    void dispatch(WidgetId id, const InputEvent& ev) {
        if (auto it = index_.find(id); it != index_.end())
            it->second->on_input.emit(ev);
    }

    void update_hover(std::optional<WidgetId> id) {
        WidgetId new_id = id.value_or(0);
        if (new_id == hovered_id_) return;
        if (auto it = index_.find(hovered_id_); it != index_.end()) {
            it->second->visual_state.hovered = false;
            it->second->dirty = true;
        }
        hovered_id_ = new_id;
        if (auto it = index_.find(hovered_id_); it != index_.end()) {
            it->second->visual_state.hovered = true;
            it->second->dirty = true;
        }
    }

    void set_pressed(WidgetId id, bool pressed) {
        if (auto it = index_.find(id); it != index_.end()) {
            it->second->visual_state.pressed = pressed;
            it->second->dirty = true;
        }
    }

    [[nodiscard]] Connection connect_input(WidgetId id, std::function<void(const InputEvent&)> cb) {
        if (auto it = index_.find(id); it != index_.end())
            return it->second->on_input.connect(std::move(cb));
        return {};
    }

    [[nodiscard]] WidgetId focused_id() const { return focused_id_; }

    [[nodiscard]] const std::vector<WidgetId>& focus_order() const { return focus_order_; }

    void set_focused(WidgetId id) {
        if (id == focused_id_) return;
        if (std::find(focus_order_.begin(), focus_order_.end(), id) == focus_order_.end()) return;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = false;
            it->second->dirty = true;
        }
        focused_id_ = id;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = true;
            it->second->dirty = true;
        }
    }

    void clear_focus() {
        if (focused_id_ == 0) return;
        if (auto it = index_.find(focused_id_); it != index_.end()) {
            it->second->visual_state.focused = false;
            it->second->dirty = true;
        }
        focused_id_ = 0;
    }

    void focus_next() {
        if (focus_order_.empty()) return;
        if (focused_id_ == 0) {
            set_focused(focus_order_.front());
            return;
        }
        auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
        if (it == focus_order_.end() || ++it == focus_order_.end())
            set_focused(focus_order_.front());
        else
            set_focused(*it);
    }

    void focus_prev() {
        if (focus_order_.empty()) return;
        if (focused_id_ == 0) {
            set_focused(focus_order_.back());
            return;
        }
        auto it = std::find(focus_order_.begin(), focus_order_.end(), focused_id_);
        if (it == focus_order_.begin())
            set_focused(focus_order_.back());
        else
            set_focused(*std::prev(it));
    }

    void mark_dirty_by_id(WidgetId id) {
        mark_dirty(root_, id);
    }

    [[nodiscard]] std::unique_ptr<SceneSnapshot> build_snapshot(float w, float h, uint64_t version) {
        refresh_dirty(root_);

        LayoutNode layout;
        // root_ is always a container; Spacer is only valid on non-container nodes
        assert(root_.layout_kind != LayoutKind::Spacer);
        layout.kind = (root_.layout_kind == LayoutKind::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        layout.id = root_.id;
        for (auto& c : root_.children)
            build_layout(c, layout);

        layout_measure(layout, LayoutAxis::Vertical);
        layout_arrange(layout, {Point{X{0}, Y{0}}, Size{Width{w}, Height{h}}});

        // Post-layout: update canvas nodes with their resolved bounds and re-record
        update_canvas_bounds(layout, root_);

        auto snap = std::make_unique<SceneSnapshot>();
        snap->version = version;
        layout_flatten(layout, *snap);
        return snap;
    }

private:
    WidgetNode root_;
    WidgetId next_id_ = 1;
    WidgetId hovered_id_ = 0;
    WidgetId focused_id_ = 0;
    std::vector<WidgetId> focus_order_;
    std::unordered_map<WidgetId, WidgetNode*> index_;

    template <typename Model>
    void check_unplaced_fields([[maybe_unused]] Model& model,
                               [[maybe_unused]] const std::set<const void*>& placed) {
#ifndef NDEBUG
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Model, std::meta::access_context::unchecked()));
        template for (constexpr auto m : members) {
            auto& member = model.[:m:];
            using M = std::remove_cvref_t<decltype(member)>;
            if constexpr (is_field_v<M>) {
                if (!placed.contains(&member)) {
                    std::fprintf(stderr, "[prism] warning: Field '%.*s' in %.*s not placed by view()\n",
                        static_cast<int>(std::meta::identifier_of(m).size()),
                        std::meta::identifier_of(m).data(),
                        static_cast<int>(std::meta::identifier_of(^^Model).size()),
                        std::meta::identifier_of(^^Model).data());
                }
            }
        }
#endif
    }

    void build_index(WidgetNode& node) {
        index_[node.id] = &node;
        if (node.wire) {
            node.wire(node);
            node.wire = nullptr;
        }
        if (!node.is_container && node.focus_policy != FocusPolicy::none)
            focus_order_.push_back(node.id);
        for (auto& c : node.children)
            build_index(c);
    }

    static size_t count_leaves(const WidgetNode& node) {
        if (!node.is_container)
            return node.layout_kind == LayoutKind::Spacer ? 0 : 1;
        size_t n = 0;
        for (auto& c : node.children) n += count_leaves(c);
        return n;
    }

    static bool check_dirty(const WidgetNode& node) {
        if (node.dirty) return true;
        for (auto& c : node.children)
            if (check_dirty(c)) return true;
        return false;
    }

    static void clear_dirty_impl(WidgetNode& node) {
        node.dirty = false;
        for (auto& c : node.children) clear_dirty_impl(c);
    }

    static void close_overlays_impl(WidgetNode& node) {
        if (!node.overlay_draws.empty()) {
            node.overlay_draws.clear();
            node.edit_state.reset();
            node.dirty = true;
        }
        for (auto& c : node.children) close_overlays_impl(c);
    }

    static void collect_leaf_ids(const WidgetNode& node, std::vector<WidgetId>& ids) {
        if (!node.is_container) {
            if (node.layout_kind != LayoutKind::Spacer)
                ids.push_back(node.id);
            return;
        }
        for (auto& c : node.children) collect_leaf_ids(c, ids);
    }

    static void refresh_dirty(WidgetNode& node) {
        if (node.dirty && node.record)
            node.record(node);
        for (auto& c : node.children)
            refresh_dirty(c);
    }

    static bool mark_dirty(WidgetNode& node, WidgetId id) {
        if (node.id == id) { node.dirty = true; return true; }
        for (auto& c : node.children)
            if (mark_dirty(c, id)) return true;
        return false;
    }

    static void update_canvas_bounds(LayoutNode& layout_node, WidgetNode& widget_root) {
        if (layout_node.kind == LayoutNode::Kind::Canvas) {
            auto* wn = find_widget_node(widget_root, layout_node.id);
            if (wn && wn->record) {
                wn->canvas_bounds = Rect{
                    Point{X{0}, Y{0}},
                    layout_node.allocated.extent
                };
                wn->record(*wn);
                layout_node.draws = wn->draws;
            }
            return;
        }
        for (auto& child : layout_node.children)
            update_canvas_bounds(child, widget_root);
    }

    static WidgetNode* find_widget_node(WidgetNode& node, WidgetId id) {
        if (node.id == id) return &node;
        for (auto& c : node.children) {
            auto* found = find_widget_node(c, id);
            if (found) return found;
        }
        return nullptr;
    }

    static void build_layout(WidgetNode& node, LayoutNode& parent) {
        using LK = LayoutKind;

        if (!node.is_container) {
            if (node.layout_kind == LK::Spacer) {
                LayoutNode spacer;
                spacer.kind = LayoutNode::Kind::Spacer;
                spacer.id = node.id;
                parent.children.push_back(std::move(spacer));
            } else if (node.layout_kind == LK::Canvas) {
                LayoutNode canvas;
                canvas.kind = LayoutNode::Kind::Canvas;
                canvas.id = node.id;
                canvas.draws = node.draws;
                canvas.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(canvas));
            } else {
                LayoutNode leaf;
                leaf.kind = LayoutNode::Kind::Leaf;
                leaf.id = node.id;
                leaf.draws = node.draws;
                leaf.overlay_draws = node.overlay_draws;
                parent.children.push_back(std::move(leaf));
            }
        } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
            LayoutNode container;
            container.kind = (node.layout_kind == LK::Row)
                ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
            container.id = node.id;
            for (auto& c : node.children)
                build_layout(c, container);
            parent.children.push_back(std::move(container));
        } else {
            for (auto& c : node.children)
                build_layout(c, parent);
        }
    }

    template <typename T>
    WidgetNode build_leaf(Field<T>& field) {
        WidgetNode node;
        node.id = next_id_++;
        node.is_container = false;
        node.focus_policy = Delegate<T>::focus_policy;

        node.record = [&field](WidgetNode& n) {
            n.draws.clear();
            n.overlay_draws.clear();
            Delegate<T>::record(n.draws, field, n);
        };
        node.record(node);

        node.wire = [&field](WidgetNode& n) {
            n.connections.push_back(
                n.on_input.connect([&field, &n](const InputEvent& ev) {
                    Delegate<T>::handle_input(field, ev, n);
                })
            );
        };

        auto id = node.id;
        node.connections.push_back(
            field.on_change().connect([this, id](const T&) {
                mark_dirty(root_, id);
            })
        );

        return node;
    }

    template <typename Model>
    WidgetNode build_container(Model& model) {
        WidgetNode container;
        container.id = next_id_++;
        container.is_container = true;

        if constexpr (requires(Model& m, ViewBuilder& vb) { m.view(vb); }) {
            ViewBuilder vb{*this, container};
            model.view(vb);
            check_unplaced_fields(model, vb.placed());
            vb.finalize();
        } else {
            static constexpr auto members = std::define_static_array(
                std::meta::nonstatic_data_members_of(
                    ^^Model, std::meta::access_context::unchecked()));

            template for (constexpr auto m : members) {
                auto& member = model.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;

                if constexpr (is_state_v<M>) {
                    // invisible observable — no widget
                } else if constexpr (is_field_v<M>) {
                    container.children.push_back(build_leaf(member));
                } else if constexpr (is_component_v<M>) {
                    container.children.push_back(build_container(member));
                }
            }
        }

        return container;
    }
};

} // namespace prism
