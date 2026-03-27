#pragma once

#include <prism/core/draw_list.hpp>
#include <prism/core/field.hpp>
#include <prism/core/input_event.hpp>

#include <algorithm>
#include <string>
#include <variant>

namespace prism {

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <typename T>
concept StringLike = requires(const T& t) {
    { t.data() } -> std::convertible_to<const char*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

// Sentinel: read-only label
template <StringLike T = std::string>
struct Label {
    T value{};
    bool operator==(const Label&) const = default;
};

// Primary template: default delegate for any Field<T>.
// Renders a label-only widget, ignores input.
template <typename T>
struct Delegate {
    static void record(DrawList& dl, const Field<T>& field) {
        auto label_text = std::string(field.label);
        dl.filled_rect({0, 0, 200, 30}, Color::rgba(50, 50, 60));
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<T>&, const InputEvent&) {}
};

// bool specialization: toggle widget
template <>
struct Delegate<bool> {
    static void record(DrawList& dl, const Field<bool>& field) {
        auto label_text = std::string(field.label);
        auto bg = field.get()
            ? Color::rgba(0, 120, 80)
            : Color::rgba(50, 50, 60);
        dl.filled_rect({0, 0, 200, 30}, bg);
        dl.text(std::move(label_text), {4, 4}, 14, Color::rgba(220, 220, 220));
    }

    static void handle_input(Field<bool>& field, const InputEvent& ev) {
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed)
            field.set(!field.get());
    }
};

template <StringLike T>
struct Delegate<Label<T>> {
    static void record(DrawList& dl, const Field<Label<T>>& field) {
        dl.filled_rect({0, 0, 200, 24}, Color::rgba(40, 40, 48));
        dl.text(std::string(field.get().value.data(), field.get().value.size()),
                {4, 4}, 14, Color::rgba(180, 180, 190));
    }

    static void handle_input(Field<Label<T>>&, const InputEvent&) {}
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
    static constexpr float track_w = 200.f;
    static constexpr float track_h = 6.f;
    static constexpr float thumb_w = 12.f;
    static constexpr float widget_h = 30.f;

    static float ratio(const Slider<T>& s) {
        if (s.max == s.min) return 0.f;
        return static_cast<float>(s.value - s.min) / static_cast<float>(s.max - s.min);
    }

    static void record(DrawList& dl, const Field<Slider<T>>& field) {
        auto& s = field.get();
        float r = ratio(s);
        float track_y = (widget_h - track_h) / 2.f;

        // Track background
        dl.filled_rect({0, track_y, track_w, track_h}, Color::rgba(60, 60, 70));
        // Thumb
        float thumb_x = r * (track_w - thumb_w);
        dl.filled_rect({thumb_x, 0, thumb_w, widget_h}, Color::rgba(0, 140, 200));
        // Label
        dl.text(std::string(field.label), {track_w + 8, 4}, 14, Color::rgba(200, 200, 210));
    }

    static void handle_input(Field<Slider<T>>& field, const InputEvent& ev) {
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

} // namespace prism
