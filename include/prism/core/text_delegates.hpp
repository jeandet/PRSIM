#pragma once

#include <prism/core/widget_node.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace prism {
namespace detail {

inline const TextEditState& get_text_edit_state(const WidgetNode& node) {
    static const TextEditState default_state;
    auto* p = std::get_if<TextEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline TextEditState& ensure_text_edit_state(WidgetNode& node) {
    if (!std::holds_alternative<TextEditState>(node.edit_state))
        node.edit_state = TextEditState{};
    return std::get<TextEditState>(node.edit_state);
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

    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(make_rect(0, 0, tf_widget_w, tf_widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, tf_widget_w + 2, tf_widget_h + 2),
                        t.focus_ring, 2.0f);

    float text_area_w = tf_widget_w - 2 * tf_padding;
    dl.clip_push(make_point(tf_padding, 0), Size{Width{text_area_w}, Height{tf_widget_h}});

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, make_point(0, tf_padding + 2), tf_font_size,
                t.text_placeholder);
    } else {
        float text_x = -es.scroll_offset;
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, make_point(text_x, tf_padding + 2), tf_font_size,
                t.text);
    }

    if (vs.focused) {
        float cursor_x = static_cast<float>(es.cursor) * cw - es.scroll_offset;
        dl.filled_rect(make_rect(cursor_x, tf_padding, tf_cursor_w, tf_widget_h - 2 * tf_padding),
                       t.cursor);
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
    auto* p = std::get_if<TextAreaEditState>(&node.edit_state);
    return p ? *p : default_state;
}

inline TextAreaEditState& ensure_text_area_edit_state(WidgetNode& node) {
    if (!std::holds_alternative<TextAreaEditState>(node.edit_state))
        node.edit_state = TextAreaEditState{};
    return std::get<TextAreaEditState>(node.edit_state);
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

    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(make_rect(0, 0, ta_widget_w, widget_h), bg);

    if (vs.focused)
        dl.rect_outline(make_rect(-1, -1, ta_widget_w + 2, widget_h + 2),
                        t.focus_ring, 2.0f);

    dl.clip_push(make_point(ta_padding, ta_padding), Size{Width{text_area_w}, Height{text_area_h}});

    auto wrapped = wrap_lines(std::string_view(sf.value.data(), sf.value.size()),
                              text_area_w, cw);

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, make_point(0, 2), ta_font_size, t.text_placeholder);
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            float y = static_cast<float>(i) * ta_line_height - es.scroll_y;
            if (y + ta_line_height < 0) continue;
            if (y > text_area_h) break;
            if (wrapped[i].length > 0) {
                std::string line_text(sf.value.data() + wrapped[i].start, wrapped[i].length);
                dl.text(line_text, make_point(0, y + 2), ta_font_size, t.text);
            }
        }
    }

    if (vs.focused) {
        auto [line, col] = cursor_to_line_col(es.cursor, wrapped);
        float cx = static_cast<float>(col) * cw;
        float cy = static_cast<float>(line) * ta_line_height - es.scroll_y;
        dl.filled_rect(make_rect(cx, cy, ta_cursor_w, ta_line_height), t.cursor);
    }

    dl.clip_pop();
}

template <typename Sentinel>
void text_area_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = ensure_text_area_edit_state(node);
    auto sf = field.get();
    auto len = sf.value.size();
    es.cursor = std::min(es.cursor, len);
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
                                    WidgetNode& node) {
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
                                    WidgetNode& node) {
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
                                   WidgetNode& node) {
    detail::text_area_record(dl, field, node);
}

template <StringLike T>
void Delegate<TextArea<T>>::handle_input(Field<TextArea<T>>& field, const InputEvent& ev,
                                         WidgetNode& node) {
    detail::text_area_handle_input(field, ev, node);
}

} // namespace prism
