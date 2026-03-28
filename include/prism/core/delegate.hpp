#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace prism {

struct WidgetVisualState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
};

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

enum class FocusPolicy : uint8_t { none, tab_and_click };

// Sentinel: read-only label
template <StringLike T = std::string>
struct Label {
    T value{};
    bool operator==(const Label&) const = default;
};

// Concept: type wrapping a StringLike value (for editable text delegates)
template <typename T>
concept TextEditable = requires(const T& t) {
    { t.value } -> StringLike;
};

// Sentinel: single-line editable text field
template <StringLike T = std::string>
struct TextField {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const TextField&) const = default;
};

// Monospace text measurement — single replacement point for future TextMetrics
inline float char_width(float font_size) { return 0.6f * font_size; }

// Ephemeral cursor state for text editing delegates
struct TextEditState {
    size_t cursor = 0;
    float scroll_offset = 0.f;
};

// WidgetNode is defined in widget_tree.hpp; declared here so delegate
// signatures can take WidgetNode& without creating a circular include.
// The accessor below lets delegate bodies reach visual_state without
// needing the complete type in this header.
struct WidgetNode;

// Declared here, defined in widget_tree.hpp (after WidgetNode is complete).
const WidgetVisualState& node_vs(const WidgetNode& n);

// Primary template: default delegate for any Field<T>.
// Renders only a filled rect, ignores input.
template <typename T>
struct Delegate {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>&, const WidgetNode& node) {
        auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect({0, 0, 200, 30}, bg);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// StringLike specialization: displays the string value
template <StringLike T>
struct Delegate<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>& field, const WidgetNode& node) {
        auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect({0, 0, 200, 30}, bg);
        dl.text(std::string(field.get().data(), field.get().size()),
                {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// bool specialization: toggle widget
template <>
struct Delegate<bool> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<bool>& field, const WidgetNode& node) {
        auto& vs = node_vs(node);
        Color bg;
        if (field.get()) {
            bg = vs.pressed ? Color::rgba(0, 100, 65)
               : vs.hovered ? Color::rgba(0, 140, 95)
               : Color::rgba(0, 120, 80);
        } else {
            bg = vs.pressed ? Color::rgba(40, 40, 48)
               : vs.hovered ? Color::rgba(60, 60, 72)
               : Color::rgba(50, 50, 60);
        }
        dl.filled_rect({0, 0, 200, 30}, bg);
        if (vs.focused)
            dl.rect_outline({-1, -1, 202, 32}, Color::rgba(80, 160, 240), 2.0f);
    }

    static void handle_input(Field<bool>& field, const InputEvent& ev, WidgetNode&) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
            field.set(!field.get());
        } else if (auto* kp = std::get_if<KeyPress>(&ev);
                   kp && (kp->key == keys::space || kp->key == keys::enter)) {
            field.set(!field.get());
        }
    }
};

template <StringLike T>
struct Delegate<Label<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<Label<T>>& field, const WidgetNode&) {
        dl.filled_rect({0, 0, 200, 24}, Color::rgba(40, 40, 48));
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                {4, 4}, 14, Color::rgba(180, 180, 190));
    }

    static void handle_input(Field<Label<T>>&, const InputEvent&, WidgetNode&) {}
};

// Sentinel: numeric slider with min/max/step bounds
template <Numeric T = double>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
    bool operator==(const Slider&) const = default;
};

template <Numeric T>
struct Delegate<Slider<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float track_w = 200.f;
    static constexpr float track_h = 6.f;
    static constexpr float thumb_w = 12.f;
    static constexpr float widget_h = 30.f;

    static float ratio(const Slider<T>& s) {
        if (s.max == s.min) return 0.f;
        return static_cast<float>(s.value - s.min) / static_cast<float>(s.max - s.min);
    }

    static void record(DrawList& dl, const Field<Slider<T>>& field, const WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& s = field.get();
        float r = ratio(s);
        float track_y = (widget_h - track_h) / 2.f;

        auto track_bg = vs.hovered ? Color::rgba(70, 70, 82) : Color::rgba(60, 60, 70);
        dl.filled_rect({0, track_y, track_w, track_h}, track_bg);

        auto thumb_color = vs.pressed ? Color::rgba(0, 120, 180)
                         : vs.hovered ? Color::rgba(0, 160, 220)
                         : Color::rgba(0, 140, 200);
        float thumb_x = r * (track_w - thumb_w);
        dl.filled_rect({thumb_x, 0, thumb_w, widget_h}, thumb_color);
        if (vs.focused)
            dl.rect_outline({-1, -1, track_w + 2, widget_h + 2}, Color::rgba(80, 160, 240), 2.0f);
    }

    static void handle_input(Field<Slider<T>>& field, const InputEvent& ev, WidgetNode&) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
            float t = std::clamp(mb->position.x / track_w, 0.f, 1.f);
            auto& s = field.get();
            T raw = static_cast<T>(s.min + t * (s.max - s.min));
            Slider<T> updated = s;
            if (s.step != T{0}) {
                T steps = static_cast<T>((raw - s.min + s.step / T{2}) / s.step);
                updated.value = std::clamp(static_cast<T>(s.min + steps * s.step), s.min, s.max);
            } else {
                updated.value = raw;
            }
            field.set(updated);
        }
    }
};

// Sentinel: clickable button with text label
struct Button {
    std::string text;
    uint64_t click_count = 0;
    bool operator==(const Button&) const = default;
};

template <>
struct Delegate<Button> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Button>& field, const WidgetNode& node) {
        auto& vs = node_vs(node);
        Color bg = vs.pressed ? Color::rgba(30, 90, 160)
                 : vs.hovered ? Color::rgba(50, 120, 200)
                 : Color::rgba(40, 105, 180);
        dl.filled_rect({0, 0, 200, 32}, bg);
        dl.rect_outline({0, 0, 200, 32}, Color::rgba(60, 140, 220), 1.0f);
        dl.text(field.get().text, {8, 7}, 14, Color::rgba(240, 240, 240));
        if (vs.focused)
            dl.rect_outline({-2, -2, 204, 36}, Color::rgba(80, 160, 240), 2.0f);
    }

    static void handle_input(Field<Button>& field, const InputEvent& ev, WidgetNode&) {
        bool activate = false;
        if (auto* mb = std::get_if<MouseButton>(&ev))
            activate = mb->pressed;
        else if (auto* kp = std::get_if<KeyPress>(&ev))
            activate = (kp->key == keys::space || kp->key == keys::enter);

        if (activate) {
            auto btn = field.get();
            btn.click_count++;
            field.set(btn);
        }
    }
};

template <StringLike T>
struct Delegate<TextField<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);

    static void record(DrawList& dl, const Field<TextField<T>>& field, const WidgetNode& node);
    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};

} // namespace prism
