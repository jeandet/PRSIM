#pragma once

#include <prism/ui/context.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/input/input_event.hpp>

#include <algorithm>
#include <any>
#include <memory>
#if __cpp_impl_reflection
#include <meta>
#endif
#include <string>
#include <type_traits>
#include <vector>

namespace prism::ui {
using namespace prism::core;
using namespace prism::render;
using namespace prism::input;


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

enum class LayoutKind : uint8_t { Default, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle };

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
inline Width char_width(float font_size) { return Width{0.6f * font_size}; }

// Ephemeral cursor state for text editing delegates
struct TextEditState {
    size_t cursor = 0;
    DX scroll_offset{0};
};

// Ephemeral cursor state for multiline text editing
struct TextAreaEditState {
    size_t cursor = 0;
    DY scroll_y{0};
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

// Ephemeral split-pane state (stored in WidgetNode::edit_state on a Row/Column container)
struct SplitState {
    bool engaged = false;           // true once the user has dragged a handle at least once
    std::vector<float> pane_sizes;  // main-axis size per pane (excludes Handle thickness); valid when engaged
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
    std::vector<std::pair<X, X>> header_x_ranges;
};

// Type-erased ephemeral widget state stored in WidgetNode::edit_state.
// Any default-constructible type can be stored; get_or_create<S>() provides
// typed access with lazy initialization.
using EditState = std::any;

// WidgetNode is defined in widget_tree.hpp; declared here so delegate
// signatures can take WidgetNode& without creating a circular include.
// The accessor below lets delegate bodies reach visual_state without
// needing the complete type in this header.
struct WidgetNode;

// Declared here, defined in widget_node.hpp (after WidgetNode is complete).
const WidgetVisualState& node_vs(const WidgetNode& n);
Size node_allocated(const WidgetNode& n);
const Theme& node_theme(const WidgetNode& n);

namespace detail {
inline Rect make_rect(X x, Y y, Width w, Height h) {
    return {Point{x, y}, Size{w, h}};
}
inline Point make_point(X x, Y y) {
    return {x, y};
}
} // namespace detail

namespace detail {
inline constexpr Height default_widget_h{30.f};
inline constexpr Width default_widget_w{200.f};

inline Width widget_w(const WidgetNode& node) {
    auto sz = node_allocated(node);
    return sz.w.raw() > 0 ? sz.w : default_widget_w;
}
inline Height widget_h(const WidgetNode& node) {
    auto sz = node_allocated(node);
    return sz.h.raw() > 0 ? sz.h : default_widget_h;
}
} // namespace detail

// Primary template: default widget for any Field<T>.
// Renders a filled rect, ignores input.
template <typename T>
struct Widget {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>&, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, detail::widget_h(node)), bg);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

/// True if Widget<T> has been specialized with record() and handle_input().
template <typename T>
concept is_widget_v = requires(DrawList& dl, const Field<T>& cf, Field<T>& f,
                                const InputEvent& ev, WidgetNode& node) {
    Widget<T>::record(dl, cf, node);
    Widget<T>::handle_input(f, ev, node);
};

// Numeric specialization: displays the value as text
template <Numeric T>
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, detail::widget_h(node)), bg);
        dl.text(std::to_string(field.get()),
                detail::make_point(X{4}, Y{4}), 14, t.text);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// StringLike specialization: displays the string value
template <StringLike T>
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, detail::widget_h(node)), bg);
        dl.text(std::string(field.get().data(), field.get().size()),
                detail::make_point(X{4}, Y{4}), 14, t.text);
    }

    static void handle_input(Field<T>&, const InputEvent&, WidgetNode&) {}
};

// Sentinel: checkbox with label
struct Checkbox {
    bool checked = false;
    std::string label;
    bool operator==(const Checkbox&) const = default;
};

// Shared checkbox box rendering for Widget<bool> and Widget<Checkbox>
inline void draw_check_box(DrawList& dl, X x, Y y, bool checked,
                           const WidgetVisualState& vs, const Theme& t) {
    constexpr Width box_w{16.f};
    constexpr Height box_h{16.f};
    constexpr float border = 1.5f;

    if (checked) {
        auto fill = vs.pressed  ? t.accent_active
                  : vs.hovered  ? t.accent_hover
                  :               t.accent;
        dl.filled_rect(detail::make_rect(x, y, box_w, box_h), fill);
        dl.text("\xe2\x9c\x93", detail::make_point(x + DX{2.f}, y + DY{1.f}), 13, t.text_on_primary);
    } else {
        auto fill = vs.pressed  ? t.surface_active
                  : vs.hovered  ? t.surface_hover
                  :               t.surface;
        dl.filled_rect(detail::make_rect(x, y, box_w, box_h), fill);
    }
    dl.rect_outline(detail::make_rect(x, y, box_w, box_h),
                    vs.hovered ? t.border_hover : t.border,
                    border);
}

template <>
struct Widget<Checkbox> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Height widget_h{30.f};
    static constexpr Height box_size{16.f};

    static void record(DrawList& dl, const Field<Checkbox>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto& cb = field.get();
        Width w = detail::widget_w(node);

        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, widget_h), bg);

        Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
        draw_check_box(dl, X{8}, box_y, cb.checked, vs, t);

        if (!cb.label.empty())
            dl.text(cb.label, detail::make_point(X{32}, Y{7}), 14, t.text);

        if (vs.focused)
            dl.rect_outline(detail::make_rect(X{-1}, Y{-1}, w + Width{2.f}, widget_h + Height{2.f}),
                            t.focus_ring, 2.0f);
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
struct Widget<bool> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<bool>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        constexpr Height widget_h{30.f};
        constexpr Height box_size{16.f};
        Width w = detail::widget_w(node);

        auto bg = vs.hovered ? t.surface_hover : t.surface;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, widget_h), bg);

        Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
        draw_check_box(dl, X{8}, box_y, field.get(), vs, t);

        if (vs.focused)
            dl.rect_outline(detail::make_rect(X{-1}, Y{-1}, w + Width{2.f}, widget_h + Height{2.f}),
                            t.focus_ring, 2.0f);
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
struct Widget<Label<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;

    static void record(DrawList& dl, const Field<Label<T>>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, Height{24}), t.surface);
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                detail::make_point(X{4}, Y{4}), 14, t.text_muted);
    }

    static void handle_input(Field<Label<T>>&, const InputEvent&, WidgetNode&) {}
};

enum class Orientation { Horizontal, Vertical };

// Sentinel: numeric slider with min/max/step bounds
template <Numeric T = double, Orientation O = Orientation::Horizontal>
struct Slider {
    T value{};
    T min = T{0};
    T max = T{1};
    T step = T{0};  // 0 = continuous
    bool operator==(const Slider&) const = default;
};

template <Numeric T, Orientation O>
struct Widget<Slider<T, O>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr bool vertical = (O == Orientation::Vertical);
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = vertical ? ExpandAxis::Vertical : ExpandAxis::Horizontal;
    static constexpr float min_track_len = 200.f;
    static constexpr float track_thick = 6.f;
    static constexpr float thumb_len = 12.f;
    static constexpr float widget_extent = 30.f;

    static float track_len(const WidgetNode& node) {
        auto sz = node_allocated(node);
        float allocated = vertical ? sz.h.raw() : sz.w.raw();
        return allocated > 0 ? allocated : min_track_len;
    }

    static float ratio(const Slider<T, O>& s) {
        if (s.max == s.min) return 0.f;
        return static_cast<float>(s.value - s.min) / static_cast<float>(s.max - s.min);
    }

    static void record(DrawList& dl, const Field<Slider<T, O>>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        auto& s = field.get();
        float r = ratio(s);
        float tl = track_len(node);

        auto track_bg = vs.hovered ? t.track_hover : t.track;
        auto thumb_color = vs.pressed ? t.accent_active
                         : vs.hovered ? t.accent_hover
                         : t.accent;

        if constexpr (vertical) {
            X track_x{(widget_extent - track_thick) / 2.f};
            dl.filled_rect(detail::make_rect(track_x, Y{0}, Width{track_thick}, Height{tl}), track_bg);
            Y thumb_y{(1.f - r) * (tl - thumb_len)};
            dl.filled_rect(detail::make_rect(X{0}, thumb_y, Width{widget_extent}, Height{thumb_len}), thumb_color);
            if (vs.focused)
                dl.rect_outline(detail::make_rect(X{-1}, Y{-1}, Width{widget_extent + 2}, Height{tl + 2}),
                                t.focus_ring, 2.0f);
        } else {
            Y track_y{(widget_extent - track_thick) / 2.f};
            dl.filled_rect(detail::make_rect(X{0}, track_y, Width{tl}, Height{track_thick}), track_bg);
            X thumb_x{r * (tl - thumb_len)};
            dl.filled_rect(detail::make_rect(thumb_x, Y{0}, Width{thumb_len}, Height{widget_extent}), thumb_color);
            if (vs.focused)
                dl.rect_outline(detail::make_rect(X{-1}, Y{-1}, Width{tl + 2}, Height{widget_extent + 2}),
                                t.focus_ring, 2.0f);
        }
    }

    static void apply_position(Field<Slider<T, O>>& field, float pos, const WidgetNode& node) {
        float tl = track_len(node);
        float t = vertical ? std::clamp(1.f - pos / tl, 0.f, 1.f)
                           : std::clamp(pos / tl, 0.f, 1.f);
        auto& s = field.get();
        T raw = static_cast<T>(s.min + t * (s.max - s.min));
        Slider<T, O> updated = s;
        if (s.step != T{0}) {
            T steps = static_cast<T>((raw - s.min + s.step / T{2}) / s.step);
            updated.value = std::clamp(static_cast<T>(s.min + steps * s.step), s.min, s.max);
        } else {
            updated.value = raw;
        }
        field.set(updated);
    }

    static void handle_input(Field<Slider<T, O>>& field, const InputEvent& ev, WidgetNode& node) {
        auto extract = [](auto& e) { return vertical ? e.position.y.raw() : e.position.x.raw(); };
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            apply_position(field, extract(*mb), node);
        if (auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(node).pressed)
            apply_position(field, extract(*mm), node);
    }
};

// Sentinel: clickable button with text label
struct Button {
    std::string text;
    uint64_t click_count = 0;
    bool operator==(const Button&) const = default;
};

template <>
struct Widget<Button> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Button>& field, WidgetNode& node) {
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        Width w = detail::widget_w(node);
        Color bg = vs.pressed ? t.primary_active
                 : vs.hovered ? t.primary_hover
                 : t.primary;
        dl.filled_rect(detail::make_rect(X{0}, Y{0}, w, Height{32}), bg);
        dl.rect_outline(detail::make_rect(X{0}, Y{0}, w, Height{32}), t.primary_outline, 1.0f);
        dl.text(field.get().text, detail::make_point(X{8}, Y{7}), 14, t.text_on_primary);
        if (vs.focused)
            dl.rect_outline(detail::make_rect(X{-2}, Y{-2}, w + Width{4.f}, Height{36}), t.focus_ring, 2.0f);
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
struct Widget<TextField<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Height widget_h{30.f};
    // padding is used in both a horizontal (inset from left edge) and
    // vertical (inset from top edge) role in text_delegates.hpp — split
    // rather than force one Scalar<Tag> to serve both axes.
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Width cursor_w{2.f};

    // Defined in widget_tree.hpp (after WidgetNode is complete).
    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextField<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextField<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Widget<Password<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Height widget_h{30.f};
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Width cursor_w{2.f};

    static const TextEditState& get_edit_state(const WidgetNode& node);
    static TextEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<Password<T>>& field, WidgetNode& node);
    static void handle_input(Field<Password<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <StringLike T>
struct Widget<TextArea<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Width padding_x{4.f};
    static constexpr Height padding_y{4.f};
    static constexpr float font_size = 14.f;
    static constexpr Height line_height{font_size * 1.4f};
    static constexpr Width cursor_w{2.f};

    static const TextAreaEditState& get_edit_state(const WidgetNode& node);
    static TextAreaEditState& ensure_edit_state(WidgetNode& node);
    static void record(DrawList& dl, const Field<TextArea<T>>& field, WidgetNode& node);
    static void handle_input(Field<TextArea<T>>& field, const InputEvent& ev, WidgetNode& node);
};

// ScopedEnum delegate -- declared here, defined in widget_tree.hpp
template <ScopedEnum T>
    requires (!TextEditable<T>)
struct Widget<T> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<T>& field, WidgetNode& node);
    static void handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node);
};

template <ScopedEnum T>
struct Widget<Dropdown<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<Dropdown<T>>& field, WidgetNode& node);
    static void handle_input(Field<Dropdown<T>>& field, const InputEvent& ev, WidgetNode& node);
};

template <>
struct Widget<TabBar<>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;

    static void record(DrawList& dl, const Field<TabBar<>>& field, WidgetNode& node);
    static void handle_input(Field<TabBar<>>& field, const InputEvent& ev, WidgetNode& node);
};

} // namespace prism::ui
