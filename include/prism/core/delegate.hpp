#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <algorithm>
#include <memory>
#if __cpp_impl_reflection
#include <meta>
#endif
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

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

enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs };

enum class ScrollBarPolicy : uint8_t { Auto, Always, Never };
enum class ScrollEventPolicy : uint8_t { ConsumeAlways, BubbleAtBounds };

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

// Sentinel: masked text field (displays bullets instead of actual text)
template <StringLike T = std::string>
struct Password {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;
    bool operator==(const Password&) const = default;
};

// Sentinel: multiline text editor with character wrapping
template <StringLike T = std::string>
struct TextArea {
    T value{};
    std::string placeholder{};
    size_t max_length = 0;   // 0 = unlimited
    size_t rows = 6;         // visible lines, determines widget height
    bool operator==(const TextArea&) const = default;
};

// Monospace text measurement -- single replacement point for future TextMetrics
inline float char_width(float font_size) { return 0.6f * font_size; }

// Ephemeral cursor state for text editing delegates
struct TextEditState {
    size_t cursor = 0;
    float scroll_offset = 0.f;
};

// Ephemeral cursor state for multiline text editing
struct TextAreaEditState {
    size_t cursor = 0;
    float scroll_y = 0.f;
};

// Ephemeral scroll state (stored in WidgetNode::edit_state)
struct ScrollState {
    DY offset_y{0};
    DX offset_x{0};
    Height content_h{0};
    Width content_w{0};
    Height viewport_h{0};
    Width viewport_w{0};
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    uint8_t show_ticks = 0;
};

// Sentinel: scroll container with observable position
struct ScrollArea {
    ScrollBarPolicy scrollbar = ScrollBarPolicy::Auto;
    ScrollEventPolicy event_policy = ScrollEventPolicy::BubbleAtBounds;
    DY scroll_y{0};
    DX scroll_x{0};
    bool operator==(const ScrollArea&) const = default;
};

// Concept: scoped enum type
template <typename T>
concept ScopedEnum = std::is_scoped_enum_v<T>;

// Enum introspection helpers for scoped enums
#if __cpp_impl_reflection

template <ScopedEnum T>
consteval size_t enum_count() {
    return std::meta::enumerators_of(^^T).size();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    std::string result;
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = std::string(std::meta::identifier_of(e));
        ++i;
    }
    return result;
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    size_t result = 0, i = 0;
    template for (constexpr auto e : enums) {
        if ([:e:] == value) result = i;
        ++i;
    }
    return result;
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    static constexpr auto enums = std::define_static_array(
        std::meta::enumerators_of(^^T));
    T result{};
    size_t i = 0;
    template for (constexpr auto e : enums) {
        if (i == index) result = [:e:];
        ++i;
    }
    return result;
}

#else

#include <magic_enum/magic_enum.hpp>

template <ScopedEnum T>
constexpr size_t enum_count() {
    return magic_enum::enum_count<T>();
}

template <ScopedEnum T>
std::string enum_label(size_t index) {
    auto values = magic_enum::enum_values<T>();
    return std::string(magic_enum::enum_name(values[index]));
}

template <ScopedEnum T>
constexpr size_t enum_index(T value) {
    return magic_enum::enum_index(value).value();
}

template <ScopedEnum T>
T enum_from_index(size_t index) {
    return magic_enum::enum_values<T>()[index];
}

#endif

// Sentinel: dropdown with optional custom labels
template <ScopedEnum T>
struct Dropdown {
    T value{};
    std::vector<std::string> labels{};  // empty = use reflection
    bool operator==(const Dropdown&) const = default;
};

// Ephemeral state for open dropdown popup
struct DropdownEditState {
    bool open = false;
    size_t highlighted = 0;
    Rect popup_rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
};

// Sentinel: tab bar — selected index only; tab names defined in view()
template <typename S = void>
struct TabBar {
    size_t selected = 0;
    bool operator==(const TabBar&) const = default;
};

template <typename S>
    requires (!std::is_void_v<S>)
struct TabBar<S> {
    size_t selected = 0;
    S pages{};
    // Only compare selected — pages contains non-copyable sub-components
    bool operator==(const TabBar& other) const { return selected == other.selected; }
};

// Ephemeral state for tab bar hover tracking and header hit regions
struct TabBarEditState {
    std::optional<size_t> hovered_tab;
    std::vector<std::pair<float, float>> header_x_ranges;
};

// Closed set of ephemeral widget states stored in WidgetNode::edit_state.
// std::shared_ptr<void> holds type-erased lifetime (e.g. virtual list row Field<T>).
struct VirtualListState;
struct TableState;
struct TabsState;

using EditState = std::variant<
    std::monostate,
    TextEditState,
    TextAreaEditState,
    DropdownEditState,
    ScrollState,
    TabBarEditState,
    std::shared_ptr<VirtualListState>,
    std::shared_ptr<TableState>,
    std::shared_ptr<TabsState>,
    std::shared_ptr<void>
>;

// WidgetNode is defined in widget_tree.hpp; declared here so delegate
// signatures can take WidgetNode& without creating a circular include.
// The accessor below lets delegate bodies reach visual_state without
// needing the complete type in this header.
struct WidgetNode;

// Declared here, defined in widget_tree.hpp (after WidgetNode is complete).
const WidgetVisualState& node_vs(const WidgetNode& n);

namespace detail {
inline Rect make_rect(float x, float y, float w, float h) {
    return {Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}};
}
inline Point make_point(float x, float y) {
    return {X{x}, Y{y}};
}
} // namespace detail

// Primary template: default delegate for any Field<T>.
// Renders only a filled rect, ignores input.
template <typename T>
struct Delegate {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>&, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// StringLike specialization: displays the string value
template <StringLike T>
struct Delegate<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto bg = vs.hovered ? Color::rgba(60, 60, 72) : Color::rgba(50, 50, 60);
        dl.filled_rect(detail::make_rect(0, 0, 200, 30), bg);
        dl.text(std::string(field.get().data(), field.get().size()),
                detail::make_point(4, 4), 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// Sentinel: checkbox with label
struct Checkbox {
    bool checked = false;
    std::string label;
    bool operator==(const Checkbox&) const = default;
};

// Shared checkbox box rendering for Delegate<bool> and Delegate<Checkbox>
inline void draw_check_box(DrawList& dl, float x, float y, bool checked,
                           const WidgetVisualState& vs) {
    constexpr float box_size = 16.f;
    constexpr float border = 1.5f;

    if (checked) {
        auto fill = vs.pressed  ? Color::rgba(0, 100, 170)
                  : vs.hovered  ? Color::rgba(0, 140, 220)
                  :               Color::rgba(0, 120, 200);
        dl.filled_rect(detail::make_rect(x, y, box_size, box_size), fill);
        dl.text("\xe2\x9c\x93", detail::make_point(x + 2, y + 1), 13, Color::rgba(255, 255, 255)); // checkmark
    } else {
        auto fill = vs.pressed  ? Color::rgba(35, 35, 42)
                  : vs.hovered  ? Color::rgba(55, 55, 65)
                  :               Color::rgba(45, 45, 55);
        dl.filled_rect(detail::make_rect(x, y, box_size, box_size), fill);
    }
    dl.rect_outline(detail::make_rect(x, y, box_size, box_size),
                    vs.hovered ? Color::rgba(120, 120, 135) : Color::rgba(90, 90, 105),
                    border);
}

template <>
struct Delegate<Checkbox> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f, widget_h = 30.f;
    static constexpr float box_size = 16.f;

    static void record(DrawList& dl, const Field<Checkbox>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& cb = field.get();

        auto bg = vs.hovered ? Color::rgba(55, 55, 65) : Color::rgba(45, 45, 55);
        dl.filled_rect(detail::make_rect(0, 0, widget_w, widget_h), bg);

        float box_y = (widget_h - box_size) / 2.f;
        draw_check_box(dl, 8, box_y, cb.checked, vs);

        if (!cb.label.empty())
            dl.text(cb.label, detail::make_point(32, 7), 14, Color::rgba(220, 220, 220));

        if (vs.focused)
            dl.rect_outline(detail::make_rect(-1, -1, widget_w + 2, widget_h + 2),
                            Color::rgba(80, 160, 240), 2.0f);
    }

    static void handle_input(Field<Checkbox>& field, const InputEvent& ev, WidgetNode&) {
        bool activate = false;
        if (auto* mb = std::get_if<MouseButton>(&ev))
            activate = mb->pressed;
        else if (auto* kp = std::get_if<KeyPress>(&ev))
            activate = (kp->key == keys::space || kp->key == keys::enter);

        if (activate) {
            auto cb = field.get();
            cb.checked = !cb.checked;
            field.set(cb);
        }
    }
};

// bool specialization: toggle widget
template <>
struct Delegate<bool> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<bool>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        constexpr float widget_w = 200.f, widget_h = 30.f;
        constexpr float box_size = 16.f;

        auto bg = vs.hovered ? Color::rgba(55, 55, 65) : Color::rgba(45, 45, 55);
        dl.filled_rect(detail::make_rect(0, 0, widget_w, widget_h), bg);

        float box_y = (widget_h - box_size) / 2.f;
        draw_check_box(dl, 8, box_y, field.get(), vs);

        if (vs.focused)
            dl.rect_outline(detail::make_rect(-1, -1, widget_w + 2, widget_h + 2),
                            Color::rgba(80, 160, 240), 2.0f);
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

    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode&) {
        dl.filled_rect(detail::make_rect(0, 0, 200, 24), Color::rgba(40, 40, 48));
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                detail::make_point(4, 4), 14, Color::rgba(180, 180, 190));
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

    static void record(DrawList& dl, const Field<Slider<T>>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& s = field.get();
        float r = ratio(s);
        float track_y = (widget_h - track_h) / 2.f;

        auto track_bg = vs.hovered ? Color::rgba(70, 70, 82) : Color::rgba(60, 60, 70);
        dl.filled_rect(detail::make_rect(0, track_y, track_w, track_h), track_bg);

        auto thumb_color = vs.pressed ? Color::rgba(0, 120, 180)
                         : vs.hovered ? Color::rgba(0, 160, 220)
                         : Color::rgba(0, 140, 200);
        float thumb_x = r * (track_w - thumb_w);
        dl.filled_rect(detail::make_rect(thumb_x, 0, thumb_w, widget_h), thumb_color);
        if (vs.focused)
            dl.rect_outline(detail::make_rect(-1, -1, track_w + 2, widget_h + 2), Color::rgba(80, 160, 240), 2.0f);
    }

    static void apply_position(Field<Slider<T>>& field, float x) {
        float t = std::clamp(x / track_w, 0.f, 1.f);
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

    static void handle_input(Field<Slider<T>>& field, const InputEvent& ev, WidgetNode& node) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            apply_position(field, mb->position.x.raw());
        if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(node).pressed)
            apply_position(field, mm->position.x.raw());
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

    static void record(DrawList& dl, const Field<Button>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        Color bg = vs.pressed ? Color::rgba(30, 90, 160)
                 : vs.hovered ? Color::rgba(50, 120, 200)
                 : Color::rgba(40, 105, 180);
        dl.filled_rect(detail::make_rect(0, 0, 200, 32), bg);
        dl.rect_outline(detail::make_rect(0, 0, 200, 32), Color::rgba(60, 140, 220), 1.0f);
        dl.text(field.get().text, detail::make_point(8, 7), 14, Color::rgba(240, 240, 240));
        if (vs.focused)
            dl.rect_outline(detail::make_rect(-2, -2, 204, 36), Color::rgba(80, 160, 240), 2.0f);
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
    static constexpr float widget_w = 200.f;
    static constexpr float widget_h = 30.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float cursor_w = 2.f;

    // Defined in widget_tree.hpp (after WidgetNode is complete).
    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextField<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Delegate<Password<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f;
    static constexpr float widget_h = 30.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float cursor_w = 2.f;

    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<Password<T>>& field, WidgetNode& node);
    static void handle_input(Field<Password<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Delegate<TextArea<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr float widget_w = 200.f;
    static constexpr float padding = 4.f;
    static constexpr float font_size = 14.f;
    static constexpr float line_height = font_size * 1.4f;
    static constexpr float cursor_w = 2.f;

    static const TextAreaEditState& get_edit_state(const WidgetNode& node);
    static TextAreaEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextArea<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextArea<T>>& field, const InputEvent& ev, WidgetNode& node);
};

// ScopedEnum delegate -- declared here, defined in widget_tree.hpp
template <ScopedEnum T>
    requires (!TextEditable<T>)
struct Delegate<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node);
    static void handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node);
};

template <ScopedEnum T>
struct Delegate<Dropdown<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Dropdown<T>>& field, WidgetNode& node);
    static void handle_input(Field<Dropdown<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Delegate<TabBar<>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<TabBar<>>& field, WidgetNode& node);
    static void handle_input(Field<TabBar<>>& field, const InputEvent& ev, WidgetNode& node);
};

} // namespace prism
